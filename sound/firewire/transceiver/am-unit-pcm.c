/*
 * am-unit-pcm.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"
#include <sound/pcm_params.h>

static int pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;
	unsigned int rate;
	int err;

	err = snd_fwtxrx_stream_add_pcm_constraints(&am->tx_streams[index],
						    substream->runtime);
	if (err >= 0)
		snd_pcm_set_sync(substream);

	if (amdtp_stream_running(&am->tx_streams[index])) {
		/* TODO: PCM channels */
		rate = amdtp_rate_table[am->tx_streams[index].sfc];
		substream->runtime->hw.rate_min = rate;
		substream->runtime->hw.rate_max = rate;
	}

	return err;
}

static int pcm_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int pcm_playback_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;
	int err;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;

	amdtp_am824_set_pcm_format(&am->tx_streams[index],
				   params_format(hw_params));

	return 0;
}

static int pcm_playback_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;

	amdtp_stream_pcm_prepare(&am->tx_streams[index]);

	return 0;
}

static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&am->tx_streams[index], substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&am->tx_streams[index], NULL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t pcm_playback_pointer(struct snd_pcm_substream *substream)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;

	return amdtp_stream_pcm_pointer(&am->tx_streams[index]);
}

static struct snd_pcm_ops pcm_playback_ops = {
	.open		= pcm_playback_open,
	.close		= pcm_playback_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pcm_playback_hw_params,
	.hw_free	= pcm_playback_hw_free,
	.prepare	= pcm_playback_prepare,
	.trigger	= pcm_playback_trigger,
	.pointer	= pcm_playback_pointer,
	.page		= snd_pcm_lib_get_vmalloc_page,
};

int fw_am_unit_create_pcm_devices(struct fw_am_unit *am)
{
	struct snd_pcm *pcm;
	int i, err;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		/* PCM playback only. */
		err = snd_pcm_new(am->card, am->card->driver, i,
				  1, 0, &pcm);
		if (err < 0)
			return err;

		pcm->private_data = am;
		snprintf(pcm->name, sizeof(pcm->name),
			 "%s %d PCM", am->card->shortname, i + 1);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&pcm_playback_ops);
	}

	return 0;
}
