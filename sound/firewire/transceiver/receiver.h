/*
 * receiver.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "transceiver.h"

#define OHCI1394_MIN_RX_CTX	4

struct snd_fwtx {
	struct fw_unit *unit;

	bool registered;
	struct snd_card *card;
	struct delayed_work dwork;

	struct mutex mutex;
	spinlock_t lock;

	struct cmp_connection out_conn[OHCI1394_MIN_RX_CTX];
	struct amdtp_stream tx_stream[OHCI1394_MIN_RX_CTX];
	unsigned int capture_substreams[OHCI1394_MIN_RX_CTX];
};

int snd_fwtx_stream_init_simplex(struct snd_fwtx *fwtx);
void snd_fwtx_stream_destroy_simplex(struct snd_fwtx *fwtx);
void snd_fwtx_stream_update_simplex(struct snd_fwtx *fwtx);
int snd_fwtx_stream_start_simplex(struct snd_fwtx *fwtx, int index,
				  unsigned int rate);
void snd_fwtx_stream_stop_simplex(struct snd_fwtx *fwtx, int index);

int snd_fwtx_create_midi_devices(struct snd_fwtx *fwtx);
