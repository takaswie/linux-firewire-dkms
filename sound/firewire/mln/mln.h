/*
 * mln.h - a part of driver for Yamaha MLN3 board module.
 *
 * Copyright (c) 2017-2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_MLN_H_INCLUDED
#define SOUND_MLN_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>

/* TODO: remove when merging to upstream. */
#include "../../../backport.h"

#include <sound/core.h>
#include <sound/info.h>

#include "../lib.h"

struct snd_mln_protocol;

struct snd_mln {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	const struct snd_mln_protocol *protocol;

	/* For notification. */
	struct fw_address_handler async_handler;
	int owner_generation;
};

struct snd_mln_protocol {
	unsigned int version;

	void (*dump_info)(struct snd_mln *mln, struct snd_info_buffer *buffer);
};

extern const struct snd_mln_protocol snd_mln_protocol_v3;

int snd_mln_transaction_register(struct snd_mln *mln);
int snd_mln_transaction_reregister(struct snd_mln *mln);
void snd_mln_transaction_unregister(struct snd_mln *mln);

void snd_mln_proc_init(struct snd_mln *mln);

#endif
