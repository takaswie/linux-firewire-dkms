/*
 * tx-stream.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"

#define CALLBACK_TIMEOUT	100

int fw_am_unit_stream_init(struct fw_am_unit *am)
{
	unsigned int i, err;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		err = amdtp_am824_init(&am->opcr[i].stream, am->unit,
				       AMDTP_OUT_STREAM, CIP_BLOCKING);
		if (err < 0)
			break;
	}

	return err;
}

void fw_am_unit_stream_update(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		amdtp_stream_update(&am->opcr[i].stream);
}

void fw_am_unit_stream_destroy(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		amdtp_stream_destroy(&am->opcr[i].stream);
}

int fw_am_unit_stream_start(struct fw_am_unit *am, unsigned int index,
			    unsigned int isoc_ch, unsigned int speed)
{
	struct pcr_resource *pcr = &am->opcr[index];
	int err;

	err = amdtp_am824_set_parameters(&pcr->stream, pcr->pcm_channels,
					 pcr->rate, 8, false);
	if (err < 0)
		return err;

	/* I believe that all of parameters are already set. */
	err = amdtp_stream_start(&pcr->stream, isoc_ch, speed);
	if (err < 0)
		return err;

	/* This returns immediately. */
	if (!amdtp_stream_wait_callback(&pcr->stream, CALLBACK_TIMEOUT) < 0) {
		amdtp_stream_stop(&pcr->stream);
		err = -ETIMEDOUT;
	}

	return err;
}

void fw_am_unit_stream_stop(struct fw_am_unit *am, unsigned int index)
{
	amdtp_stream_pcm_abort(&am->opcr[index].stream);
	amdtp_stream_stop(&am->opcr[index].stream);
}
