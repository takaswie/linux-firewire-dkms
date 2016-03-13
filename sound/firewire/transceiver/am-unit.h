/*
 * am-unit.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "transceiver.h"

struct fw_am_unit {
	struct fw_unit *unit;

	struct mutex mutex;

	bool registered;
	struct snd_card *card;
	struct delayed_work dwork;
};
