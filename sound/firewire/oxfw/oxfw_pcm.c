/*
 * oxfw_pcm.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

static int hw_rule_rate(struct snd_pcm_hw_params *params,
			struct snd_pcm_hw_rule *rule,
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

	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
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

static int hw_rule_channels(struct snd_pcm_hw_params *params,
			    struct snd_pcm_hw_rule *rule,
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

	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
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

static inline int hw_rule_playback_rate(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_rate(params, rule, oxfw,
			    oxfw->rx_stream_formations);
}

static inline int hw_rule_playback_channels(struct snd_pcm_hw_params *params,
					    struct snd_pcm_hw_rule *rule)
{
	struct snd_oxfw *oxfw = rule->private;
	return hw_rule_channels(params, rule, oxfw,
				oxfw->rx_stream_formations);
}

static void prepare_channels(struct snd_pcm_hardware *hw,
			     struct snd_oxfw_stream_formation *formations)
{
	unsigned int i;

	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->channels_min = min(hw->channels_min, formations[i].pcm);
		hw->channels_max = max(hw->channels_max, formations[i].pcm);
	}

	return;
}

static void prepare_rates(struct snd_pcm_hardware *hw,
			  struct snd_oxfw_stream_formation *formations)
{
	unsigned int i;

	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->rate_min = min(hw->rate_min, snd_oxfw_rate_table[i]);
		hw->rate_max = max(hw->rate_max, snd_oxfw_rate_table[i]);
		hw->rates |= snd_pcm_rate_to_rate_bit(snd_oxfw_rate_table[i]);
	}

	return;
}

static int oxfw_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hw = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
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
		.buffer_bytes_max = 4 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int rate;
	int err;

	runtime->hw = hw;

	/* add rule between channels and sampling rate */
	prepare_rates(&runtime->hw, oxfw->rx_stream_formations);
	prepare_channels(&runtime->hw, oxfw->rx_stream_formations);
	runtime->hw.formats = AMDTP_OUT_PCM_FORMAT_BITS;
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			    hw_rule_playback_channels, oxfw,
			    SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			    hw_rule_playback_rate, oxfw,
			    SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		goto end;

	/*
	 * AMDTP functionality in firewire-lib require periods to be aligned to
	 * 16 bit, or 24bit inner 32bit.
	 */
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (err < 0)
		goto end;

	/* time for period constraint */
	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   5000, UINT_MAX);

	/*
	 * When any PCM stream are already running, the available sampling rate
	 *  is limited at current value.
	 */
	if (amdtp_stream_pcm_running(&oxfw->rx_stream)) {
		err = avc_general_get_sig_fmt(oxfw->unit, &rate,
					      AVC_GENERAL_PLUG_DIR_IN, 0);
		if (err < 0)
			goto end;
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;
	}

	snd_pcm_set_sync(substream);
end:
	return err;
}

static int oxfw_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int oxfw_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *hw_params)
{
	struct snd_oxfw *oxfw = substream->private_data;
	int err;

	mutex_lock(&oxfw->mutex);
	snd_oxfw_stream_stop(oxfw);
	mutex_unlock(&oxfw->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		goto error;

	amdtp_stream_set_parameters(&oxfw->rx_stream,
				    params_rate(hw_params),
				    params_channels(hw_params),
				    0);

	amdtp_stream_set_pcm_format(&oxfw->rx_stream,
				    params_format(hw_params));

	err = avc_general_set_sig_fmt(oxfw->unit, params_rate(hw_params),
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0)
		goto err_buffer;
	if (err != 0x09 /* ACCEPTED */) {
		dev_err(&oxfw->unit->device, "failed to set sample rate\n");
		err = -EIO;
		goto error;
	}

	return 0;

err_buffer:
	snd_pcm_lib_free_vmalloc_buffer(substream);
error:
	return err;
}

static int oxfw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	mutex_lock(&oxfw->mutex);
	snd_oxfw_stream_stop(oxfw);
	mutex_unlock(&oxfw->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int oxfw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;
	int err;

	mutex_lock(&oxfw->mutex);

	snd_oxfw_stream_stop(oxfw);

	err = snd_oxfw_stream_start(oxfw);
	if (err < 0)
		goto end;

	amdtp_stream_pcm_prepare(&oxfw->rx_stream);
end:
	mutex_unlock(&oxfw->mutex);
	return err;
}

static int oxfw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_oxfw *oxfw = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_stream_pcm_trigger(&oxfw->rx_stream, pcm);
	return 0;
}

static snd_pcm_uframes_t oxfw_pointer(struct snd_pcm_substream *substream)
{
	struct snd_oxfw *oxfw = substream->private_data;

	return amdtp_stream_pcm_pointer(&oxfw->rx_stream);
}

int snd_oxfw_create_pcm(struct snd_oxfw *oxfw)
{
	static struct snd_pcm_ops ops = {
		.open      = oxfw_open,
		.close     = oxfw_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = oxfw_hw_params,
		.hw_free   = oxfw_hw_free,
		.prepare   = oxfw_prepare,
		.trigger   = oxfw_trigger,
		.pointer   = oxfw_pointer,
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(oxfw->card, oxfw->card->driver, 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = oxfw;
	strcpy(pcm->name, oxfw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &ops);
	return 0;
}
