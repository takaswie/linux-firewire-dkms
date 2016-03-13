/*
 * transceiver.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include "transceiver.h"

MODULE_DESCRIPTION("AMDTP transmitter to receiver units on IEEE 1394 bus");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp");
MODULE_LICENSE("GPL v2");

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

static int check_unit_directory(struct fw_unit *unit)
{
	struct fw_csr_iterator it;
	int key, val;
	int id;
	char name[12];
	unsigned int index, offset, i;
	u32 literal;
	int err;

	/* Check model ID in unit directory. */
	id = 0;
	fw_csr_iterator_init(&it, unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		if (key == CSR_MODEL) {
			id = val;
			break;
		}
	}

	if (id != AM_UNIT_MODEL_ID)
		return -ENODEV;

	/* Check texture descriptor leaf. */
	err = fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name));
	if (err < 0)
		return err;

	for (i = 0; i < strlen(name); i++) {
		index = i / 4;
		if (index == 0)
			literal = AM_UNIT_NAME_0;
		else if (index == 1)
			literal = AM_UNIT_NAME_1;
		else if (index == 2)
			literal = AM_UNIT_NAME_2;
		else
			break;

		offset = i % 4;
		if (name[i] != ((literal >> (24 - offset * 8)) & 0xff))
			return -ENODEV;
	}

	return 0;
}

static int fwtxrx_probe(struct fw_unit *unit,
			const struct ieee1394_device_id *entry)
{
	return check_unit_directory(unit);
}

static void fwtxrx_update(struct fw_unit *unit)
{
	return;
}

static void fwtxrx_remove(struct fw_unit *unit)
{
	return;
}

static u32 am_unit_leafs[] = {
	0x00040000,		/* Unit directory consists of below 4 quads. */
	(CSR_SPECIFIER_ID << 24) | AM_UNIT_SPEC_1394TA,
	(CSR_VERSION	  << 24) | AM_UNIT_VERSION_AVC,
	(CSR_MODEL	  << 24) | AM_UNIT_MODEL_ID,
	((CSR_LEAF | CSR_DESCRIPTOR) << 24) | 0x00000001, /* Begin at next. */
	0x00050000,		/* Text leaf consists of below 5 quads. */
	0x00000000,
	0x00000000,
	AM_UNIT_NAME_0,
	AM_UNIT_NAME_1,
	AM_UNIT_NAME_2,
};

static struct fw_descriptor am_unit_directory = {
	.length = ARRAY_SIZE(am_unit_leafs),
	.immediate = 0x0c0083c0,	/* Node capabilities */
	.key = (CSR_DIRECTORY | CSR_UNIT) << 24,
	.data = am_unit_leafs,
};

static const struct ieee1394_device_id fwtx_id_table[] = {
	/* Linux 4.0 or later. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= ROOT_VENDOR_ID,
		.specifier_id	= AM_UNIT_SPEC_1394TA,
		.version	= AM_UNIT_VERSION_AVC,
		.model_id	= AM_UNIT_MODEL_ID,
	},
	/* Linux 3.19 or former. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= ROOT_VENDOR_ID_OLD,
		.specifier_id	= AM_UNIT_SPEC_1394TA,
		.version	= AM_UNIT_VERSION_AVC,
		.model_id	= AM_UNIT_MODEL_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(ieee1394, fwtx_id_table);

static struct fw_driver fwtxrx_driver = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "snd-firewire-transceiver",
		.bus	= &fw_bus_type
	},
	.probe	  = fwtxrx_probe,
	.update	  = fwtxrx_update,
	.remove	  = fwtxrx_remove,
	.id_table = fwtx_id_table,
};

static int __init snd_fwtxrx_init(void)
{
	int err;

	err = driver_register(&fwtxrx_driver.driver);
	if (err < 0)
		return err;

	err = fw_core_add_descriptor(&am_unit_directory);
	if (err < 0)
		driver_unregister(&fwtxrx_driver.driver);

	return err;
}

static void __exit snd_fwtxrx_exit(void)
{
	fw_core_remove_descriptor(&am_unit_directory);
	driver_unregister(&fwtxrx_driver.driver);
}

module_init(snd_fwtxrx_init)
module_exit(snd_fwtxrx_exit)
