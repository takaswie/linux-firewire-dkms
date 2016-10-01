/*
 * am-unit.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "am-unit.h"

MODULE_DESCRIPTION("An Audio and Music unit_directory entry to IEEE 1394 bus");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp");
MODULE_LICENSE("GPL v2");

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

static int fw_am_unit_probe(struct platform_device *pdev)
{
	return fw_core_add_descriptor(&am_unit_directory);
}

static int fw_am_unit_remove(struct platform_device *pdev)
{
	fw_core_remove_descriptor(&am_unit_directory);
	return 0;
}

static struct platform_driver fw_am_unit_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-firewire-am-unit",
	},
	.probe	= fw_am_unit_probe,
	.remove	= fw_am_unit_remove,
};

module_platform_driver(fw_am_unit_driver);
