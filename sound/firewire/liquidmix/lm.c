/*
 * lm.c - a part of driver for Focusrite Liquid Mix series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "lm.h"

#define OUI_FOCUSRITE	0x00130e

MODULE_DESCRIPTION("Focusrite Liquid Mix driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(SND_LM_FIRMWARE_NAME);

#define LM_DEVICE_ENTRY(model)					\
	{							\
		.match_flags = IEEE1394_MATCH_VENDOR_ID |	\
			       IEEE1394_MATCH_MODEL_ID,		\
		.vendor_id = OUI_FOCUSRITE,			\
		.model_id = (model),				\
	}

static const struct ieee1394_device_id lm_id_table[] = {
	// Liquid Mix 32 firmware loader.
	LM_DEVICE_ENTRY(0x010200),
	// Liquid Mix 16 firmware loader.
	LM_DEVICE_ENTRY(0x010204),
	{},
};
MODULE_DEVICE_TABLE(ieee1394, lm_id_table);

static int lm_probe(struct fw_unit *unit,
		    const struct ieee1394_device_id *entry)
{
	return snd_lm_loader_probe(unit);
}

static void lm_remove(struct fw_unit *unit)
{
	snd_lm_loader_remove(unit);
}

static void lm_bus_update(struct fw_unit *unit)
{
	snd_lm_loader_bus_update(unit);
}

static struct fw_driver lm_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = lm_probe,
	.update   = lm_bus_update,
	.remove   = lm_remove,
	.id_table = lm_id_table,
};

static int __init alsa_lm_init(void)
{
	return driver_register(&lm_driver.driver);
}

static void __exit alsa_lm_exit(void)
{
	driver_unregister(&lm_driver.driver);
}

module_init(alsa_lm_init);
module_exit(alsa_lm_exit);
