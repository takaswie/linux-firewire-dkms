/*
 * am-unit.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "transceiver.h"

#define OHCI1394_MIN_TX_CTX	4

struct fw_am_unit {
	struct fw_unit *unit;

	struct mutex mutex;
	spinlock_t lock;

	bool registered;
	struct snd_card *card;
	struct delayed_work dwork;

	struct amdtp_stream tx_streams[OHCI1394_MIN_TX_CTX];

	struct list_head list_for_cmp;
	u32 ompr;
	u32 opcr[OHCI1394_MIN_TX_CTX];

	struct list_head list_for_fcp;
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

int fw_am_unit_cmp_register(struct fw_am_unit *am);
void fw_am_unit_cmp_update(struct fw_am_unit *am);
void fw_am_unit_cmp_unregister(struct fw_am_unit *am);

int fw_am_unit_fcp_register(struct fw_am_unit *am);
void fw_am_unit_fcp_update(struct fw_am_unit *am);
void fw_am_unit_fcp_unregister(struct fw_am_unit *am);

int fw_am_unit_create_midi_devices(struct fw_am_unit *unit);
