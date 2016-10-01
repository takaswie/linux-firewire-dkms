/*
 * am-unit.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_AM_UNIT_H_INCLUDED
#define SOUND_FIREWIRE_AM_UNIT_H_INCLUDED

#include <linux/firewire.h>
#include <linux/firewire-constants.h>

/* See d71e6a11737f4b3d857425a1d6f893231cbd1296 */
#define ROOT_VENDOR_ID_OLD	0xd00d1e
#define ROOT_VENDOR_ID		0x001f11
#define ROOT_MODEL_ID		0x023901

/* Unit directory for AV/C protocol. */
#define AM_UNIT_SPEC_1394TA	0x0000a02d
#define AM_UNIT_VERSION_AVC	0x00010001
#define AM_UNIT_MODEL_ID	0x00736e64	/* "snd" */
#define AM_UNIT_NAME_0		0x4c696e75	/* Linu */
#define AM_UNIT_NAME_1		0x7820414c	/* x AL */
#define AM_UNIT_NAME_2		0x53410000	/* SA.. */

#endif
