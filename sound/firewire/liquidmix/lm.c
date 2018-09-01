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
	// Liquid Mix 16/32 runtime v2.3.4.
	LM_DEVICE_ENTRY(0x420304),
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
	if (entry == lm_id_table)
		return snd_lm_runtime_probe(unit);
	else
		return snd_lm_loader_probe(unit);
}

static void lm_remove(struct fw_unit *unit)
{
	struct snd_lm_common *lm = dev_get_drvdata(&unit->device);

	if (lm->type == SND_LM_TYPE_LOADER)
		snd_lm_loader_remove(unit);
	else if (lm->type == SND_LM_TYPE_RUNTIME)
		snd_lm_runtime_remove(unit);
	else
		dev_info(&unit->device,
			 "Something goes bad. Please report to developer.\n");
}

static void lm_bus_update(struct fw_unit *unit)
{
	struct snd_lm_common *lm = dev_get_drvdata(&unit->device);

	if (lm->type == SND_LM_TYPE_LOADER)
		snd_lm_loader_bus_update(unit);
	else if (lm->type == SND_LM_TYPE_RUNTIME)
		snd_lm_runtime_bus_update(unit);
	else
		dev_info(&unit->device,
			 "Something goes bad. Please report to developer.\n");
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
