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

static int amdtp_sfc_table[] = {
	[CIP_SFC_32000]	 = 32000,
	[CIP_SFC_44100]	 = 44100,
	[CIP_SFC_48000]	 = 48000,
	[CIP_SFC_88200]	 = 88200,
	[CIP_SFC_96000]	 = 96000,
	[CIP_SFC_176400] = 176400,
	[CIP_SFC_192000] = 192000};

int set_sampling_rate(struct fw_unit *unit, int rate,
		      int direction, unsigned short plug)
{
	int sfc;
	u8 *buf;
	int err;

	for (sfc = 0; sfc < ARRAY_SIZE(amdtp_sfc_table); sfc += 1)
		if (amdtp_sfc_table[sfc] == rate)
			break;

	buf = kmalloc(8, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}

	buf[0] = 0x00;		/* AV/C CONTROL */
	buf[1] = 0xff;		/* unit */
	if (direction > 0)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & plug;	/* plug */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0x00 | sfc;	/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x09) /* ACCEPTED */) {
		dev_err(&unit->device, "failed to set sampe rate\n");
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

int get_sampling_rate(struct fw_unit *unit, int *rate,
		      int direction, unsigned short plug)
{
	int sfc, evt;
	u8 *buf;
	int err;

	buf = kmalloc(8, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* unit */
	if (direction > 0)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & plug;	/* plug */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0xff;		/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x0c) /* IMPLEMENTED/STABLE */) {
		dev_err(&unit->device, "failed to get sampe rate\n");
		err = -EIO;
		goto end;
	}

	/* check EVT field */
	evt = (0x30 & buf[5]) >> 4;
	if (evt != 0) {	/* not AM824 */
		err = -EINVAL;
		goto end;
	}

	/* check sfc field */
	sfc = 0x07 & buf[5];
	if (sfc >= ARRAY_SIZE(amdtp_sfc_table)) {
		err = -EINVAL;
		goto end;
	}

	*rate = amdtp_sfc_table[sfc];
	err = 0;
end:
	kfree(buf);
	return err;
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
		.rates = bebob->supported_sampling_rates,
		.rate_min = 32000,
		.rate_max = 96000,
		.buffer_bytes_max = 1024 * 1024 * 1024,
		.period_bytes_min = 256,
		.period_bytes_max = 1024 * 1024 * 1024 / 2,
		.periods_min = 2,
		.periods_max = 32,
		.fifo_size = 0,
	};

	substream->runtime->hw = hardware;
	substream->runtime->delay = substream->runtime->hw.fifo_size;

	/* AM824 in IEC 61883-6 can deliver 24bit data */
	err = snd_pcm_hw_constraint_msbits(substream->runtime, 0, 32, 24);
	if (err < 0)
		return err;

	/* format of PCM samples is 16bit or 24bit inner 32bit */
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (err < 0)
		return err;
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (err < 0)
		return err;

	/* time for period constraint */
	err = snd_pcm_hw_constraint_minmax(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					500, UINT_MAX);
	if (err < 0)
		return err;

	return 0;
}

static int
pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_bebob *bebob = substream->private_data;
//	int sampling_rate;
	int err;

	/* common hardware information */
	err = pcm_init_hw_params(bebob, substream);
	if (err < 0)
		goto end;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		substream->runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
		substream->runtime->hw.channels_min = bebob->pcm_capture_channels;
		substream->runtime->hw.channels_max = bebob->pcm_capture_channels;
	} else {
		substream->runtime->hw.formats = AMDTP_OUT_PCM_FORMAT_BITS;
		substream->runtime->hw.channels_min = bebob->pcm_playback_channels;
		substream->runtime->hw.channels_max = bebob->pcm_playback_channels;
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
	struct snd_bebob *bebob = substream->private_data;
	struct amdtp_stream *stream;
	int midi_count;
	int err;

	/* keep PCM ring buffer */
	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
				params_buffer_bytes(hw_params));
	if (err < 0)
		goto end;

	/* set sampling rate if fw isochronous stream is not running */
//	if (!!IS_ERR(&bebob->transmit_stream.context) ||
//	    !!IS_ERR(&bebob->receive_stream.context)) {
//		err = snd_bebob_command_set_sampling_rate(bebob,
//					params_rate(hw_params));
//		if (err < 0)
//			return err;
//		snd_ctl_notify(bebob->card, SNDRV_CTL_EVENT_MASK_VALUE,
//					bebob->control_id_sampling_rate);
//	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		stream = &bebob->receive_stream;
		midi_count = bebob->midi_input_ports;
	} else {
		stream = &bebob->transmit_stream;
		midi_count = bebob->midi_output_ports;
	}

	/* set AMDTP parameters for transmit stream */
	amdtp_stream_set_rate(stream, params_rate(hw_params));
	amdtp_stream_set_pcm(stream, params_channels(hw_params));
	amdtp_stream_set_pcm_format(stream, params_format(hw_params));
	amdtp_stream_set_midi(stream, midi_count, 1);
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
