/*
 * receiver-pcm.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <sound/pcm_params.h>
#include "receiver.h"

static int pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwrx = substream->private_data;
	int index = substream->pcm->device;
	int err;

	err = snd_fwtxrx_stream_add_pcm_constraints(&fwrx->tx_stream[index],
						    substream->runtime);
	if (err >= 0)
		snd_pcm_set_sync(substream);

	return err;
}

static int pcm_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int pcm_capture_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_fwtx *fwrx = substream->private_data;
	int index = substream->pcm->device;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		mutex_lock(&fwrx->mutex);
		fwrx->capture_substreams[index]++;
		mutex_unlock(&fwrx->mutex);
	}

	amdtp_am824_set_pcm_format(&fwrx->tx_stream[index],
				   params_format(hw_params));

	return 0;
}

static int pcm_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwrx = substream->private_data;
	int index = substream->pcm->device;

	mutex_lock(&fwrx->mutex);

	if (substream->runtime->status->state != SNDRV_PCM_STATE_OPEN)
		fwrx->capture_substreams[index]--;

	snd_fwtx_stream_stop_simplex(fwrx, index);

	mutex_unlock(&fwrx->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwrx = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int index = substream->pcm->device;
	int err;

	mutex_lock(&fwrx->mutex);

	err = snd_fwtx_stream_start_simplex(fwrx, index, runtime->rate);
	if (err >= 0)
		amdtp_stream_pcm_prepare(&fwrx->tx_stream[index]);

	mutex_unlock(&fwrx->mutex);

	return err;
}

static int pcm_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_fwtx *fwrx = substream->private_data;
	int index = substream->pcm->device;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&fwrx->tx_stream[index], substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&fwrx->tx_stream[index], NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_fwtx *fwrx = substream->private_data;
	int index = substream->pcm->device;

	return amdtp_stream_pcm_pointer(&fwrx->tx_stream[index]);
}

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

int snd_fwtx_create_pcm_devices(struct snd_fwtx *fwrx)
{
	struct snd_pcm *pcm;
	int i, err;

	for (i = 0; i < OHCI1394_MIN_RX_CTX; i++) {
		/* PCM capture only. */
		err = snd_pcm_new(fwrx->card, fwrx->card->driver, i,
				  0, 1, &pcm);
		if (err < 0)
			return err;

		pcm->private_data = fwrx;
		snprintf(pcm->name, sizeof(pcm->name),
			 "%s %d PCM", fwrx->card->shortname, i);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
				&pcm_capture_ops);
	}

	return 0;
}
