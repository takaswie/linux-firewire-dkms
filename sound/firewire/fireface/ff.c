/*
 * ff.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

#define OUI_RME	0x000a35

MODULE_DESCRIPTION("RME Fireface series Driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static void name_card(struct snd_ff *ff)
{
	struct fw_device *fw_dev = fw_parent_device(ff->unit);
	const char *const model = "Fireface Skeleton";

	strcpy(ff->card->driver, "Fireface");
	strcpy(ff->card->shortname, model);
	strcpy(ff->card->mixername, model);
	snprintf(ff->card->longname, sizeof(ff->card->longname),
		 "RME %s, GUID %08x%08x at %s, S%d", model,
		 fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&ff->unit->device), 100 << fw_dev->max_speed);
}

static void ff_free(struct snd_ff *ff)
{
	fw_unit_put(ff->unit);

	mutex_destroy(&ff->mutex);
	kfree(ff);
}

static void ff_card_free(struct snd_card *card)
{
	ff_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct snd_ff *ff = container_of(work, struct snd_ff, dwork.work);
	int err;

	if (ff->registered)
		return;

	err = snd_card_new(&ff->unit->device, -1, NULL, THIS_MODULE, 0,
			   &ff->card);
	if (err < 0)
		return;

	name_card(ff);

	err = snd_card_register(ff->card);
	if (err < 0)
		goto error;

	ff->card->private_free = ff_card_free;
	ff->card->private_data = ff;
	ff->registered = true;

	return;
error:
	snd_card_free(ff->card);
	dev_info(&ff->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static int snd_ff_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_ff *ff;

	ff = kzalloc(sizeof(struct snd_ff), GFP_KERNEL);
	if (ff == NULL)
		return -ENOMEM;

	/* initialize myself */
	ff->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, ff);

	mutex_init(&ff->mutex);

	/* Register this sound card later. */
	INIT_DEFERRABLE_WORK(&ff->dwork, do_registration);
	snd_fw_schedule_registration(unit, &ff->dwork);

	return 0;
}

static void snd_ff_update(struct fw_unit *unit)
{
	struct snd_ff *ff = dev_get_drvdata(&unit->device);

	/* Postpone a workqueue for deferred registration. */
	if (!ff->registered)
		snd_fw_schedule_registration(unit, &ff->dwork);
}

static void snd_ff_remove(struct fw_unit *unit)
{
	struct snd_ff *ff = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_work_sync(&ff->dwork.work);

	if (ff->registered) {
		/* No need to wait for releasing card object in this context. */
		snd_card_free_when_closed(ff->card);
	} else {
		/* Don't forget this case. */
		ff_free(ff);
	}
}

static const struct ieee1394_device_id snd_ff_id_table[] = {
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_ff_id_table);

static struct fw_driver ff_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-fireface",
		.bus	= &fw_bus_type,
	},
	.probe    = snd_ff_probe,
	.update   = snd_ff_update,
	.remove   = snd_ff_remove,
	.id_table = snd_ff_id_table,
};

static int __init snd_ff_init(void)
{
	return driver_register(&ff_driver.driver);
}

static void __exit snd_ff_exit(void)
{
	driver_unregister(&ff_driver.driver);
}

module_init(snd_ff_init);
module_exit(snd_ff_exit);
