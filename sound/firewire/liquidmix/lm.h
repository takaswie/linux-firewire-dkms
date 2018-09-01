/*
 * lm.h - a part of driver for Focusrite Liquid Mix series
 *
 * Copyright (c) 2018 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_LIQUIDMIX_H_INCLUDED
#define SOUND_FIREWIRE_LIQUIDMIX_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

/* TODO: remove when merging to upstream. */
#include "../../../backport.h"

#include "../lib.h"

#define SND_LM_FIRMWARE_NAME	"focusrite-liquid_mix-v2.3.4.bin"

enum snd_lm_type {
	SND_LM_TYPE_LOADER = 1,
	SND_LM_TYPE_RUNTIME,
};

struct snd_lm_common {
	enum snd_lm_type type;
};

int snd_lm_loader_probe(struct fw_unit *unit);
void snd_lm_loader_remove(struct fw_unit *unit);
void snd_lm_loader_bus_update(struct fw_unit *unit);

enum snd_lm_runtime_msg_types {
	SND_LM_RUNTIME_MSG_TYPE_ASCII = 1,
	SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_0,
	SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_1,
	SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_2,
	SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_3,
};

struct snd_lm_runtime {
	enum snd_lm_type type;

	struct fw_unit *unit;

	bool registered;
	struct delayed_work dwork;
	struct snd_card *card;

	struct mutex mutex;
	struct fw_address_handler msg_handler;
	enum snd_lm_runtime_msg_types last_msg_type;
	wait_queue_head_t wait;

	u32 caps[3];
};

int snd_lm_runtime_probe(struct fw_unit *unit);
void snd_lm_runtime_bus_update(struct fw_unit *unit);
void snd_lm_runtime_remove(struct fw_unit *unit);

int snd_lm_transaction_register(struct snd_lm_runtime *lm);
int snd_lm_transaction_reregister(struct snd_lm_runtime *lm);
void snd_lm_transaction_unregister(struct snd_lm_runtime *lm);

#endif
