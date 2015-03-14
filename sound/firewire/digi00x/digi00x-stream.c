/*
 * digi00x-stream.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

#define CALLBACK_TIMEOUT 500

const unsigned int snd_dg00x_stream_rates[SND_DG00X_RATE_COUNT] = {
	[0] = 44100,
	[1] = 48000,
	[2] = 88200,
	[3] = 96000,
};

/* Multi Bit Linear Audio data channels for each sampling transfer frequency. */
const unsigned int
snd_dg00x_stream_mbla_data_channels[SND_DG00X_RATE_COUNT] = {
	/* Analog/ADAT/SPDIF */
	[0] = (8 + 8 + 2),
	[1] = (8 + 8 + 2),
	/* Analog/SPDIF */
	[2] = (8 + 2),
	[3] = (8 + 2),
};

int snd_dg00x_stream_get_rate(struct snd_dg00x *dg00x, unsigned int *rate)
{
	__be32 data;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 0xffffe0000110ull, &data, sizeof(data), 0);
	if (err < 0)
		goto end;

	data = be32_to_cpu(data) & 0x0f;
	if (data >= ARRAY_SIZE(snd_dg00x_stream_rates)) {
		err = -EIO;
		goto end;
	}

	*rate = snd_dg00x_stream_rates[data];
end:
	return err;
}

int snd_dg00x_stream_set_rate(struct snd_dg00x *dg00x, unsigned int rate)
{
	__be32 data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(snd_dg00x_stream_rates); i++) {
		if (rate == snd_dg00x_stream_rates[i])
			break;
	}
	if (i == ARRAY_SIZE(snd_dg00x_stream_rates))
		return -EIO;

	data = cpu_to_be32(i);
	return snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
				  0xffffe0000110ull, &data, sizeof(data), 0);
}

int snd_dg00x_stream_get_clock(struct snd_dg00x *dg00x,
			       enum snd_dg00x_clock *clock)
{
	__be32 data;
	int err;

	err = snd_fw_transaction(dg00x->unit, TCODE_READ_QUADLET_REQUEST,
				 0xffffe0000118ull, &data, sizeof(data), 0);
	if (err < 0)
		return err;

	*clock = be32_to_cpu(data) & 0x0f;
	if (*clock >= ARRAY_SIZE(snd_dg00x_stream_rates))
		err = -EIO;

	return err;
}

static void finish_session(struct snd_dg00x *dg00x)
{
	__be32 data = cpu_to_be32(0x00000003);

	snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
			   0xffffe0000004ull, &data, sizeof(data), 0);
}

static int begin_session(struct snd_dg00x *dg00x)
{
	__be32 data;
	u32 curr;
	int err;

	err = snd_fw_transaction(dg00x->unit,
				 TCODE_READ_QUADLET_REQUEST,
				 0xffffe0000000ull,
				 &data, sizeof(data), 0);
	if (err < 0)
		goto error;
	curr = be32_to_cpu(data);

	if (curr == 0)
		curr = 2;

	curr--;
	while (curr > 0) {
		data = cpu_to_be32(curr);
		err = snd_fw_transaction(dg00x->unit,
					 TCODE_WRITE_QUADLET_REQUEST,
					 0xffffe0000004ull,
					 &data, sizeof(data), 0);
		if (err < 0)
			goto error;

		msleep(20);
		curr--;
	}

	return 0;
error:
	finish_session(dg00x);
	return err;
}

static void release_resources(struct snd_dg00x *dg00x)
{
	__be32 data = 0;

	/* Unregister isochronous channels for both direction. */
	snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
			   0xffffe0000100ull, &data, sizeof(data), 0);

	/* Release isochronous resources. */
	fw_iso_resources_free(&dg00x->tx_resources);
	fw_iso_resources_free(&dg00x->rx_resources);

	msleep(300);
}

static int keep_resources(struct snd_dg00x *dg00x, unsigned int rate)
{
	unsigned int i, p;
	__be32 data;
	int err;

	/* Check sampling rate. */
	for (i = 0; i < SND_DG00X_RATE_COUNT; i++) {
		if (snd_dg00x_stream_rates[i] == rate)
			break;
	}
	if (i == SND_DG00X_RATE_COUNT)
		return -EINVAL;

	/* Keep resources for out-stream. */
	amdtp_stream_set_parameters(&dg00x->rx_stream, rate,
				    snd_dg00x_stream_mbla_data_channels[i], 1);
	err = fw_iso_resources_allocate(&dg00x->rx_resources,
				amdtp_stream_get_max_payload(&dg00x->rx_stream),
				fw_parent_device(dg00x->unit)->max_speed);
	if (err < 0)
		return err;

	/* Keep resources for in-stream. */
	amdtp_stream_set_parameters(&dg00x->tx_stream, rate,
				    snd_dg00x_stream_mbla_data_channels[i], 1);
	err = fw_iso_resources_allocate(&dg00x->tx_resources,
				amdtp_stream_get_max_payload(&dg00x->tx_stream),
				fw_parent_device(dg00x->unit)->max_speed);
	if (err < 0)
		goto error;

	/* Register isochronous channels for both direction. */
	data = cpu_to_be32((dg00x->tx_resources.channel << 16) |
			   dg00x->rx_resources.channel);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_QUADLET_REQUEST,
				 0xffffe0000100ull, &data, sizeof(data), 0);
	if (err < 0)
		goto error;

	/* The first data channel in a packet is for MIDI conformant data. */
	for (p = 0; p < snd_dg00x_stream_mbla_data_channels[i]; p++) {
		dg00x->rx_stream.pcm_positions[p] = p + 1;
		dg00x->tx_stream.pcm_positions[p] = p + 1;
	}
	dg00x->rx_stream.midi_position = 0;
	dg00x->tx_stream.midi_position = 0;

	return 0;
error:
	release_resources(dg00x);
	return err;
}

int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x)
{
	int err;

	/* For out-stream. */
	err = fw_iso_resources_init(&dg00x->rx_resources, dg00x->unit);
	if (err < 0)
		return err;
	err = amdtp_stream_init(&dg00x->rx_stream, dg00x->unit,
				AMDTP_OUT_STREAM, CIP_NONBLOCKING);

	/* For in-stream. */
	err = fw_iso_resources_init(&dg00x->tx_resources, dg00x->unit);
	if (err < 0)
		return err;
	err = amdtp_stream_init(&dg00x->tx_stream, dg00x->unit,
				AMDTP_IN_STREAM,
				CIP_BLOCKING | CIP_SKIP_INIT_DBC_CHECK);
	if (err < 0)
		amdtp_stream_destroy(&dg00x->rx_stream);

	return err;
}

/*
 * This function should be called before starting streams or after stopping
 * streams.
 */
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x)
{
	amdtp_stream_destroy(&dg00x->rx_stream);
	fw_iso_resources_destroy(&dg00x->rx_resources);

	amdtp_stream_destroy(&dg00x->tx_stream);
	fw_iso_resources_destroy(&dg00x->tx_resources);
}

int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x, unsigned int rate)
{
	unsigned int curr_rate;
	int err = 0;

	if (dg00x->playback_substreams == 0 &&
	    dg00x->capture_substreams == 0)
		goto end;

	/* Check current sampling rate. */
	err = snd_dg00x_stream_get_rate(dg00x, &curr_rate);
	if (err < 0)
		goto error;
	/* For MIDI substreams. */
	if (rate == 0)
		rate = curr_rate;
	if ((curr_rate != rate) |
	    amdtp_streaming_error(&dg00x->tx_stream) |
	    amdtp_streaming_error(&dg00x->rx_stream)) {
		finish_session(dg00x);

		amdtp_stream_stop(&dg00x->tx_stream);
		amdtp_stream_stop(&dg00x->rx_stream);
		release_resources(dg00x);
	}

	/* No streams are transmitted without receiving a stream. */
	if (!amdtp_stream_running(&dg00x->rx_stream)) {
		err = snd_dg00x_stream_set_rate(dg00x, rate);
		if (err < 0)
			goto error;

		err = keep_resources(dg00x, rate);
		if (err < 0)
			goto error;

		err = begin_session(dg00x);
		if (err < 0)
			goto error;

		err = amdtp_stream_start(&dg00x->rx_stream,
				dg00x->rx_resources.channel,
				fw_parent_device(dg00x->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&dg00x->rx_stream,
						CALLBACK_TIMEOUT)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}

	/*
	 * The value of SYT field in transmitted packets is always 0x0000. Thus,
	 * duplex streams with timestamp synchronization cannot be built.
	 */
	if (dg00x->capture_substreams > 0 &&
	    !amdtp_stream_running(&dg00x->tx_stream)) {
		err = amdtp_stream_start(&dg00x->tx_stream,
				dg00x->tx_resources.channel,
				fw_parent_device(dg00x->unit)->max_speed);
		if (err < 0)
			goto error;

		if (!amdtp_stream_wait_callback(&dg00x->tx_stream,
						CALLBACK_TIMEOUT)) {
			err = -ETIMEDOUT;
			goto error;
		}
	}
end:
	return err;
error:
	finish_session(dg00x);

	amdtp_stream_stop(&dg00x->tx_stream);
	amdtp_stream_stop(&dg00x->rx_stream);
	release_resources(dg00x);

	return err;
}

void snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x)
{
	if (dg00x->capture_substreams > 0)
		return;
	amdtp_stream_stop(&dg00x->tx_stream);

	if (dg00x->playback_substreams > 0)
		return;
	finish_session(dg00x);
	amdtp_stream_stop(&dg00x->rx_stream);
	release_resources(dg00x);
}

/* TODO: investigation. */
void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x)
{
	fw_iso_resources_update(&dg00x->tx_resources);
	fw_iso_resources_update(&dg00x->rx_resources);

	amdtp_stream_update(&dg00x->tx_stream);
	amdtp_stream_update(&dg00x->rx_stream);
}

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x)
{
	dg00x->dev_lock_changed = true;
	wake_up(&dg00x->hwdep_wait);
}

int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x)
{
	int err;

	spin_lock_irq(&dg00x->lock);

	/* user land lock this */
	if (dg00x->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (dg00x->dev_lock_count++ == 0)
		snd_dg00x_stream_lock_changed(dg00x);
	err = 0;
end:
	spin_unlock_irq(&dg00x->lock);
	return err;
}

void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x)
{
	spin_lock_irq(&dg00x->lock);

	if (WARN_ON(dg00x->dev_lock_count <= 0))
		goto end;
	if (--dg00x->dev_lock_count == 0)
		snd_dg00x_stream_lock_changed(dg00x);
end:
	spin_unlock_irq(&dg00x->lock);
}
