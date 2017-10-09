/*
 * mln.c - a part of driver for Yamaha MLN2/MLN3 board modules.
 *
 * Copyright (c) 2017-2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "mln.h"

#define OUI_YAMAHA	0x00a0de

MODULE_DESCRIPTION("Driver for Yamaha MLN2/MLN3 board modules");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static int name_card(struct snd_mln *mln)
{
	struct fw_device *fw_dev = fw_parent_device(mln->unit);
	char vendor[32];
	char model[32];
	int err;

	/* get vendor name from root directory */
	err = fw_csr_string(fw_dev->config_rom + 5, CSR_VENDOR,
			    vendor, sizeof(vendor));
	if (err < 0)
		return err;

	/* get model name from unit directory */
	err = fw_csr_string(mln->unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		return err;

	strcpy(mln->card->driver, "MLN");
	strcpy(mln->card->shortname, model);
	strcpy(mln->card->mixername, model);
	snprintf(mln->card->longname, sizeof(mln->card->longname),
		 "%s %s, GUID %08x%08x at %s, S%d", vendor, model,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&mln->unit->device), 100 << fw_dev->max_speed);

	return 0;
}

static void mln_card_free(struct snd_card *card)
{
	struct snd_mln *mln = card->private_data;

	fw_unit_put(mln->unit);

	mutex_destroy(&mln->mutex);
}

static int snd_mln_probe(struct fw_unit *unit,
			 const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_mln *mln;
	int err;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(*mln), &card);
	if (err < 0)
		return err;
	card->private_free = mln_card_free;

	mln = card->private_data;
	mln->card = card;
	mln->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, mln);

	mutex_init(&mln->mutex);

	err = name_card(mln);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	return 0;
}

static void snd_mln_update(struct fw_unit *unit)
{
	return;
}

static void snd_mln_remove(struct fw_unit *unit)
{
	struct snd_mln *mln = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(mln->card);
}

static const struct ieee1394_device_id snd_mln_id_table[] = {
	/* Yamaha 01X */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_YAMAHA,
		.specifier_id	= OUI_YAMAHA,
		.version	= 0xffffff,
		.model_id	= 0x100005,
		.driver_data	= (kernel_ulong_t)NULL,
	},
	/* Yamaha i88X */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_SPECIFIER_ID |
				  IEEE1394_MATCH_VERSION |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= OUI_YAMAHA,
		.specifier_id	= OUI_YAMAHA,
		.version	= 0xffffff,
		.model_id	= 0x100007,
		.driver_data	= (kernel_ulong_t)NULL,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_mln_id_table);

static struct fw_driver mln_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-mln",
		.bus	= &fw_bus_type,
	},
	.probe    = snd_mln_probe,
	.update   = snd_mln_update,
	.remove   = snd_mln_remove,
	.id_table = snd_mln_id_table,
};

static int __init snd_mln_init(void)
{
	return driver_register(&mln_driver.driver);
}

static void __exit snd_mln_exit(void)
{
	driver_unregister(&mln_driver.driver);
}

module_init(snd_mln_init);
module_exit(snd_mln_exit);
