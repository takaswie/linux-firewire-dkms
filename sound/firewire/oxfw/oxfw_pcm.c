/*
 * oxfw_pcm.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./oxfw.h"

static int
hw_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
	     struct snd_oxfw *oxfw,
	     struct snd_oxfw_stream_formation *formations)
{
	struct snd_interval *r =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int i;

	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
		/* entry is invalid */
		if (formations[i].pcm == 0)
			continue;

		if (!snd_interval_test(c, formations[i].pcm))
			continue;

		t.min = min(t.min, snd_oxfw_rate_table[i]);
		t.max = max(t.max, snd_oxfw_rate_table[i]);

	}
	return snd_interval_refine(r, &t);
}

static int
hw_rule_channels(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
		 struct snd_oxfw *oxfw,
		 struct snd_oxfw_stream_formation *formations)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};

	unsigned int i;

	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
		/* entry is invalid */
		if (formations[i].pcm == 0)
			continue;

		if (!snd_interval_test(r, snd_oxfw_rate_table[i]))
			continue;

		t.min = min(t.min, formations[i].pcm);
		t.max = max(t.max, formations[i].pcm);
	}

	return snd_interval_refine(c, &t);
}

static inline int
hw_rule_capture_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_rate(params, rule, oxfw,
				oxfw->tx_stream_formations);
}

static inline int
hw_rule_playback_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_rate(params, rule, oxfw,
				oxfw->rx_stream_formations);
}

static inline int
hw_rule_capture_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_channels(params, rule, oxfw,
				oxfw->tx_stream_formations);
}

static inline int
hw_rule_playback_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_channels(params, rule, oxfw,
				oxfw->rx_stream_formations);
}

static void
prepare_channels(struct snd_pcm_hardware *hw,
	  struct snd_oxfw_stream_formation *formations)
{
	unsigned int i;

	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->channels_min = min(hw->channels_min, formations[i].pcm);
		hw->channels_max = max(hw->channels_max, formations[i].pcm);
	}

	return;
}

static void
prepare_rates(struct snd_pcm_hardware *hw,
	  struct snd_oxfw_stream_formation *formations)
{
	unsigned int i;

	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->rate_min = min(hw->rate_min, snd_oxfw_rate_table[i]);
		hw->rate_max = max(hw->rate_max, snd_oxfw_rate_table[i]);
		hw->rates |= snd_pcm_rate_to_rate_bit(snd_oxfw_rate_table[i]);
	}

	return;
}

static int
pcm_init_hw_params(struct snd_oxfw *oxfw,
			struct snd_pcm_substream *substream)
{
	int err;

	static const struct snd_pcm_hardware hw = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_SYNC_START |
			SNDRV_PCM_INFO_FIFO_IN_FRAMES |
			SNDRV_PCM_INFO_JOINT_DUPLEX |
			/* for Open Sound System compatibility */
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		/* set up later */
		.rates = 0,
		.rate_min = UINT_MAX,
		.rate_max = 0,
		/* set up later */
		.channels_min = UINT_MAX,
		.channels_max = 0,
		.buffer_bytes_max = 1024 * 1024 * 1024,
		.period_bytes_min = 256,
		.period_bytes_max = 1024 * 1024 * 1024 / 2,
		.periods_min = 2,
		.periods_max = 32,
		.fifo_size = 0,
	};

	substream->runtime->hw = hw;
	substream->runtime->delay = substream->runtime->hw.fifo_size;

	/* add rule between channels and sampling rate */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		prepare_rates(&substream->runtime->hw,
			      oxfw->tx_stream_formations);
		prepare_channels(&substream->runtime->hw,
				 oxfw->tx_stream_formations);
		substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
		snd_pcm_hw_rule_add(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_capture_channels, oxfw,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_capture_rate, oxfw,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	} else {
		prepare_rates(&substream->runtime->hw,
			      oxfw->rx_stream_formations);
		prepare_channels(&substream->runtime->hw,
				 oxfw->rx_stream_formations);
		substream->runtime->hw.formats = AMDTP_OUT_PCM_FORMAT_BITS;
		snd_pcm_hw_rule_add(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_playback_channels, oxfw,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_playback_rate, oxfw,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		goto end;

	/*
	 * AMDTP functionality in firewire-lib require periods to be aligned to
	 * 16 bit, or 24bit inner 32bit.
	 */
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (err < 0)
		goto end;

	/* time for period constraint */
	err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					500, UINT_MAX);
	if (err < 0)
		goto end;
end:
	return err;
}

static int
pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	unsigned int sampling_rate;
//	bool internal;
	int err;

	err = snd_oxfw_stream_lock_try(oxfw);
	if (err < 0)
		goto end;

	err = pcm_init_hw_params(oxfw, substream);
	if (err < 0)
		goto err_locked;

	/*
	err = snd_oxfw_stream_check_internal_clock(oxfw, &internal);
	if (err < 0)
		goto err_locked;
	*/

	/*
	 * When source of clock is internal or any PCM stream are running,
	 * the available sampling rate is limited at current sampling rate.
	 */
	//if (!internal ||
	if (
	    amdtp_stream_pcm_running(&oxfw->tx_stream) ||
	    amdtp_stream_pcm_running(&oxfw->rx_stream)) {
		err = snd_oxfw_stream_get_rate(oxfw, &sampling_rate);
		if (err < 0)
			goto err_locked;

		substream->runtime->hw.rate_min = sampling_rate;
		substream->runtime->hw.rate_max = sampling_rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
err_locked:
	snd_oxfw_stream_lock_release(oxfw);
	return err;
}

static int
pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	snd_oxfw_stream_lock_release(oxfw);
	return 0;
}

static int
pcm_hw_params(struct snd_pcm_substream *substream,
	      struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int
pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	snd_oxfw_stream_stop_duplex(oxfw);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int
pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_oxfw_stream_start_duplex(oxfw, &oxfw->tx_stream,
					    runtime->rate);
	if (err < 0)
		goto end;

	amdtp_stream_set_pcm_format(&oxfw->tx_stream, runtime->format);
	amdtp_stream_pcm_prepare(&oxfw->tx_stream);
end:
	return err;
}
static int
pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_oxfw_stream_start_duplex(oxfw, &oxfw->rx_stream,
					    runtime->rate);
	if (err < 0)
		goto end;

	amdtp_stream_set_pcm_format(&oxfw->rx_stream, runtime->format);
	amdtp_stream_pcm_prepare(&oxfw->rx_stream);
end:
	return err;
}

static int
pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_oxfw *oxfw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&oxfw->tx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&oxfw->tx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static int
pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_oxfw *oxfw = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&oxfw->rx_stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&oxfw->rx_stream, NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t
pcm_capture_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_oxfw *oxfw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&oxfw->tx_stream);
}
static snd_pcm_uframes_t
pcm_playback_pointer(struct snd_pcm_substream *sbstrm)
{
	struct snd_oxfw *oxfw = sbstrm->private_data;
	return amdtp_stream_pcm_pointer(&oxfw->rx_stream);
}

static struct snd_pcm_ops pcm_capture_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_capture_prepare,
	.trigger	= pcm_capture_trigger,
	.pointer	= pcm_capture_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
};
static struct snd_pcm_ops pcm_playback_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_playback_prepare,
	.trigger	= pcm_playback_trigger,
	.pointer	= pcm_playback_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
	.mmap		= snd_pcm_lib_mmap_vmalloc,
};

int snd_oxfw_create_pcm_devices(struct snd_oxfw *oxfw)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(oxfw->card, oxfw->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = oxfw;
	snprintf(pcm->name, sizeof(pcm->name),
		 "%s PCM", oxfw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

end:
	return err;
}
