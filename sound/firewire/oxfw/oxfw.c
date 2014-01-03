/*
 * oxfw.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

MODULE_DESCRIPTION("Oxford Semiconductor OXFW970/971 driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable OXFW970/971 sound card");

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

#define OXFW_FIRMWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x50000)

#define OXFW_HARDWARE_ID_ADDRESS	(CSR_REGISTER_BASE + 0x90020)
#define OXFW_HARDWARE_ID_OXFW970	0x39443841
#define OXFW_HARDWARE_ID_OXFW971	0x39373100

#define	VEN_BEHRINGER	0x001564
#define	VEN_LOUD	0x000ff2

// see speakers.c
//#define	VEN_GRIFFIN	0x001292
//#define	VEN_LACIE	0x00d04b

static int
name_device(struct snd_oxfw *oxfw, unsigned int vendor_id)
{
	char vendor[24] = {0};
	char model[24] = {0};
	u32 version = 0;
	int err;

	/* get vendor name from root directory */
	err = fw_csr_string(oxfw->device->config_rom + 5, CSR_VENDOR,
			    vendor, sizeof(vendor));
	if (err < 0)
		goto end;

	/* get model name from unit directory */
	err = fw_csr_string(oxfw->unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		goto end;

	/* 0x970?vvvv or 0x971?vvvv, where vvvv = firmware version */
	err = snd_fw_transaction(oxfw->unit, TCODE_READ_QUADLET_REQUEST,
				 OXFW_FIRMWARE_ID_ADDRESS,
				 &version, sizeof(u32), 0);
	if (err < 0)
		goto end;
	be32_to_cpus(&version);

	strcpy(oxfw->card->driver, "OXFW");
	strcpy(oxfw->card->shortname, model);
	snprintf(oxfw->card->longname, sizeof(oxfw->card->longname),
		 "%s %s (%x %04x), GUID %08x%08x at %s, S%d",
		 vendor, model, version >> 20, version & 0xffff,
		 oxfw->device->config_rom[3], oxfw->device->config_rom[4],
		 dev_name(&oxfw->unit->device),
		 100 << oxfw->device->max_speed);
	strcpy(oxfw->card->mixername, oxfw->card->driver);
end:
	return err;
}

static void
oxfw_card_free(struct snd_card *card)
{
	struct snd_oxfw *oxfw = card->private_data;

	if (oxfw->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~BIT(oxfw->card_index);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&oxfw->mutex);

	return;
}

static int
oxfw_probe(struct fw_unit *unit,
	    const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_oxfw *oxfw;
	unsigned int card_index;
	int err;

	mutex_lock(&devices_mutex);

	for (card_index = 0; card_index < SNDRV_CARDS; card_index++) {
		if (!(devices_used & BIT(card_index)) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	err = snd_card_create(index[card_index], id[card_index],
			      THIS_MODULE, sizeof(struct snd_oxfw), &card);
	if (err < 0)
		goto end;
	card->private_free = oxfw_card_free;

	oxfw = card->private_data;
	oxfw->card = card;
	oxfw->device = fw_parent_device(unit);
	oxfw->unit = unit;
	oxfw->card_index = -1;
	mutex_init(&oxfw->mutex);
	spin_lock_init(&oxfw->lock);
	init_waitqueue_head(&oxfw->hwdep_wait);

	err = name_device(oxfw, entry->vendor_id);
	if (err < 0)
		goto error;

	err = snd_oxfw_stream_discover(oxfw);
	if (err < 0)
		goto error;

	err = snd_oxfw_stream_init_duplex(oxfw);
	if (err < 0)
		goto error;

	snd_oxfw_proc_init(oxfw);

	err = snd_oxfw_create_pcm_devices(oxfw);
	if (err < 0)
		goto error;

	if ((oxfw->midi_input_ports > 0) ||
	    (oxfw->midi_output_ports > 0)) {
		err = snd_oxfw_create_midi_devices(oxfw);
		if (err < 0)
			goto error;
	}

	err = snd_oxfw_create_hwdep_device(oxfw);
	if (err < 0)
		goto error;

	snd_card_set_dev(card, &unit->device);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		goto error;
	}
	dev_set_drvdata(&unit->device, oxfw);
	devices_used |= BIT(card_index);
	oxfw->card_index = card_index;
end:
	mutex_unlock(&devices_mutex);
	return err;
error:
	snd_card_free(card);
	mutex_unlock(&devices_mutex);
	return err;
}

static void
oxfw_update(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	fcp_bus_reset(oxfw->unit);
	snd_oxfw_stream_update_duplex(oxfw);
}


static void
oxfw_remove(struct fw_unit *unit)
{
	struct snd_oxfw *oxfw = dev_get_drvdata(&unit->device);

	snd_oxfw_stream_destroy_duplex(oxfw);
	snd_card_disconnect(oxfw->card);
	snd_card_free_when_closed(oxfw->card);
}

static const struct ieee1394_device_id oxfw_id_table[] = {
	/* Behringer, F-Control Audio 202 */
	SND_OXFW_DEV_ENTRY(VEN_BEHRINGER, 0x00fc22),
	/* Mackie, Onyx-i (former model) */
	SND_OXFW_DEV_ENTRY(VEN_LOUD, 0x081216),
	/* Mackie, Onyx Sattelite */
	SND_OXFW_DEV_ENTRY(VEN_LOUD, 0x00200f),
	/* IDs are unknown but able to be supported */
	/*  Mackie, d.2 pro */
	/*  Mackie, d.4 pro */
	/*  Mackie, U.420 */
	/*  Mackie, U.420d */
	/*  Mackie, Tapco Link.Firewire */

	/* see speakers.c */
	/* Griffin, FireWave */
//	SND_OXFW_DEV_ENTRY(VEN_GRIFFIN, 0x00f970),
	/* Lacie, FireWire Speakers */
//	SND_OXFW_DEV_ENTRY(VEN_LACIE, 0x00f970),
	{}
};
MODULE_DEVICE_TABLE(ieee1394, oxfw_id_table);

static struct fw_driver oxfw_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = oxfw_probe,
	.update	  = oxfw_update,
	.remove   = oxfw_remove,
	.id_table = oxfw_id_table,
};

static int __init
snd_oxfw_init(void)
{
	return driver_register(&oxfw_driver.driver);
}

static void __exit
snd_oxfw_exit(void)
{
	driver_unregister(&oxfw_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_oxfw_init);
module_exit(snd_oxfw_exit);
