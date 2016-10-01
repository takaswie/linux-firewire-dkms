/*
 * rx-pcm.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "rx.h"

static int pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwtx = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int index = substream->pcm->device;
	int err;

	runtime->hw.info = SNDRV_PCM_INFO_BATCH |
			   SNDRV_PCM_INFO_BLOCK_TRANSFER |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID;
	runtime->hw.formats = AM824_IN_PCM_FORMAT_BITS;

	runtime->hw.rates = SNDRV_PCM_RATE_32000 |
			    SNDRV_PCM_RATE_44100 |
			    SNDRV_PCM_RATE_48000 |
			    SNDRV_PCM_RATE_88200 |
			    SNDRV_PCM_RATE_96000 |
			    SNDRV_PCM_RATE_176400 |
			    SNDRV_PCM_RATE_192000;
	snd_pcm_limit_hw_rates(runtime);

	runtime->hw.channels_min = 2;
	runtime->hw.channels_max = 64;

	runtime->hw.periods_min = 2;
	runtime->hw.periods_max = UINT_MAX;
	runtime->hw.period_bytes_min = 4 * 64;
	runtime->hw.period_bytes_max = runtime->hw.period_bytes_min * 2048;
	runtime->hw.buffer_bytes_max = runtime->hw.period_bytes_max *
				       runtime->hw.periods_min;

	err = snd_fw_trx_stream_add_pcm_constraints(&fwtx->tx_stream[index],
						    runtime);
	if (err < 0)
		return err;

	snd_pcm_set_sync(substream);

	return 0;
}

static int pcm_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int pcm_capture_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_fwtx *fwtx = substream->private_data;
	int index = substream->pcm->device;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&fwtx->mutex);
		fwtx->capture_substreams[index]++;
		mutex_unlock(&fwtx->mutex);
	}

	amdtp_am824_set_pcm_format(&fwtx->tx_stream[index],
				   params_format(hw_params));

	return 0;
}

static int pcm_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwtx = substream->private_data;
	int index = substream->pcm->device;

	mutex_lock(&fwtx->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		fwtx->capture_substreams[index]--;

	snd_fwtx_stream_stop_simplex(fwtx, index);

	mutex_unlock(&fwtx->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwtx = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int index = substream->pcm->device;
	int err;

	mutex_lock(&fwtx->mutex);

	err = snd_fwtx_stream_start_simplex(fwtx, index, runtime->channels,
					    runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&fwtx->tx_stream[index]);

	mutex_unlock(&fwtx->mutex);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_fwtx *fwtx = substream->private_data;
	int index = substream->pcm->device;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&fwtx->tx_stream[index], substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&fwtx->tx_stream[index], NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwtx = substream->private_data;
	int index = substream->pcm->device;

	return amdtp_stream_pcm_pointer(&fwtx->tx_stream[index]);
}

int snd_fwtx_create_pcm_devices(struct snd_fwtx *fwtx)
{
	static struct snd_pcm_ops pcm_capture_ops = {
		.open		= pcm_capture_open,
		.close		= pcm_capture_close,
		.ioctl		= snd_pcm_lib_ioctl,
		.hw_params	= pcm_capture_hw_params,
		.hw_free	= pcm_capture_hw_free,
		.prepare	= pcm_capture_prepare,
		.trigger	= pcm_capture_trigger,
		.pointer	= pcm_capture_pointer,
		.page		= snd_pcm_lib_get_vmalloc_page,
	};
	struct snd_pcm *pcm;
	int i, err;

	for (i = 0; i < OHCI1394_MIN_RX_CTX; ++i) {
		/* PCM capture only. */
		err = snd_pcm_new(fwtx->card, fwtx->card->driver, i, 0, 1,
				  &pcm);
		if (err < 0)
			return err;

		pcm->private_data = fwtx;
		snprintf(pcm->name, sizeof(pcm->name),
			 "%s %d PCM", fwtx->card->shortname, i);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&pcm_capture_ops);
	}

	return 0;
}
