/*
 * digi00x-e.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

#define ISO_DATA_FMT_TAG_CIP	1
#define ISO_DATA_LENGTH_SHIFT	16
#define CIP_HEADER_SIZE		4

#define QUEUE_LENGTH		48
#define INTERRUPT_INTERVAL	16

#define WAIT_TIMEOUT		1000

bool snd_dg00x_engine_running(struct snd_dg00x_engine *engine)
{
        return !IS_ERR(engine->context);
}

void snd_dg00x_engine_set_params(struct snd_dg00x_engine *engine,
				 unsigned int rate,
				 unsigned int pcm_data_channels,
				 unsigned int midi_data_channels)
{
	unsigned int sfc;

	for (sfc = 0; sfc < ARRAY_SIZE(amdtp_rate_table); ++sfc) {
		if (amdtp_rate_table[sfc] == rate)
			engine->sfc = sfc;
	}

	engine->pcm_data_channels = pcm_data_channels;
	engine->midi_data_channels = midi_data_channels;
}

unsigned int snd_dg00x_engine_get_payload_size(struct snd_dg00x_engine *engine)
{
	return 8 + (engine->pcm_data_channels + engine->midi_data_channels) * 4;
}

static int queue_packet(struct snd_dg00x_engine *engine,
			unsigned int payload_length)
{
	struct iso_packets_buffer *buffer = &engine->buffer;
	struct fw_iso_packet p = {0};
	unsigned int packet_index;
	int err = 0;

	if (!snd_dg00x_engine_running(engine))
		goto end;

	p.interrupt = IS_ALIGNED(engine->packet_index + 1, INTERRUPT_INTERVAL);
	p.tag = ISO_DATA_FMT_TAG_CIP;
	p.header_length = CIP_HEADER_SIZE;
	p.payload_length = payload_length;
	if (p.payload_length == 0)
		p.skip = true;

	packet_index = engine->packet_index;
	err = fw_iso_context_queue(engine->context, &p,
				   &engine->buffer.iso_buffer,
				   buffer->packets[packet_index].offset);
	if (err < 0) {
		dev_err(&engine->unit->device, "queueing error: %d\n", err);
		goto end;
	}

	if (++engine->packet_index >= QUEUE_LENGTH)
		engine->packet_index = 0;
end:
	return err;
}

static void handle_out_packet(struct snd_dg00x_engine *engine)
{
	__be32 *buffer;
	unsigned int data_blocks, payload_length;

	buffer = engine->buffer.packets[engine->packet_index].buffer;
}

static void out_packets_callback(struct fw_iso_context *context, u32 cycle,
				 size_t header_length, void *header,
				 void *private_data)
{
	struct snd_dg00x_engine *engine = private_data;
	unsigned int p, packets, payload_size;

	packets = header_length / 4;

	for (p = 0; p < packets; ++p) {
		if (engine->packet_index < 0)
			break;

		handle_out_packet(engine);

		queue_packet(engine, payload_size);
	}

	fw_iso_context_queue_flush(engine->context);
}

void snd_dg00x_engine_update(struct snd_dg00x_engine *e)
{
	ACCESS_ONCE(e->source_node_id_field) =
		(fw_parent_device(e->unit)->card->node_id & 0x3f) << 24;
}

static void amdtp_stream_first_callback(struct fw_iso_context *context,
					u32 cycle, size_t header_length,
					void *header, void *private_data)
{
	struct snd_dg00x_engine *engine = private_data;
	engine->callbacked = true;
	wake_up(&engine->callback_wait);

	context->callback.sc = out_packets_callback;
	context->callback.sc(context, cycle, header_length, header, engine);
}

int snd_dg00x_engine_start(struct snd_dg00x *dg00x, int channel, int speed,
			   struct snd_dg00x_engine *engine)
{
	int err;

	mutex_lock(&engine->mutex);

	if (WARN_ON(snd_dg00x_engine_running(engine))) {
		err = -EBADFD;
		goto err_unlock;
	}

	err = iso_packets_buffer_init(&engine->buffer, engine->unit,
				QUEUE_LENGTH,
				snd_dg00x_engine_get_payload_size(engine),
				DMA_TO_DEVICE);
	if (err < 0)
		goto err_unlock;

	/* Create isochronous context. */
	engine->context = fw_iso_context_create(
				fw_parent_device(engine->unit)->card,
				FW_ISO_CONTEXT_TRANSMIT, channel,
				fw_parent_device(engine->unit)->max_speed,
				CIP_HEADER_SIZE, amdtp_stream_first_callback,
				engine);
	if (IS_ERR(engine->context)) {
		err = PTR_ERR(engine->context);
		if (err == -EBUSY)
			dev_err(&engine->unit->device,
				"no free contexts on this controller\n");
		goto err_buffer;
	}

	snd_dg00x_engine_update(engine);

	/* Queue skip packets. */
	engine->packet_index = 0;
	do {
		err = queue_packet(engine, 0);
		if (err < 0)
			goto err_context;
	} while (engine->packet_index > 0);

	/* Start isochronous transmit context. */
	err = fw_iso_context_start(engine->context, -1, 0,
				   FW_ISO_CONTEXT_MATCH_TAG1);
	if (err < 0)
		goto err_context;

	/* Wait for the first callback. */
	if (wait_event_timeout(engine->callback_wait,
			       engine->callbacked == true,
			       msecs_to_jiffies(WAIT_TIMEOUT)) <= 0) {
		err = -ETIMEDOUT;
		goto err_wait;
	}

	mutex_unlock(&engine->mutex);

	return 0;
err_wait:
	fw_iso_context_stop(engine->context);
err_context:
	fw_iso_context_destroy(engine->context);
	engine->context = ERR_PTR(-1);
err_buffer:
	iso_packets_buffer_destroy(&engine->buffer, engine->unit);
err_unlock:
	mutex_unlock(&engine->mutex);

	return err;
}

void snd_dg00x_engine_stop(struct snd_dg00x_engine *engine)
{
	if (!snd_dg00x_engine_running(engine))
		return;

	mutex_lock(&engine->mutex);

	fw_iso_context_stop(engine->context);
	fw_iso_context_destroy(engine->context);
	engine->context = ERR_PTR(-1);
	iso_packets_buffer_destroy(&engine->buffer, engine->unit);

	engine->callbacked = false;

	mutex_unlock(&engine->mutex);
}

int snd_dg00x_engine_init(struct snd_dg00x *dg00x,
			  struct snd_dg00x_engine *engine)
{
	engine->unit = dg00x->unit;
	engine->context = ERR_PTR(-1);
	mutex_init(&engine->mutex);
	engine->packet_index = 0;

	init_waitqueue_head(&engine->callback_wait);

	return 0;
}

void snd_dg00x_engine_destroy(struct snd_dg00x *dg00x,
			      struct snd_dg00x_engine *engine)
{
	WARN_ON(snd_dg00x_engine_running(engine));
	mutex_destroy(&engine->mutex);
}
