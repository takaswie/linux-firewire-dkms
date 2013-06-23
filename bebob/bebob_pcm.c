/*
 * bebob_pcm.c - driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */
#include "./bebob.h"

static int
hw_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
	     struct snd_bebob *bebob,
	     struct snd_bebob_stream_formation *formations)
{
	struct snd_interval *r =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
			hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		/* entry is invalid */
		if (formations[i].sampling_rate == 0)
			continue;

		if (!snd_interval_test(c, formations[i].pcm))
			continue;

		t.min = min(t.min, formations[i].sampling_rate);
		t.max = max(t.max, formations[i].sampling_rate);

	}
	return snd_interval_refine(r, &t);
}

static int
hw_rule_channels(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
		 struct snd_bebob *bebob,
		 struct snd_bebob_stream_formation *formations)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};

	int i;


	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		/* entry is invalid */
		if (formations[i].sampling_rate == 0)
			continue;

		if (!snd_interval_test(r, formations[i].sampling_rate))
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
	struct snd_bebob *bebob = rule->private;
	return hw_rule_rate(params, rule, bebob,
				bebob->receive_stream_formations);
}

static inline int
hw_rule_playback_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_bebob *bebob = rule->private;
	return hw_rule_rate(params, rule, bebob,
				bebob->transmit_stream_formations);
}

static inline int
hw_rule_capture_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_bebob *bebob = rule->private;
	return hw_rule_channels(params, rule, bebob,
				bebob->receive_stream_formations);
}

static inline int
hw_rule_playback_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_bebob *bebob = rule->private;
	return hw_rule_channels(params, rule, bebob,
				bebob->transmit_stream_formations);
}

static void
prepare_channels(struct snd_pcm_hardware *hw,
	  struct snd_bebob_stream_formation *formations)
{
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
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
	  struct snd_bebob_stream_formation *formations)
{
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		/* entry has no PCM channels */
		if (formations[i].pcm == 0)
			continue;

		hw->rate_min = min(hw->rate_min, formations[i].sampling_rate);
		hw->rate_max = max(hw->rate_max, formations[i].sampling_rate);
		hw->rates |=
			snd_pcm_rate_to_rate_bit(formations[i].sampling_rate);
	}

	return;
}

static int
pcm_init_hw_params(struct snd_bebob *bebob,
			struct snd_pcm_substream *substream)
{
	int err = 0;

	struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_SYNC_START |
			SNDRV_PCM_INFO_FIFO_IN_FRAMES |
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

	substream->runtime->hw = hardware;
	substream->runtime->delay = substream->runtime->hw.fifo_size;

	/* add rule between channels and sampling rate */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		prepare_rates(&substream->runtime->hw, bebob->receive_stream_formations);
		prepare_channels(&substream->runtime->hw, bebob->receive_stream_formations);
		substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_capture_channels, bebob,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_capture_rate, bebob,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	} else {
		prepare_rates(&substream->runtime->hw, bebob->transmit_stream_formations);
		prepare_channels(&substream->runtime->hw, bebob->transmit_stream_formations);
		substream->runtime->hw.formats = AMDTP_OUT_PCM_FORMAT_BITS;
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_playback_channels, bebob,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_playback_rate, bebob,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		goto end;

	/* format of PCM samples is 16bit or 24bit inner 32bit */
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

	err = 0;

end:
	return err;
}

static int
pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	int err;

	err = pcm_init_hw_params(bebob, substream);
	if (err < 0)
		goto end;

	snd_pcm_set_sync(substream);

end:
	return err;
}

static int
pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int
pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	struct snd_bebob *bebob = substream->private_data;
	struct amdtp_stream *stream;
	struct snd_bebob_stream_formation *formation;
	int index;
	int direction;
	int err;

	/* keep PCM ring buffer */
	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
				params_buffer_bytes(hw_params));
	if (err < 0)
		goto end;

	index = snd_bebob_get_formation_index(params_rate(hw_params));
	if (index < 0) {
		err = -EIO;
		goto end;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		stream = &bebob->receive_stream;
		formation = bebob->receive_stream_formations + index;
		direction = 0;
	} else {
		stream = &bebob->transmit_stream;
		formation = bebob->transmit_stream_formations + index;
		direction = 1;
	}

	/* set sampling rate if fw isochronous stream is not running */
	err = avc_generic_set_sampling_rate(bebob->unit, formation->sampling_rate, direction, 0);
	if (err < 0)
		return err;
//	snd_ctl_notify(bebob->card, SNDRV_CTL_EVENT_MASK_VALUE,
//				bebob->control_id_sampling_rate);

	/* set AMDTP parameters for transmit stream */
	amdtp_stream_set_rate(stream, params_rate(hw_params));
	amdtp_stream_set_pcm(stream, params_channels(hw_params));
	amdtp_stream_set_pcm_format(stream, params_format(hw_params));

end:
	return err;
}

static int
pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	struct amdtp_stream *stream;
	struct cmp_connection *connection;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		stream = &bebob->receive_stream;
		connection = &bebob->input_connection;
	} else {
		stream = &bebob->transmit_stream;
		connection = &bebob->output_connection;
	}

	/* stop fw isochronous stream of AMDTP with CMP */
	snd_bebob_stream_stop(bebob, stream);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int
pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
	struct amdtp_stream *stream;
	int err = 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		stream = &bebob->receive_stream;
	else
		stream = &bebob->transmit_stream;

	/* start stream */
	err = snd_bebob_stream_start(bebob, stream);
	if (err < 0)
		goto end;

	/* initialize buffer pointer */
	amdtp_stream_pcm_prepare(stream);

end:
	return err;
}

static int
pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_bebob *bebob = substream->private_data;
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

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		amdtp_stream_pcm_trigger(&bebob->receive_stream, pcm);
	else
		amdtp_stream_pcm_trigger(&bebob->transmit_stream, pcm);

	return 0;
}

static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return amdtp_stream_pcm_pointer(&bebob->receive_stream);
	else
		return amdtp_stream_pcm_pointer(&bebob->transmit_stream);
}

static struct snd_pcm_ops pcm_playback_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_prepare,
	.trigger	= pcm_trigger,
	.pointer	= pcm_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
	.mmap		= snd_pcm_lib_mmap_vmalloc,
};

static struct snd_pcm_ops pcm_capture_ops = {
	.open		= pcm_open,
	.close		= pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_hw_params,
	.hw_free	= pcm_hw_free,
	.prepare	= pcm_prepare,
	.trigger	= pcm_trigger,
	.pointer	= pcm_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
};

int snd_bebob_create_pcm_devices(struct snd_bebob *bebob)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(bebob->card, bebob->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = bebob;
	snprintf(pcm->name, sizeof(pcm->name), "%s PCM", bebob->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

	/* for host transmit and target input */
	err = snd_bebob_stream_init(bebob, &bebob->transmit_stream);
	if (err < 0)
		goto end;

	/* for host receive and target output */
	err = snd_bebob_stream_init(bebob, &bebob->receive_stream);
	if (err < 0) {
		snd_bebob_stream_destroy(bebob, &bebob->transmit_stream);
		goto end;
	}

end:
	return err;
}

void snd_bebob_destroy_pcm_devices(struct snd_bebob *bebob)
{
	amdtp_stream_pcm_abort(&bebob->receive_stream);
	amdtp_stream_stop(&bebob->receive_stream);
	snd_bebob_stream_destroy(bebob, &bebob->receive_stream);

	amdtp_stream_pcm_abort(&bebob->transmit_stream);
	amdtp_stream_stop(&bebob->transmit_stream);
	snd_bebob_stream_destroy(bebob, &bebob->transmit_stream);

	return;
}
