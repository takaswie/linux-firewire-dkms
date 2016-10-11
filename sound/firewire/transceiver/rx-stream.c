/*
 * rx-stream.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "rx.h"
#include "../fcp.h"

#define CALLBACK_TIMEOUT	100

int snd_fwtx_stream_start_simplex(struct snd_fwtx *fwtx, int index,
				  unsigned int pcm_channels, unsigned int rate)
{
	u8 format[9];
	struct avc_stream_formation formation;
	unsigned int i, len;
	int err;

	if (fwtx->capture_substreams[index] == 0)
		return 0;

	/* Get current stream format information. */
	len = sizeof(format);
	err = avc_stream_get_format_single(fwtx->unit, AVC_GENERAL_PLUG_DIR_OUT,
					   index, format, &len);
	if (err < 0)
		return err;
	err = avc_stream_parse_format(format, &formation);
	if (err < 0)
		return err;

	/* Supplement to stream format information. */
	if (pcm_channels == 0)
		pcm_channels = formation.pcm;
	if (rate == 0)
		rate = formation.rate;

	/* Construct stream format information. */
	for (i = 0; i < ARRAY_SIZE(avc_stream_rate_table); ++i) {
		if (avc_stream_rate_table[i] == rate)
			break;
	}
	if (i == ARRAY_SIZE(avc_stream_rate_table))
		return -EINVAL;

	format[0] = 0x90;	/* Audio and Music root. */
	format[1] = 0x40;	/* Compound AM824 level. */
	format[2] = avc_stream_rate_codes[i];	/* Sampling frequency. */
	format[3] = 0x02;	/* Unsupport Command-based rate control. */
	format[4] = 0x02;	/* Two stream formats. */
	format[5] = pcm_channels;
	format[6] = 0x06;	/* Multi bit linear audio data channel. */
	format[7] = 0x01;
	format[8] = 0x0d;	/* MIDI conformant data channel. */
	err = avc_stream_set_format(fwtx->unit, AVC_GENERAL_PLUG_DIR_OUT, index,
				    format, sizeof(format));
	if (err < 0)
		return err;

	err = amdtp_am824_set_parameters(&fwtx->tx_stream[index],
					 rate, pcm_channels, rate, false);
	if (err < 0)
		return err;

	err = cmp_connection_establish(&fwtx->out_conn[index],
			amdtp_stream_get_max_payload(&fwtx->tx_stream[index]));
	if (err < 0)
		return err;

	err = amdtp_stream_start(&fwtx->tx_stream[index],
				 fwtx->out_conn[index].resources.channel,
				 fwtx->out_conn[index].speed);
	if (err < 0) {
		cmp_connection_break(&fwtx->out_conn[index]);
		return err;
	}

	if (!amdtp_stream_wait_callback(&fwtx->tx_stream[index],
							CALLBACK_TIMEOUT)) {
		amdtp_stream_stop(&fwtx->tx_stream[index]);
		cmp_connection_break(&fwtx->out_conn[index]);
		err = -ETIMEDOUT;
	}

	return err;
}

void snd_fwtx_stream_stop_simplex(struct snd_fwtx *fwtx, int index)
{
	if (fwtx->capture_substreams[index] > 0)
		return;

	amdtp_stream_pcm_abort(&fwtx->tx_stream[index]);
	amdtp_stream_stop(&fwtx->tx_stream[index]);

	cmp_connection_break(&fwtx->out_conn[index]);
}

int snd_fwtx_stream_init_simplex(struct snd_fwtx *fwtx)
{
	int i, err;

	/* TODO: error handling. */
	for (i = 0; i < OHCI1394_MIN_RX_CTX; i++) {
		err = cmp_connection_init(&fwtx->out_conn[i], fwtx->unit,
					  CMP_OUTPUT, i);
		if (err < 0)
			return err;

		err = amdtp_am824_init(&fwtx->tx_stream[i], fwtx->unit,
				       AMDTP_IN_STREAM, CIP_BLOCKING);
		if (err < 0) {
			cmp_connection_destroy(&fwtx->out_conn[i]);
			break;
		}
	}

	return err;
}

void snd_fwtx_stream_update_simplex(struct snd_fwtx *fwtx)
{
	int i;

	for (i = 0; i < OHCI1394_MIN_RX_CTX; i++) {
		/* No need to update. */
		if (fwtx->capture_substreams[i] < 0)
			continue;

		if (cmp_connection_update(&fwtx->out_conn[i]) < 0) {
			amdtp_stream_pcm_abort(&fwtx->tx_stream[i]);
			mutex_lock(&fwtx->mutex);
			amdtp_stream_stop(&fwtx->tx_stream[i]);
			mutex_unlock(&fwtx->mutex);
		}
	}
}

void snd_fwtx_stream_destroy_simplex(struct snd_fwtx *fwtx)
{
	int i;

	for (i = 0; i < OHCI1394_MIN_RX_CTX; i++) {
		cmp_connection_destroy(&fwtx->out_conn[i]);
		amdtp_stream_destroy(&fwtx->tx_stream[i]);
	}
}
