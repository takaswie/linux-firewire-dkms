/*
 * tx.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_TRANSMITTER_H_INCLUDED
#define SOUND_FIREWIRE_TRANSMITTER_H_INCLUDED

#include "trx.h"

#define OHCI1394_MIN_TX_CTX	4

struct pcr_resource {
	u32 reg;
	unsigned int pcm_channels;
	unsigned int rate;
	struct amdtp_stream stream;
};

struct fw_am_unit {
	struct fw_unit *unit;

	struct mutex mutex;
	spinlock_t lock;

	struct snd_card *card;

	u32 ompr;
	struct pcr_resource opcr[OHCI1394_MIN_TX_CTX];
	struct fw_address_handler cmp_handler;

	struct fw_address_handler fcp_handler;
	void *transactions;
	struct mutex transactions_mutex;
	struct work_struct fcp_work;
};

int fw_am_unit_stream_init(struct fw_am_unit *am);
void fw_am_unit_stream_destroy(struct fw_am_unit *am);
int fw_am_unit_stream_start(struct fw_am_unit *am, unsigned int index,
			    unsigned int isoc_ch, unsigned int speed);
void fw_am_unit_stream_update(struct fw_am_unit *am);
void fw_am_unit_stream_stop(struct fw_am_unit *am, unsigned int index);

int fw_am_unit_cmp_init(struct fw_am_unit *am);
void fw_am_unit_cmp_update(struct fw_am_unit *am);
void fw_am_unit_cmp_destroy(struct fw_am_unit *am);

int fw_am_unit_fcp_init(struct fw_am_unit *am);
void fw_am_unit_fcp_update(struct fw_am_unit *am);
void fw_am_unit_fcp_destroy(struct fw_am_unit *am);

int fw_am_unit_create_midi_devices(struct fw_am_unit *unit);
int fw_am_unit_create_pcm_devices(struct fw_am_unit *unit);

#endif
