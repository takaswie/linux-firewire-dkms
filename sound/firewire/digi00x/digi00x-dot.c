/*
 * digi00x-dot.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

#define ISO_DATA_HEADER_SIZE	4
#define ISO_DATA_FMT_TAG_CIP	1
#define ISO_DATA_LENGTH_SHIFT	16

/* common isochronous packet header parameters */
#define CIP_EOH			0x01u
#define CIP_EOH_SHIFT		31
#define CIP_EOH_MASK		0x80000000
#define CIP_DBS_SHIFT		16
/* In specification, this is for AM824, while vendor ignores it. */
#define CIP_FMT_DOT		0x10u
#define CIP_FMT_SHIFT		24
#define CIP_FMT_MASK		0x3f000000
#define CIP_FDF_SHIFT		16
#define CIP_FDF_MASK		0x00ff0000

/* Double-oh-three protocol. */
#define MAX_MIDI_RX_BLOCKS	8
#define QUEUE_LENGTH		48
#define INTERRUPT_INTERVAL	16

#define WAIT_TIMEOUT		1000

static void update_pcm_pointers(struct amdtp_stream *s,
				struct snd_pcm_substream *pcm,
				unsigned int frames)
{
	unsigned int ptr;

	ptr = s->pcm_buffer_pointer + frames;
	if (ptr >= pcm->runtime->buffer_size)
		ptr -= pcm->runtime->buffer_size;
	ACCESS_ONCE(s->pcm_buffer_pointer) = ptr;

	s->pcm_period_pointer += frames;
	if (s->pcm_period_pointer >= pcm->runtime->period_size) {
		s->pcm_period_pointer -= pcm->runtime->period_size;
		s->pointer_flush = false;
		tasklet_hi_schedule(&s->period_tasklet);
	}
}

static int queue_packet(struct amdtp_stream *s, unsigned int payload_length)
{
	struct iso_packets_buffer *buffer = &s->buffer;
	struct fw_iso_packet p = {0};
	unsigned int packet_index;
	int err = 0;

	if (amdtp_stream_running(s))
		goto end;

	p.interrupt = IS_ALIGNED(s->packet_index + 1, INTERRUPT_INTERVAL);
	p.tag = ISO_DATA_FMT_TAG_CIP;
	p.header_length = ISO_DATA_HEADER_SIZE;
	p.payload_length = payload_length;
	if (p.payload_length == 0)
		p.skip = true;

	packet_index = s->packet_index;
	err = fw_iso_context_queue(s->context, &p,
				   &s->buffer.iso_buffer,
				   buffer->packets[packet_index].offset);
	if (err < 0) {
		dev_err(&s->unit->device, "queueing error: %d\n", err);
		goto end;
	}

	if (++s->packet_index >= QUEUE_LENGTH)
		s->packet_index = 0;
end:
	return err;
}

static unsigned int calculate_data_blocks(struct amdtp_stream *s)
{
	unsigned int phase, data_blocks;

	if (!cip_sfc_is_base_44100(s->sfc)) {
		phase = s->data_block_state;
		if (phase >= 16)
			phase = 0;

		data_blocks = ((phase % 16) > 7) ? 5 : 7;
		if (++phase >= 16)
			phase = 0;
		s->data_block_state = phase;
	} else {
		phase = s->data_block_state;

		/*
		 * This calculates the number of data blocks per packet so that
		 * 1) the overall rate is correct and exactly synchronized to
		 *    the bus clock, and
		 * 2) packets with a rounded-up number of blocks occur as early
		 *    as possible in the sequence (to prevent underruns of the
		 *    device's buffer).
		 */
		if (s->sfc == CIP_SFC_44100)
			/* 6 6 5 6 5 6 5 ... */
			data_blocks = 5 + ((phase & 1) ^
					   (phase == 0 || phase >= 40));
		else
			/* 12 11 11 11 11 ... or 23 22 22 22 22 ... */
			data_blocks = 11 * (s->sfc >> 1) + (phase == 0);
		if (++phase >= (80 >> (s->sfc >> 1)))
			phase = 0;
		s->data_block_state = phase;
	}

	return data_blocks;
}

static void fill_pcm_s32(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			 __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u32 *src;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			buffer[c + 1] = cpu_to_be32((*src >> 8) | 0x40000000);
			src++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void fill_pcm_silence(struct amdtp_stream *s, __be32 *buffer,
			     unsigned int frames)
{
	unsigned int i, c;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < s->pcm_channels; ++c)
			buffer[c + 1] = cpu_to_be32(0x40000000);
		buffer += s->data_block_quadlets;
	}
}

static void fill_midi(struct amdtp_stream *s, __be32 *buffer,
		      unsigned int frames)
{
	unsigned int f, port;
	u8 *b;

	for (f = 0; f < frames; f++) {
		b = (u8 *)&buffer[0];

		port = (s->data_block_counter + f) % 8;
		if (f < MAX_MIDI_RX_BLOCKS &&
		    s->midi[port] != NULL &&
		    snd_rawmidi_transmit(s->midi[port], &b[1], 1) == 1) {
			b[0] = 0x81;
		} else {
			b[0] = 0x80;
			b[1] = 0;
		}
		b[2] = 0;
		b[3] = 0;

		buffer += s->data_block_quadlets;
	}
}

static void handle_out_packet(struct amdtp_stream *s, unsigned int cycle)
{
	__be32 *buffer;
	unsigned int index;
	unsigned int data_blocks;
	unsigned int payload_length;
	struct snd_pcm_substream *pcm;

	if (s->packet_index < 0)
		return;
	index = s->packet_index;

	data_blocks = calculate_data_blocks(s);

	buffer = s->buffer.packets[s->packet_index].buffer;
	buffer[0] = cpu_to_be32(
			ACCESS_ONCE(s->source_node_id_field) |
			(s->data_block_quadlets << CIP_DBS_SHIFT) |
			s->data_block_counter);
	buffer[1] = cpu_to_be32(
			((CIP_EOH << CIP_EOH_SHIFT) & CIP_EOH_MASK) |
			((CIP_FMT_DOT << CIP_FMT_SHIFT) & CIP_FMT_MASK) |
			((s->sfc << CIP_FDF_SHIFT) & CIP_FDF_MASK));
	buffer += 2;

	pcm = ACCESS_ONCE(s->pcm);

	if (pcm)
		fill_pcm_s32(s, pcm, buffer, data_blocks);
	else
		fill_pcm_silence(s, buffer, data_blocks);
	if (s->midi_ports)
		fill_midi(s, buffer, data_blocks);

	s->data_block_counter = (s->data_block_counter + data_blocks) & 0xff;

	payload_length = 8 + data_blocks * 4 * s->data_block_quadlets;
	if (queue_packet(s, payload_length) < 0) {
		s->packet_index = -1;
		amdtp_stream_pcm_abort(s);
		return;
	}

	if (pcm)
		update_pcm_pointers(s, pcm, data_blocks);
}

static void out_stream_callback(struct fw_iso_context *context, u32 cycle,
				size_t header_length, void *header,
				void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int p, packets;

	packets = header_length / 4;

	/*
	 * Compute the cycle of the last queued packet.
	 * (We need only the four lowest bits for the SYT, so we can ignore
	 * that bits 0-11 must wrap around at 3072.)
	 */
	cycle += QUEUE_LENGTH - packets;

	for (p = 0; p < packets; ++p)
		handle_out_packet(s, ++cycle);

	fw_iso_context_queue_flush(s->context);
}

static void dot_stream_first_callback(struct fw_iso_context *context,
				      u32 cycle, size_t header_length,
				      void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	s->callbacked = true;
	wake_up(&s->callback_wait);

	context->callback.sc = out_stream_callback;
	context->callback.sc(context, cycle, header_length, header, s);
}

int snd_dg00x_dot_start(struct amdtp_stream *s, int channel, int speed)
{
	int err;

	mutex_lock(&s->mutex);

	if (WARN_ON(amdtp_stream_running(s))) {
		err = -EBADFD;
		goto err_unlock;
	}

	err = iso_packets_buffer_init(&s->buffer, s->unit, QUEUE_LENGTH,
				      amdtp_stream_get_max_payload(s),
				      DMA_TO_DEVICE);
	if (err < 0)
		goto err_unlock;

	/* Create isochronous context. */
	s->context = fw_iso_context_create(
				fw_parent_device(s->unit)->card,
				FW_ISO_CONTEXT_TRANSMIT, channel,
				fw_parent_device(s->unit)->max_speed,
				ISO_DATA_HEADER_SIZE, dot_stream_first_callback,
				s);
	if (IS_ERR(s->context)) {
		err = PTR_ERR(s->context);
		if (err == -EBUSY)
			dev_err(&s->unit->device,
				"no free contexts on this controller\n");
		goto err_buffer;
	}

	amdtp_stream_update(s);

	/* Queue skip packets. */
	s->packet_index = 0;
	do {
		err = queue_packet(s, 0);
		if (err < 0)
			goto err_context;
	} while (s->packet_index > 0);

	/* Start isochronous transmit context. */
	err = fw_iso_context_start(s->context, -1, 0,
				   FW_ISO_CONTEXT_MATCH_TAG1);
	if (err < 0)
		goto err_context;

	/* Wait for the first callback. */
	if (wait_event_timeout(s->callback_wait,
			       s->callbacked == true,
			       msecs_to_jiffies(WAIT_TIMEOUT)) <= 0) {
		err = -ETIMEDOUT;
		fw_iso_context_stop(s->context);
		goto err_context;
	}

	mutex_unlock(&s->mutex);

	return 0;
err_context:
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
err_buffer:
	iso_packets_buffer_destroy(&s->buffer, s->unit);
err_unlock:
	mutex_unlock(&s->mutex);

	return err;
}

void snd_dg00x_dot_stop(struct amdtp_stream *s)
{
	if (!amdtp_stream_running(s))
		return;

	mutex_lock(&s->mutex);

	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);

	s->callbacked = false;

	mutex_unlock(&s->mutex);
}

int snd_dg00x_dot_init(struct amdtp_stream *s, struct fw_unit *unit)
{
	s->unit = unit;
	s->context = ERR_PTR(-1);
	mutex_init(&s->mutex);
	s->packet_index = 0;

	init_waitqueue_head(&s->callback_wait);

	return 0;
}

void snd_dg00x_dot_destroy(struct amdtp_stream *s)
{
	WARN_ON(amdtp_stream_running(s));
	mutex_destroy(&s->mutex);
}
