/*
 * mln.h - a part of driver for Yamaha MLN2/MLN3 board modules.
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

#include "../lib.h"

struct snd_mln {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;
};

#endif
