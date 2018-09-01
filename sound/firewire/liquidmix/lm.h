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

int snd_lm_loader_probe(struct fw_unit *unit);
void snd_lm_loader_remove(struct fw_unit *unit);
void snd_lm_loader_bus_update(struct fw_unit *unit);

#endif
