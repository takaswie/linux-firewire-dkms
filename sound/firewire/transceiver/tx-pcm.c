/*
 * tx-pcm.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"

static int pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct fw_am_unit *am = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int index = substream->pcm->device;
	int err;

	/* When peers start packet streaming. */
	if (amdtp_stream_running(&am->opcr[index].stream))
		return -EIO;

	runtime->hw.info = SNDRV_PCM_INFO_BATCH |
			   SNDRV_PCM_INFO_BLOCK_TRANSFER |
			   SNDRV_PCM_INFO_INTERLEAVED |
			   SNDRV_PCM_INFO_MMAP |
			   SNDRV_PCM_INFO_MMAP_VALID;
	runtime->hw.formats = AM824_OUT_PCM_FORMAT_BITS;

	runtime->hw.rates = SNDRV_PCM_RATE_32000 |
			    SNDRV_PCM_RATE_44100 |
			    SNDRV_PCM_RATE_48000 |
			    SNDRV_PCM_RATE_88200 |
			    SNDRV_PCM_RATE_96000 |
			    SNDRV_PCM_RATE_176400 |
			    SNDRV_PCM_RATE_192000;
	snd_pcm_limit_hw_rates(runtime);

	runtime->hw.rate_min = am->opcr[index].rate;
	runtime->hw.rate_max = am->opcr[index].rate;

	runtime->hw.channels_min = am->opcr[index].pcm_channels;
	runtime->hw.channels_max = am->opcr[index].pcm_channels;

	runtime->hw.periods_min = 2;
	runtime->hw.periods_max = UINT_MAX;
	runtime->hw.period_bytes_min = 4 * 64;
	runtime->hw.period_bytes_max = runtime->hw.period_bytes_min * 2048;
	runtime->hw.buffer_bytes_max = runtime->hw.period_bytes_max *
				       runtime->hw.periods_min;

	err = snd_fw_trx_stream_add_pcm_constraints(&am->opcr[index].stream,
						    substream->runtime);
	if (err < 0)
		return err;

	snd_pcm_set_sync(substream);

	return 0;
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

	if (amdtp_stream_running(&am->opcr[index].stream))
		return -EIO;

	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

static int pcm_playback_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;

	if (amdtp_stream_running(&am->opcr[index].stream))
		return -EIO;

	amdtp_stream_pcm_prepare(&am->opcr[index].stream);

	return 0;
}

static int pcm_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct fw_am_unit *am = substream->private_data;
	unsigned int index = substream->pcm->device;

	if (amdtp_stream_running(&am->opcr[index].stream))
		return -EIO;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		amdtp_stream_pcm_trigger(&am->opcr[index].stream, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		amdtp_stream_pcm_trigger(&am->opcr[index].stream, NULL);
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

	return amdtp_stream_pcm_pointer(&am->opcr[index].stream);
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
