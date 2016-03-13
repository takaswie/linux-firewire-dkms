/*
 * am-unit-stream.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"

#define CALLBACK_TIMEOUT	100

int fw_am_unit_stream_init(struct fw_am_unit *am)
{
	unsigned int i, err;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		err = amdtp_am824_init(&am->tx_streams[i], am->unit,
				       AMDTP_OUT_STREAM, CIP_BLOCKING);
		if (err < 0)
			break;

		/* TODO: configurable. */
		err = amdtp_am824_set_parameters(&am->tx_streams[i], 44100,
						 2, 8, false);
		if (err < 0)
			break;
	}

	return err;
}

void fw_am_unit_stream_update(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		amdtp_stream_update(&am->tx_streams[i]);
}

void fw_am_unit_stream_destroy(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		amdtp_stream_destroy(&am->tx_streams[i]);
}

int fw_am_unit_stream_start(struct fw_am_unit *am, unsigned int index,
			    unsigned int isoc_ch, unsigned int speed)
{
	int err;

	/* I believe that all of parameters are already set. */
	err = amdtp_stream_start(&am->tx_streams[index], isoc_ch, speed);
	if (err < 0)
		return err;

	/* This returns immediately. */
	if (!amdtp_stream_wait_callback(&am->tx_streams[index],
					CALLBACK_TIMEOUT) < 0) {
		amdtp_stream_stop(&am->tx_streams[index]);
		err = -ETIMEDOUT;
	}

	return err;
}

void fw_am_unit_stream_stop(struct fw_am_unit *am, unsigned int index)
{
	amdtp_stream_pcm_abort(&am->tx_streams[index]);
	amdtp_stream_stop(&am->tx_streams[index]);
}
