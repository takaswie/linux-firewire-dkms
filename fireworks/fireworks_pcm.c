/*
 * fireworks_pcm.c - driver for Firewire devices from Echo Digital Audio
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
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
#include "./fireworks.h"

/*
 * NOTE:
 * Fireworks reduces its PCM channels according to its sampling rate.
 * There are three modes.
 *  0:  32.0- 48.0 kHz then nb_1394_XXX_channels    applied
 *  1:  88.2- 96.0 kHz then nb_1394_XXX_channels_2x applied
 *  2: 176.4-192.0 kHz then nb_1394_XXX_channels_4x applied
 *
 * Then the number of PCM channels for analog input and output are always fixed
 * but the number of PCM channels for digital input and output are differed.
 *
 * Additionally, according to AudioFire Owner's Manual Version 2.2,
 * the number of PCM channels for digital input has more restriction
 *  with digital mode.
 *  - ADAT optical with 32.0-48.0 kHz	: use inputs 1-8
 *  - ADAT coaxial with 88.2-96.0 kHz	: use inputs 1-4
 *  - S/PDIF coaxial and optical	: use inputs 1-2
 *
 * Currently this module doesn't have rules for the latter.
 */
static unsigned int freq_table[] = {
	/* multiplier mode 0 */
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	/* multiplier mode 1 */
	[3] = 88200,
	[4] = 96000,
	/* multiplier mode 2 */
	[5] = 176400,
	[6] = 192000,
};

static int
get_multiplier_mode(int index)
{
	return ((int)index - 1) / 2;
}

static int
hw_rule_rate(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
			struct snd_efw_t *efw, unsigned int *channels_sets)
{
	struct snd_interval *r =
			hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	const struct snd_interval *c =
			hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};
	unsigned int rate_bit;
	int i, mode;

	for (i = 0; i < ARRAY_SIZE(freq_table); i += 1) {
		/* skip unsupported sampling rate */
		rate_bit = snd_pcm_rate_to_rate_bit(freq_table[i]);
		if (!(efw->supported_sampling_rate & rate_bit))
			continue;

		mode = get_multiplier_mode(i);
		if (!snd_interval_test(c, channels_sets[mode]))
			continue;
		t.min = min(t.min, freq_table[i]);
		t.max = max(t.max, freq_table[i]);

	}

	return snd_interval_refine(r, &t);
}

static int
hw_rule_channels(struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule,
			struct snd_efw_t *efw, unsigned int *channels_sets)
{
	struct snd_interval *c =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	const struct snd_interval *r =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval t = {
		.min = UINT_MAX, .max = 0, .integer = 1
	};

	unsigned int rate_bit;
	int i, mode;

	for (i = 0; i < ARRAY_SIZE(freq_table); i += 1) {
		/* skip unsupported sampling rate */
		rate_bit = snd_pcm_rate_to_rate_bit(freq_table[i]);
		if (!(efw->supported_sampling_rate & rate_bit))
			continue;

		mode = get_multiplier_mode(i);
		if (!snd_interval_test(r, freq_table[i]))
			continue;

		t.min = min(t.min, channels_sets[mode]);
		t.max = max(t.max, channels_sets[mode]);

	}

	return snd_interval_refine(c, &t);
}

static inline int
hw_rule_capture_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_efw_t *efw = rule->private;
	return hw_rule_rate(params, rule, efw,
				efw->pcm_capture_channels_sets);
}

static inline int
hw_rule_playback_rate(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_efw_t *efw = rule->private;
	return hw_rule_rate(params, rule, efw,
				efw->pcm_playback_channels_sets);
}

static inline int
hw_rule_capture_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_efw_t *efw = rule->private;
	return hw_rule_channels(params, rule, efw,
				efw->pcm_capture_channels_sets);
}

static inline int
hw_rule_playback_channels(struct snd_pcm_hw_params *params,
				struct snd_pcm_hw_rule *rule)
{
	struct snd_efw_t *efw = rule->private;
	return hw_rule_channels(params, rule, efw,
				efw->pcm_playback_channels_sets);
}

static int
pcm_init_hw_params(struct snd_efw_t *efw,
			struct snd_pcm_substream *substream)
{
	int err = 0;

	struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_SYNC_START |
			SNDRV_PCM_INFO_FIFO_IN_FRAMES,
		.formats = AMDTP_OUT_PCM_FORMAT_BITS,
		.rates = efw->supported_sampling_rate,
		.rate_min = 22050,
		.rate_max = 192000,
		.buffer_bytes_max = 32 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
		.fifo_size = 8,
	};

	substream->runtime->hw = hardware;
	substream->runtime->delay = substream->runtime->hw.fifo_size;

	err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					500, 8192000);
	if (err < 0)
		return err;

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		return err;

	/* add rule between channels and sampling rate */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_capture_channels, efw,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_capture_rate, efw,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	} else {
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_rule_playback_channels, efw,
				SNDRV_PCM_HW_PARAM_RATE, -1);
		snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				hw_rule_playback_rate, efw,
				SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	}

	return 0;
}

static int
pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_efw_t *efw = substream->private_data;
	int sampling_rate;
	int err;

	/* common hardware information */
	err = pcm_init_hw_params(efw, substream);
	if (err < 0)
		goto end;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		substream->runtime->hw.formats |= SNDRV_PCM_FMTBIT_S24_BE;
		substream->runtime->hw.channels_min = efw->pcm_capture_channels;
		substream->runtime->hw.channels_max = efw->pcm_capture_channels;
	} else {
		substream->runtime->hw.channels_min = efw->pcm_playback_channels;
		substream->runtime->hw.channels_max = efw->pcm_playback_channels;
	}

	/* the same sampling rate must be used for transmit and receive stream */
	if (efw->transmit_stream.pcm || efw->transmit_stream.midi ||
	    efw->receive_stream.pcm || efw->receive_stream.midi) {
		err = snd_efw_command_get_sampling_rate(efw, &sampling_rate);
		if (err < 0)
			goto end;
		substream->runtime->hw.rate_min = sampling_rate;
		substream->runtime->hw.rate_max = sampling_rate;
	}

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
	struct snd_efw_t *efw = substream->private_data;
	struct snd_efw_stream_t *stream;
	int midi_count;
	int err;

	/* keep PCM ring buffer */
	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
				params_buffer_bytes(hw_params));
	if (err < 0)
		goto end;

	/* set sampling rate if fw isochronous stream is not running */
	if (!!IS_ERR(efw->transmit_stream.strm.context) ||
	    !!IS_ERR(efw->receive_stream.strm.context)) {
		err = snd_efw_command_set_sampling_rate(efw,
					params_rate(hw_params));
		if (err < 0)
			return err;
		snd_ctl_notify(efw->card, SNDRV_CTL_EVENT_MASK_VALUE,
					efw->control_id_sampling_rate);
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		stream = &efw->receive_stream;
		midi_count = efw->midi_input_count;
	} else {
		stream = &efw->transmit_stream;
		midi_count = efw->midi_output_count;
	}

	/* set AMDTP parameters for transmit stream */
	amdtp_out_stream_set_rate(&stream->strm, params_rate(hw_params));
	amdtp_out_stream_set_pcm(&stream->strm, params_channels(hw_params));
	amdtp_out_stream_set_pcm_format(&stream->strm, params_format(hw_params));
	amdtp_out_stream_set_midi(&stream->strm, midi_count);
end:
	return err;
}

static int
pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_efw_t *efw = substream->private_data;
	struct snd_efw_stream_t *stream;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		stream = &efw->receive_stream;
	else
		stream = &efw->transmit_stream;

	/* stop fw isochronous stream of AMDTP with CMP */
	snd_efw_stream_stop(stream);
	stream->pcm = false;

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int
pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_efw_t *efw = substream->private_data;
	struct snd_efw_stream_t *stream;
	int err = 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		stream = &efw->receive_stream;
	else
		stream = &efw->transmit_stream;

	/* start stream */
	err = snd_efw_stream_start(stream);
	if (err < 0)
		goto end;
	stream->pcm = true;

	/* initialize buffer pointer */
	amdtp_out_stream_pcm_prepare(&stream->strm);

end:
	return err;
}

static int
pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_efw_t *efw = substream->private_data;
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
		amdtp_out_stream_pcm_trigger(&efw->receive_stream.strm, pcm);
	else
		amdtp_out_stream_pcm_trigger(&efw->transmit_stream.strm, pcm);

	return 0;
}

static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_efw_t *efw = substream->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return amdtp_out_stream_pcm_pointer(&efw->receive_stream.strm);
	else
		return amdtp_out_stream_pcm_pointer(&efw->transmit_stream.strm);
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

int snd_efw_create_pcm_devices(struct snd_efw_t *efw)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(efw->card, efw->card->driver, 0, 1, 1, &pcm);
	if (err < 0)
		goto end;

	pcm->private_data = efw;
	snprintf(pcm->name, sizeof(pcm->name), "%s PCM", efw->card->shortname);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

	/* for host transmit and target input */
	err = snd_efw_stream_init(efw, &efw->transmit_stream);
	if (err < 0)
		goto end;

	/* for host receive and target output */
	err = snd_efw_stream_init(efw, &efw->receive_stream);
	if (err < 0) {
		snd_efw_stream_destroy(&efw->transmit_stream);
		goto end;
	}

end:
	return err;
}

void snd_efw_destroy_pcm_devices(struct snd_efw_t *efw)
{
	amdtp_out_stream_pcm_abort(&efw->transmit_stream.strm);
	amdtp_out_stream_stop(&efw->transmit_stream.strm);
	snd_efw_stream_destroy(&efw->transmit_stream);

	amdtp_out_stream_pcm_abort(&efw->receive_stream.strm);
	amdtp_out_stream_stop(&efw->receive_stream.strm);
	snd_efw_stream_destroy(&efw->receive_stream);

	return;
}
