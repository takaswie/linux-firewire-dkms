/*
 * Audio and Music Data Transmission Protocol (IEC 61883-6) streams
 * with Common Isochronous Packet (IEC 61883-1) headers and MIDI comformant
 * data according to MMA/AMEI RP-027.
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/firewire.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include "amdtp.h"

#define TICKS_PER_CYCLE		3072
#define CYCLES_PER_SECOND	8000
#define TICKS_PER_SECOND	(TICKS_PER_CYCLE * CYCLES_PER_SECOND)

#define TRANSFER_DELAY_TICKS	0x2e00 /* 479.17 µs */

#define ISO_DATA_LENGTH_SHIFT	16
#define TAG_CIP			1
#define CIP_EOH_MASK		0x80000000
#define CIP_EOH_SHIFT		31
#define CIP_EOH			(1u << CIP_EOH_SHIFT)
#define CIP_FMT_MASK		0x3F000000
#define CIP_FMT_SHIFT		24
#define CIP_FMT_AM		(0x10 << CIP_FMT_SHIFT)
#define AMDTP_FDF_MASK		0x00FF0000
#define AMDTP_FDF_SFC_SHIFT	16
#define AMDTP_FDF_NO_DATA	(0xFF << AMDTP_FDF_SFC_SHIFT)
/* only "Clock-based rate controll mode" is supported */
#define AMDTP_FDF_AM824		(0 << (AMDTP_FDF_SFC_SHIFT + 3))
#define AMDTP_SYT_MASK		0x0000FFFF
#define AMDTP_DBS_MASK		0x00FF0000
#define AMDTP_DBS_SHIFT		16
#define AMDTP_DBC_MASK		0x000000FF

/* TODO: make these configurable */
#define INTERRUPT_INTERVAL	16
#define QUEUE_LENGTH		48
/* TODO: propper value? */
#define	STREAM_TIMEOUT_MS	100

static void pcm_period_tasklet(unsigned long data);

/**
 * amdtp_stream_init - initialize an AMDTP stream structure
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @flags: the packet transmission method to use
 */
int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		enum amdtp_stream_direction direction, enum cip_flags flags)
{
	int i;

	if (flags != CIP_NONBLOCKING)
		return -EINVAL;

	s->unit = fw_unit_get(unit);
	s->direction = direction;
	s->flags = flags;
	s->context = ERR_PTR(-1);
	mutex_init(&s->mutex);
	tasklet_init(&s->period_tasklet, pcm_period_tasklet, (unsigned long)s);
	s->packet_index = 0;

	s->pcm = NULL;
	for (i = 0; i < AMDTP_MAX_MIDI_STREAMS; i += 1)
		s->midi[i] = NULL;

	s->sync_mode = AMDTP_STREAM_SYNC_DRIVER_MASTER;
	s->sync_slave = ERR_PTR(-1);

	return 0;
}
EXPORT_SYMBOL(amdtp_stream_init);

/**
 * amdtp_stream_destroy - free stream resources
 * @s: the AMDTP stream to destroy
 */
void amdtp_stream_destroy(struct amdtp_stream *s)
{
	WARN_ON(amdtp_stream_running(s));
	mutex_destroy(&s->mutex);
	fw_unit_put(s->unit);
}
EXPORT_SYMBOL(amdtp_stream_destroy);

/**
 * amdtp_stream_set_rate - set the sample rate
 * @s: the AMDTP stream to configure
 * @rate: the sample rate
 *
 * The sample rate must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
void amdtp_stream_set_rate(struct amdtp_stream *s, unsigned int rate)
{
	static const struct {
		unsigned int rate;
		unsigned int syt_interval;
	} rate_info[] = {
		[CIP_SFC_32000]  = {  32000,  8, },
		[CIP_SFC_44100]  = {  44100,  8, },
		[CIP_SFC_48000]  = {  48000,  8, },
		[CIP_SFC_88200]  = {  88200, 16, },
		[CIP_SFC_96000]  = {  96000, 16, },
		[CIP_SFC_176400] = { 176400, 32, },
		[CIP_SFC_192000] = { 192000, 32, },
	};
	unsigned int sfc;

	if (WARN_ON(amdtp_stream_running(s)))
		return;

	for (sfc = 0; sfc < ARRAY_SIZE(rate_info); ++sfc)
		if (rate_info[sfc].rate == rate) {
			s->sfc = sfc;
			s->syt_interval = rate_info[sfc].syt_interval;
			return;
		}
	WARN_ON(1);
}
EXPORT_SYMBOL(amdtp_stream_set_rate);

/**
 * amdtp_stream_get_max_payload - get the stream's packet size
 * @s: the AMDTP stream
 *
 * This function must not be called before the stream has been configured
 * with amdtp_stream_set_hw_params(), amdtp_stream_set_pcm(), and
 * amdtp_stream_set_midi().
 */
unsigned int amdtp_stream_get_max_payload(struct amdtp_stream *s)
{
	static const unsigned int max_data_blocks[] = {
		[CIP_SFC_32000]  =  4,
		[CIP_SFC_44100]  =  6,
		[CIP_SFC_48000]  =  6,
		[CIP_SFC_88200]  = 12,
		[CIP_SFC_96000]  = 12,
		[CIP_SFC_176400] = 23,
		[CIP_SFC_192000] = 24,
	};

	s->data_block_quadlets = s->pcm_channels;
	s->data_block_quadlets += DIV_ROUND_UP(s->midi_ports, 8);

	if (s->direction == AMDTP_STREAM_RECEIVE)
		return 8 + s->syt_interval * s->data_block_quadlets * 4;
	else
		return 8 + max_data_blocks[s->sfc] * 4 * s->data_block_quadlets;
}
EXPORT_SYMBOL(amdtp_stream_get_max_payload);

static void amdtp_write_s16(struct amdtp_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames);
static void amdtp_write_s32(struct amdtp_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames);
static void amdtp_read_s16(struct amdtp_stream *s,
			   struct snd_pcm_substream *pcm,
			   __be32 *buffer, unsigned int frames);
static void amdtp_read_s32(struct amdtp_stream *s,
			   struct snd_pcm_substream *pcm,
			   __be32 *buffer, unsigned int frames);

/**
 * amdtp_stream_set_pcm_format - set the PCM format
 * @s: the AMDTP stream to configure
 * @format: the format of the ALSA PCM device
 *
 * The sample format must be set before the stream is started, and must not be
 * changed while the stream is running.
 */
void amdtp_stream_set_pcm_format(struct amdtp_stream *s,
				 snd_pcm_format_t format)
{
	if (WARN_ON(amdtp_stream_running(s)))
		return;

	switch (format) {
	default:
		WARN_ON(1);
		/* fall through */
	case SNDRV_PCM_FORMAT_S16:
		if (s->direction == AMDTP_STREAM_RECEIVE)
			s->transfer_samples = amdtp_read_s16;
		else
			s->transfer_samples = amdtp_write_s16;
		break;
	case SNDRV_PCM_FORMAT_S32:
		if (s->direction == AMDTP_STREAM_RECEIVE)
			s->transfer_samples = amdtp_read_s32;
		else
			s->transfer_samples = amdtp_write_s32;
		break;
	}
}
EXPORT_SYMBOL(amdtp_stream_set_pcm_format);

/**
 * amdtp_stream_pcm_prepare - prepare PCM device for running
 * @s: the AMDTP stream
 *
 * This function should be called from the PCM device's .prepare callback.
 */
void amdtp_stream_pcm_prepare(struct amdtp_stream *s)
{
	tasklet_kill(&s->period_tasklet);
	s->pcm_buffer_pointer = 0;
	s->pcm_period_pointer = 0;
	s->pointer_flush = true;
}
EXPORT_SYMBOL(amdtp_stream_pcm_prepare);

static unsigned int calculate_data_blocks(struct amdtp_stream *s)
{
	unsigned int phase, data_blocks;

	if (!cip_sfc_is_base_44100(s->sfc)) {
		/* Sample_rate / 8000 is an integer, and precomputed. */
		data_blocks = s->data_block_state;
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

static unsigned int calculate_syt(struct amdtp_stream *s,
				  unsigned int cycle)
{
	unsigned int syt_offset, phase, index, syt;

	if (s->last_syt_offset < TICKS_PER_CYCLE) {
		if (!cip_sfc_is_base_44100(s->sfc))
			syt_offset = s->last_syt_offset + s->syt_offset_state;
		else {
		/*
		 * The time, in ticks, of the n'th SYT_INTERVAL sample is:
		 *   n * SYT_INTERVAL * 24576000 / sample_rate
		 * Modulo TICKS_PER_CYCLE, the difference between successive
		 * elements is about 1386.23.  Rounding the results of this
		 * formula to the SYT precision results in a sequence of
		 * differences that begins with:
		 *   1386 1386 1387 1386 1386 1386 1387 1386 1386 1386 1387 ...
		 * This code generates _exactly_ the same sequence.
		 */
			phase = s->syt_offset_state;
			index = phase % 13;
			syt_offset = s->last_syt_offset;
			syt_offset += 1386 + ((index && !(index & 3)) ||
					      phase == 146);
			if (++phase >= 147)
				phase = 0;
			s->syt_offset_state = phase;
		}
	} else
		syt_offset = s->last_syt_offset - TICKS_PER_CYCLE;
	s->last_syt_offset = syt_offset;

	if (syt_offset < TICKS_PER_CYCLE) {
		syt_offset += TRANSFER_DELAY_TICKS - TICKS_PER_CYCLE;
		syt = (cycle + syt_offset / TICKS_PER_CYCLE) << 12;
		syt += syt_offset % TICKS_PER_CYCLE;

		return syt & 0xffff;
	} else {
		return 0xffff; /* no info */
	}
}

static void amdtp_write_s32(struct amdtp_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	const u32 *src;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*buffer = cpu_to_be32((*src >> 8) | 0x40000000);
			src++;
			buffer++;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void amdtp_write_s16(struct amdtp_stream *s,
			    struct snd_pcm_substream *pcm,
			    __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	const u16 *src;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*buffer = cpu_to_be32((*src << 8) | 0x40000000);
			src++;
			buffer++;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void amdtp_read_s32(struct amdtp_stream *s,
			   struct snd_pcm_substream *pcm,
			   __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	u32 *dst;

	channels = s->pcm_channels;
	dst  = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*dst = be32_to_cpu(*buffer) << 8;
			dst += 1;
			buffer += 1;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void amdtp_read_s16(struct amdtp_stream *s,
			   struct snd_pcm_substream *pcm,
			   __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, frame_step, i, c;
	u16 *dst;

	channels = s->pcm_channels;
	dst = (void *)runtime->dma_area +
			s->pcm_buffer_pointer * (runtime->frame_bits / 8);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;
	frame_step = s->data_block_quadlets - channels;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*dst = be32_to_cpu(*buffer) << 8;
			dst += 1;
			buffer += 1;
		}
		buffer += frame_step;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void amdtp_fill_pcm_silence(struct amdtp_stream *s,
				   __be32 *buffer, unsigned int frames)
{
	unsigned int i, c;

	for (i = 0; i < frames; ++i) {
		for (c = 0; c < s->pcm_channels; ++c)
			buffer[c] = cpu_to_be32(0x40000000);
		buffer += s->data_block_quadlets;
	}
}

static void amdtp_fill_midi(struct amdtp_stream *s,
			    __be32 *buffer, unsigned int frames)
{
	unsigned int m, f, c, port;
	int len;
	u8 b[4];

	/*
	 * This module can't support "negotiation procedure" in
	 * MMA/AMEI RP-027. Then a maximum data rate is 3,125 bytes per second
	 * without 1 byte label. This table is for the restriction. With this
	 * table, the maximum data rate is between 2,000 to 3,000 bytes per
	 * second.
	 */
	static const int block_interval[] = {
		[CIP_SFC_32000]   = 16,
		[CIP_SFC_44100]   = 16,
		[CIP_SFC_48000]   = 16,
		[CIP_SFC_88200]   = 32,
		[CIP_SFC_96000]   = 32,
		[CIP_SFC_176400]  = 64,
		[CIP_SFC_192000]  = 64
	};

	for (f = 0; f < frames; f += 1) {
		/* skip PCM data */
		buffer += s->pcm_channels;

		/*
		 * According to MMA/AMEI RP-027, one channels of AM824 can
		 * handle 8 MIDI streams.
		 */
		m = (s->data_block_counter + f) % 8;
		for (c = 0; c < DIV_ROUND_UP(s->midi_ports, 8); c += 1) {
			/* MIDI stream number */
			port = c * 8 + m;

			b[0] = 0x80;
			b[1] = 0x00;
			b[2] = 0x00;
			b[3] = 0x00;
			len = 0;

			if ((m == (s->data_block_counter + f) %
						block_interval[s->sfc]) &&
			    (s->midi[port] != NULL) &&
			    test_bit(port, &s->midi_triggered)) {
				len = snd_rawmidi_transmit(s->midi[port],
								b + 1, 1);
				if (len <= 0)
					b[1] = 0x00;
				else
					b[0] = 0x81;
			}

			buffer[c] = (b[0] << 24) | (b[1] << 16) |
							(b[2] << 8) | b[3];
			buffer[c] = be32_to_cpu(buffer[c]);
		}
		buffer += s->data_block_quadlets - s->pcm_channels;
	}
}

static void amdtp_pull_midi(struct amdtp_stream *s,
			    __be32 *buffer, unsigned int frames)
{
	unsigned int m, f, p, port;
	int len, ret;
	u8 *b;

	for (f = 0; f < frames; f += 1) {
		buffer += s->pcm_channels;

		m = (s->data_block_counter + f) % 8;

		for (p = 0; p < s->midi_ports; p += 1) {
			b = (u8 *)&buffer[p];

			if (b[0] < 0x81 || 0x83 < b[0])
				continue;

			len = b[0] - 0x80;

			/* MIDI stream number */
			port = p * 8 + m;

			if ((s->midi[port] == NULL) ||
			    !test_bit(port, &s->midi_triggered))
				continue;

			ret = snd_rawmidi_receive(s->midi[port], b + 1, len);
			if (ret != len) {
				dev_err(&s->unit->device,
				"MIDI[%d] receive: %08X %08X %08X %08X\n",
				port, b[0], b[1], b[2], b[3]);
			}
		}
		buffer += s->data_block_quadlets - s->pcm_channels;
	}
}

static void check_pcm_pointer(struct amdtp_stream *s,
			      struct snd_pcm_substream *pcm,
			      unsigned int frames)
{	unsigned int ptr;

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

static void pcm_period_tasklet(unsigned long data)
{
	struct amdtp_stream *s = (void *)data;
	struct snd_pcm_substream *pcm = ACCESS_ONCE(s->pcm);

	if (pcm)
		snd_pcm_period_elapsed(pcm);
}

static int queue_context_packet(struct amdtp_stream *s, unsigned int index,
				unsigned int header_length,
				unsigned int payload_length, bool skip)
{
	struct fw_iso_packet p = {0};
	int err;

	p.interrupt = IS_ALIGNED(index + 1, INTERRUPT_INTERVAL);
	p.tag = TAG_CIP;
	p.header_length = header_length;
	p.payload_length = payload_length;
	p.skip = (!skip) ? 0: 1;
	err = fw_iso_context_queue(s->context, &p, &s->buffer.iso_buffer,
				   s->buffer.packets[index].offset);
	if (err < 0) {
		dev_err(&s->unit->device, "queueing error: %d\n", err);
		s->packet_index = -1;
		goto end;
	}

	if (++index >= QUEUE_LENGTH)
		index = 0;
	s->packet_index = index;

	err = 0;

end:
	return err;
}

static void queue_out_packet(struct amdtp_stream *s, unsigned int cycle, bool nodata)
{
	__be32 *buffer;
	unsigned int index, syt, fdf, data_blocks, payload_length;
	struct snd_pcm_substream *pcm;

	if (s->packet_index < 0)
		return;
	index = s->packet_index;

	data_blocks = calculate_data_blocks(s);

	/*
	 * if the device is in non syt match mode,  then the value of syt field
	 * in transmit packet should derives from received packet.
	 */
	if (s->sync_mode == AMDTP_STREAM_SYNC_DEVICE_MASTER)
		syt = s->last_syt_offset;
	else
		syt = calculate_syt(s, cycle);

	if (!nodata)
		fdf = s->sfc << AMDTP_FDF_SFC_SHIFT;
	else
		fdf = AMDTP_FDF_NO_DATA;

	buffer = s->buffer.packets[index].buffer;
	buffer[0] = cpu_to_be32(ACCESS_ONCE(s->source_node_id_field) |
				(s->data_block_quadlets << AMDTP_DBS_SHIFT) |
				s->data_block_counter);
	buffer[1] = cpu_to_be32(CIP_EOH | CIP_FMT_AM | AMDTP_FDF_AM824 |
				fdf | syt);
	buffer += 2;

	if (!nodata) {
		pcm = ACCESS_ONCE(s->pcm);
		if (pcm)
			s->transfer_samples(s, pcm, buffer, data_blocks);
		else
			amdtp_fill_pcm_silence(s, buffer, data_blocks);

		if (s->midi_ports)
			amdtp_fill_midi(s, buffer, data_blocks);

		s->data_block_counter = (s->data_block_counter + data_blocks) & 0xff;
	}

	payload_length = 8 + data_blocks * 4 * s->data_block_quadlets;
	if (queue_context_packet(s, index, 0, payload_length, false) < 0) {
		amdtp_stream_pcm_abort(s);
		return;
	}

	if (pcm)
		check_pcm_pointer(s, pcm, data_blocks);
}

static void handle_in_packet_data(struct amdtp_stream *s,
				  unsigned int data_quadlets)
{
	__be32 *buffer;
	u32 cip_header[2];
	unsigned int index, data_block_quadlets, data_block_counter,
		     payload_length, frames = 0;
	struct snd_pcm_substream *pcm;
	bool nodata = false;

	if (s->packet_index < 0)
		return;
	index = s->packet_index;

	buffer = s->buffer.packets[index].buffer;
	cip_header[0] = be32_to_cpu(buffer[0]);
	cip_header[1] = be32_to_cpu(buffer[1]);

	/* checking CIP headers for AMDTP with restriction of this module */
	if (((cip_header[0] & CIP_EOH_MASK) == CIP_EOH) ||
	    ((cip_header[1] & CIP_EOH_MASK) != CIP_EOH) ||
	    ((cip_header[1] & CIP_FMT_MASK) != CIP_FMT_AM)) {
		dev_err(&s->unit->device, "CIP header error: %08X:%08X\n",
			cip_header[0], cip_header[1]);
		pcm = NULL;
	} else if ((data_quadlets < 3) ||
		   ((cip_header[1] & AMDTP_FDF_MASK) == AMDTP_FDF_NO_DATA)) {
		pcm = NULL;
	} else {
		/*
		 * NOTE: this module doesn't check dbc and syt field
		 *
		 * Echo Audio's Fireworks reports wrong number of data block
		 * counter. It always reports it with increment by 8 blocks
		 * even if actual data blocks different from 8.
		 *
		 * Handling syt field is related to "presentation" time stamp,
		 * but ALSA has no implements equivalent to it so this module
		 * don't support it.
		 */
		data_block_quadlets = (cip_header[0] & AMDTP_DBS_MASK) >>
								AMDTP_DBS_SHIFT;
		data_block_counter  = cip_header[0] & AMDTP_DBC_MASK;

		/*
		 * NOTE: Echo Audio's Fireworks reports a fixed value for data
		 * block quadlets but the actual value differs depending on
		 * current sampling rate. This is a workaround for Fireworks.
		 */
		if ((data_quadlets - 2) % data_block_quadlets > 0)
			s->data_block_quadlets = s->pcm_channels +
					DIV_ROUND_UP(s->midi_ports, 8);
		else
			s->data_block_quadlets = data_block_quadlets;

		/* finish to check CIP header */
		buffer += 2;

		/*
		 * NOTE: here "frames" is equivalent to "events"
		 * in IEC 61883-1
		 */
		frames = (data_quadlets - 2) / s->data_block_quadlets;
		if (frames == 0)
			pcm = NULL;
		else {
			pcm = ACCESS_ONCE(s->pcm);
			if (pcm)
				s->transfer_samples(s, pcm, buffer, frames);
			if (s->midi_ports)
				amdtp_pull_midi(s, buffer, frames);
		}

		/* for next packet */
		s->data_block_quadlets = data_block_quadlets;
		s->data_block_counter  = data_block_counter;
	}

	/* queueing a packet for next cycle */
	payload_length = amdtp_stream_get_max_payload(s);
	if (queue_context_packet(s, index, 4, payload_length, false) < 0) {
		amdtp_stream_pcm_abort(s);
		return;
	}

	/* process sync slave stream */
	if ((s->sync_mode == AMDTP_STREAM_SYNC_DEVICE_MASTER) &&
	    (!IS_ERR(s->sync_slave)) &&
	    (amdtp_stream_pcm_running(s->sync_slave))) {
		s->sync_slave->last_syt_offset =
					cip_header[1] & AMDTP_SYT_MASK;
		queue_out_packet(s->sync_slave, 0, nodata);
	}

	/* calculate period and buffer borders */
	if (pcm)
		check_pcm_pointer(s, pcm, frames);
}

static void out_packet_callback(struct fw_iso_context *context, u32 cycle,
			size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int i, packets = header_length / 4;

	/*
	 * Compute the cycle of the last queued packet.
	 * (We need only the four lowest bits for the SYT, so we can ignore
	 * that bits 0-11 must wrap around at 3072.)
	 */
	cycle += QUEUE_LENGTH - packets;

	for (i = 0; i < packets; ++i)
		queue_out_packet(s, ++cycle, false);
	fw_iso_context_queue_flush(s->context);
}

static void in_packet_callback(struct fw_iso_context *context, u32 cycle,
			size_t header_length, void *header, void *private_data)
{
	struct amdtp_stream *s = private_data;
	unsigned int p, data_quadlets, packets = header_length / 4;
	__be32 *headers = header;

	/* each fields in an isochronous header are already used in juju */
	for (p = 0; p < packets; p += 1) {
		/* how many quadlets of data in payload of this packet */
		data_quadlets =
			(be32_to_cpu(headers[p]) >> ISO_DATA_LENGTH_SHIFT) / 4;
		/* handle each data in payload */
		handle_in_packet_data(s, data_quadlets);
	}

	fw_iso_context_queue_flush(s->context);
}

/* processing is done by master callback */
static void sync_slave_callback(struct fw_iso_context *context, u32 cycle,
				size_t header_length, void *header,
				void *private_data)
{
	return;
}

/* this is executed one time */
static void amdtp_stream_callback(struct fw_iso_context *context, u32 cycle,
				  size_t header_length, void *header,
				  void *private_data)
{
	struct amdtp_stream *s = private_data;

	/* NOTE: in this module the first callback means running */
	s->run = true;

	/* TODO: overwriting sc is always OK because there's mc? */
	if (s->direction == AMDTP_STREAM_RECEIVE)
		context->callback.sc = in_packet_callback;
	else if (s->sync_mode == AMDTP_STREAM_SYNC_DRIVER_MASTER)
		context->callback.sc = out_packet_callback;
	else
		context->callback.sc = sync_slave_callback;

	context->callback.sc(context, cycle, header_length, header, s);

	return;
}

/**
 * amdtp_stream_start - start sending packets
 * @s: the AMDTP stream to start
 * @channel: the isochronous channel on the bus
 * @speed: firewire speed code
 *
 * The stream cannot be started until it has been configured with
 * amdtp_stream_set_hw_params(), amdtp_stream_set_pcm(), and
 * amdtp_stream_set_midi(); and it must be started before any
 * PCM or MIDI device can be started.
 */
int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed)
{
	static const struct {
		unsigned int data_block;
		unsigned int syt_offset;
	} initial_state[] = {
		[CIP_SFC_32000]  = {  4, 3072 },
		[CIP_SFC_48000]  = {  6, 1024 },
		[CIP_SFC_96000]  = { 12, 1024 },
		[CIP_SFC_192000] = { 24, 1024 },
		[CIP_SFC_44100]  = {  0,   67 },
		[CIP_SFC_88200]  = {  0,   67 },
		[CIP_SFC_176400] = {  0,   67 },
	};
	enum dma_data_direction dir;
	unsigned int header_size, payload_length;
	bool skip;
	int type, err;

	mutex_lock(&s->mutex);

	if (WARN_ON(amdtp_stream_running(s) ||
		    (!s->pcm_channels && !s->midi_ports))) {
		err = -EBADFD;
		goto err_unlock;
	}

	s->data_block_state = initial_state[s->sfc].data_block;
	s->syt_offset_state = initial_state[s->sfc].syt_offset;
	s->last_syt_offset = TICKS_PER_CYCLE;

	/* initialize packet buffer */
	if (s->direction == AMDTP_STREAM_RECEIVE) {
		dir = DMA_FROM_DEVICE;
		type = FW_ISO_CONTEXT_RECEIVE;
		header_size = 4;
		payload_length = amdtp_stream_get_max_payload(s);
		skip = false;
	} else {
		dir = DMA_TO_DEVICE;
		type = FW_ISO_CONTEXT_TRANSMIT;
		header_size = 0;
		payload_length = 0;
		skip = true;
	}
	err = iso_packets_buffer_init(&s->buffer, s->unit, QUEUE_LENGTH,
				      amdtp_stream_get_max_payload(s), dir);
	if (err < 0)
		goto err_unlock;

	/* NOTE: this callback is overwritten later */
	s->context = fw_iso_context_create(fw_parent_device(s->unit)->card,
					   type, channel, speed, header_size,
					   amdtp_stream_callback, s);
	if (!amdtp_stream_running(s)) {
		err = PTR_ERR(s->context);
		if (err == -EBUSY)
			dev_err(&s->unit->device,
				"no free stream on this controller\n");
		goto err_buffer;
	}

	amdtp_stream_update(s);
	s->data_block_counter = 0;

	s->packet_index = 0;
	do {
		err = queue_context_packet(s, s->packet_index, header_size,
					   payload_length, skip);
		if (err < 0)
			goto err_context;
	} while (s->packet_index > 0);

	/*
	 * NOTE:
	 * The fourth argument is effective for receive context and should be.
	 * "FW_ISO_CONTEXT_MATCH_TAG1" for receive but Fireworks outputs
	 * NODATA packets with tag 0.
	 */
	init_waitqueue_head(&s->run_wait);
	s->run = false;
	err = fw_iso_context_start(s->context, -1, 0,
			FW_ISO_CONTEXT_MATCH_TAG0 | FW_ISO_CONTEXT_MATCH_TAG1);
	if (err < 0)
		goto err_context;

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
EXPORT_SYMBOL(amdtp_stream_start);

/**
 * amdtp_stream_pcm_pointer - get the PCM buffer position
 * @s: the AMDTP stream that transports the PCM data
 *
 * Returns the current buffer position, in frames.
 */
unsigned long amdtp_stream_pcm_pointer(struct amdtp_stream *s)
{
	/* this optimization is allowed to be racy */
	if (s->pointer_flush)
		fw_iso_context_flush_completions(s->context);
	else
		s->pointer_flush = true;

	return ACCESS_ONCE(s->pcm_buffer_pointer);
}
EXPORT_SYMBOL(amdtp_stream_pcm_pointer);

/**
 * amdtp_stream_update - update the stream after a bus reset
 * @s: the AMDTP stream
 */
void amdtp_stream_update(struct amdtp_stream *s)
{
	ACCESS_ONCE(s->source_node_id_field) =
		(fw_parent_device(s->unit)->card->node_id & 0x3f) << 24;
}
EXPORT_SYMBOL(amdtp_stream_update);

/**
 * amdtp_stream_stop - stop sending packets
 * @s: the AMDTP stream to stop
 *
 * All PCM and MIDI devices of the stream must be stopped before the stream
 * itself can be stopped.
 */
void amdtp_stream_stop(struct amdtp_stream *s)
{
	mutex_lock(&s->mutex);

	if (!amdtp_stream_running(s)) {
		mutex_unlock(&s->mutex);
		return;
	}

	tasklet_kill(&s->period_tasklet);
	fw_iso_context_stop(s->context);
	fw_iso_context_destroy(s->context);
	s->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&s->buffer, s->unit);

	mutex_unlock(&s->mutex);
}
EXPORT_SYMBOL(amdtp_stream_stop);

/**
 * amdtp_stream_pcm_abort - abort the running PCM device
 * @s: the AMDTP stream about to be stopped
 *
 * If the isochronous stream needs to be stopped asynchronously, call this
 * function first to stop the PCM device.
 */
void amdtp_stream_pcm_abort(struct amdtp_stream *s)
{
	struct snd_pcm_substream *pcm;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm) {
		snd_pcm_stream_lock_irq(pcm);
		if (snd_pcm_running(pcm))
			snd_pcm_stop(pcm, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock_irq(pcm);
	}
}
EXPORT_SYMBOL(amdtp_stream_pcm_abort);

/**
 * amdtp_stream_wait_run - block till stream running or timeout
 * @s: the AMDTP stream
 *
 * If this function return false, the AMDTP stream should be stopped.
 */
bool amdtp_stream_wait_run(struct amdtp_stream *s)
{
	wait_event_timeout(s->run_wait,
			   s->run == true,
			   msecs_to_jiffies(STREAM_TIMEOUT_MS));
	return s->run;
}
EXPORT_SYMBOL(amdtp_stream_wait_run);

/**
 * amdtp_stream_midi_add - add MIDI stream
 * @s: the AMDTP stream
 * @substream: the MIDI stream to be added
 *
 * This function don't check the number of midi substream but it should be
 * within AMDTP_MAX_MIDI_STREAMS.
 */
void amdtp_stream_midi_add(struct amdtp_stream *s,
			   struct snd_rawmidi_substream *substream)
{
	ACCESS_ONCE(s->midi[substream->number]) = substream;
}
EXPORT_SYMBOL(amdtp_stream_midi_add);

/**
 * amdtp_stream_midi_remove - remove MIDI stream
 * @s: the AMDTP stream
 * @substream: the MIDI stream to be removed
 *
 * This function should not be automatically called by amdtp_stream_stop
 * because the AMDTP stream only with MIDI stream need to be restarted by
 * PCM streams at requested sampling rate.
 */
void amdtp_stream_midi_remove(struct amdtp_stream *s,
			      struct snd_rawmidi_substream *substream)
{
	ACCESS_ONCE(s->midi[substream->number]) = NULL;
}
EXPORT_SYMBOL(amdtp_stream_midi_remove);

/**
 * amdtp_stream_midi_running - check any MIDI streams are running or not
 * @s: the AMDTP stream
 *
 * If this function returns true, any MIDI streams are running.
 */
bool amdtp_stream_midi_running(struct amdtp_stream *s)
{
	int i;
	for (i = 0; i < AMDTP_MAX_MIDI_STREAMS; i += 1) {
		if (!IS_ERR_OR_NULL(s->midi[i]))
			return true;
	}

	return false;
}
EXPORT_SYMBOL(amdtp_stream_midi_running);
