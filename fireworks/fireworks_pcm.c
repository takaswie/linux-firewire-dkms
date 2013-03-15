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

	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		return err;

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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		substream->runtime->hw.channels_min = efw->pcm_playback_channels;
		substream->runtime->hw.channels_max = efw->pcm_playback_channels;
	} else {
		substream->runtime->hw.formats |= SNDRV_PCM_FMTBIT_S24_BE;
		substream->runtime->hw.channels_min = efw->pcm_capture_channels;
		substream->runtime->hw.channels_max = efw->pcm_capture_channels;
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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		stream = &efw->transmit_stream;
		midi_count = efw->midi_output_count;
	} else {
		stream = &efw->receive_stream;
		midi_count = efw->midi_input_count;
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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stream = &efw->transmit_stream;
	else
		stream = &efw->receive_stream;

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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		stream = &efw->transmit_stream;
	else
		stream = &efw->receive_stream;

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

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		amdtp_out_stream_pcm_trigger(&efw->transmit_stream.strm, pcm);
	else
		amdtp_out_stream_pcm_trigger(&efw->receive_stream.strm, pcm);

	return 0;
}

static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_efw_t *efw = substream->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return amdtp_out_stream_pcm_pointer(&efw->transmit_stream.strm);
	else
		return amdtp_out_stream_pcm_pointer(&efw->receive_stream.strm);
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
