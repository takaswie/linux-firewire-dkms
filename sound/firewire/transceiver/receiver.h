/*
 * receiver.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "transceiver.h"

struct snd_fwtx {
	struct fw_unit *unit;

	bool registered;
	struct snd_card *card;
	struct delayed_work dwork;

	struct mutex mutex;
};
