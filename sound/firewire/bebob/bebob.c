/*
 * bebob.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * BeBoB is 'BridgeCo enhanced Breakout Box'. This is installed to firewire
 * devices with DM1000/DM1100/DM1500 chipset. It gives common way for host
 * system to handle BeBoB based devices.
 */

#include "bebob.h"

MODULE_DESCRIPTION("BridgeCo BeBoB driver");
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
MODULE_PARM_DESC(enable, "enable BeBoB sound card");

static DEFINE_MUTEX(devices_mutex);
static DECLARE_BITMAP(devices_used, SNDRV_CARDS);

/* Offsets from information register. */
#define INFO_OFFSET_GUID		0x10
#define INFO_OFFSET_HW_MODEL_ID		0x18
#define INFO_OFFSET_HW_MODEL_REVISION	0x1c

#define VEN_EDIROL	0x000040ab
#define VEN_PRESONUS	0x00000a92
#define VEN_BRIDGECO	0x000007f5
#define VEN_MACKIE	0x0000000f
#define VEN_STANTON	0x00001260
#define VEN_TASCAM	0x0000022e
#define VEN_BEHRINGER	0x00001564
#define VEN_APOGEE	0x000003db
#define VEN_ESI		0x00000f1b
#define VEN_ACOUSTIC	0x00000002
#define VEN_CME		0x0000000a
#define VEN_PHONIC	0x00001496
#define VEN_LYNX	0x000019e5
#define VEN_ICON	0x00001a9e
#define VEN_PRISMSOUND	0x00001198

static int
name_device(struct snd_bebob *bebob, unsigned int vendor_id)
{
	struct fw_device *fw_dev = fw_parent_device(bebob->unit);
	char vendor[24] = {0};
	char model[32] = {0};
	u32 id;
	u32 data[2] = {0};
	u32 revision;
	int err;

	/* get vendor name from root directory */
	err = fw_csr_string(fw_dev->config_rom + 5, CSR_VENDOR,
			    vendor, sizeof(vendor));
	if (err < 0)
		goto end;

	/* get model name from unit directory */
	err = fw_csr_string(bebob->unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		goto end;

	/* get hardware id */
	err = snd_bebob_read_quad(bebob->unit, INFO_OFFSET_HW_MODEL_ID,
				  &id);
	if (err < 0)
		goto end;

	/* get hardware revision */
	err = snd_bebob_read_quad(bebob->unit, INFO_OFFSET_HW_MODEL_REVISION,
				  &revision);
	if (err < 0)
		goto end;

	/* get GUID */
	err = snd_bebob_read_block(bebob->unit, INFO_OFFSET_GUID,
				   data, sizeof(data));
	if (err < 0)
		goto end;

	strcpy(bebob->card->driver, "BeBoB");
	strcpy(bebob->card->shortname, model);
	strcpy(bebob->card->mixername, model);
	snprintf(bebob->card->longname, sizeof(bebob->card->longname),
		 "%s %s (id:%d, rev:%d), GUID %08x%08x at %s, S%d",
		 vendor, model, id, revision,
		 data[0], data[1], dev_name(&bebob->unit->device),
		 100 << fw_dev->max_speed);
end:
	return err;
}

static void
bebob_card_free(struct snd_card *card)
{
	struct snd_bebob *bebob = card->private_data;

	if (bebob->card_index >= 0) {
		mutex_lock(&devices_mutex);
		clear_bit(bebob->card_index, devices_used);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&bebob->mutex);
}

static int
bebob_probe(struct fw_unit *unit,
	    const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_bebob *bebob;
	unsigned int card_index;
	int err;

	mutex_lock(&devices_mutex);

	for (card_index = 0; card_index < SNDRV_CARDS; card_index++) {
		if (!test_bit(card_index, devices_used) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	err = snd_card_new(&unit->device, index[card_index], id[card_index],
			   THIS_MODULE, sizeof(struct snd_bebob), &card);
	if (err < 0)
		goto end;
	bebob = card->private_data;
	bebob->card_index = card_index;
	set_bit(card_index, devices_used);
	card->private_free = bebob_card_free;

	bebob->card = card;
	bebob->unit = unit;
	mutex_init(&bebob->mutex);
	spin_lock_init(&bebob->lock);

	err = name_device(bebob, entry->vendor_id);
	if (err < 0)
		goto error;

	err = snd_bebob_stream_discover(bebob);
	if (err < 0)
		goto error;

	snd_bebob_proc_init(bebob);

	if ((bebob->midi_input_ports > 0) ||
	    (bebob->midi_output_ports > 0)) {
		err = snd_bebob_create_midi_devices(bebob);
		if (err < 0)
			goto error;
	}

	err = snd_bebob_create_pcm_devices(bebob);
	if (err < 0)
		goto error;

	err = snd_bebob_stream_init_duplex(bebob);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0) {
		snd_bebob_stream_destroy_duplex(bebob);
		goto error;
	}

	dev_set_drvdata(&unit->device, bebob);
end:
	mutex_unlock(&devices_mutex);
	return err;
error:
	mutex_unlock(&devices_mutex);
	snd_card_free(card);
	return err;
}

static void
bebob_update(struct fw_unit *unit)
{
	struct snd_bebob *bebob = dev_get_drvdata(&unit->device);
	fcp_bus_reset(bebob->unit);
	snd_bebob_stream_update_duplex(bebob);
}

static void bebob_remove(struct fw_unit *unit)
{
	struct snd_bebob *bebob = dev_get_drvdata(&unit->device);
	snd_bebob_stream_destroy_duplex(bebob);
	snd_card_disconnect(bebob->card);
	snd_card_free_when_closed(bebob->card);
}

static const struct ieee1394_device_id bebob_id_table[] = {
	/* Edirol, FA-66 */
	SND_BEBOB_DEV_ENTRY(VEN_EDIROL, 0x00010049),
	/* Edirol, FA-101 */
	SND_BEBOB_DEV_ENTRY(VEN_EDIROL, 0x00010048),
	/* Presonus, FIREBOX */
	SND_BEBOB_DEV_ENTRY(VEN_PRESONUS, 0x00010000),
	/* PreSonus, FIREPOD/FP10 */
	SND_BEBOB_DEV_ENTRY(VEN_PRESONUS, 0x00010066),
	/* PreSonus, Inspire1394 */
	SND_BEBOB_DEV_ENTRY(VEN_PRESONUS, 0x00010001),
	/* BridgeCo, RDAudio1 */
	SND_BEBOB_DEV_ENTRY(VEN_BRIDGECO, 0x00010048),
	/* BridgeCo, Audio5 */
	SND_BEBOB_DEV_ENTRY(VEN_BRIDGECO, 0x00010049),
	/* Mackie, Onyx 1220/1620/1640 (Firewire I/O Card) */
	SND_BEBOB_DEV_ENTRY(VEN_MACKIE, 0x00010065),
	/* Mackie, d.2 (Firewire Option) */
	SND_BEBOB_DEV_ENTRY(VEN_MACKIE, 0x00010067),
	/* Stanton, ScratchAmp */
	SND_BEBOB_DEV_ENTRY(VEN_STANTON, 0x00000001),
	/* Tascam, IF-FW DM */
	SND_BEBOB_DEV_ENTRY(VEN_TASCAM, 0x00010067),
	/* Behringer, XENIX UFX 1204 */
	SND_BEBOB_DEV_ENTRY(VEN_BEHRINGER, 0x00001204),
	/* Behringer, XENIX UFX 1604 */
	SND_BEBOB_DEV_ENTRY(VEN_BEHRINGER, 0x00001604),
	/* Behringer, Digital Mixer X32 series (X-UF Card) */
	SND_BEBOB_DEV_ENTRY(VEN_BEHRINGER, 0x00000006),
	/* Apogee Electronics, Rosetta 200/400 (X-FireWire card) */
	/* Apogee Electronics, DA/AD/DD-16X (X-FireWire card) */
	SND_BEBOB_DEV_ENTRY(VEN_APOGEE, 0x00010048),
	/* Apogee Electronics, Ensemble */
	SND_BEBOB_DEV_ENTRY(VEN_APOGEE, 0x00001eee),
	/* ESI, Quatafire610 */
	SND_BEBOB_DEV_ENTRY(VEN_ESI, 0x00010064),
	/* AcousticReality, eARMasterOne */
	SND_BEBOB_DEV_ENTRY(VEN_ACOUSTIC, 0x00000002),
	/* CME, MatrixKFW */
	SND_BEBOB_DEV_ENTRY(VEN_CME, 0x00030000),
	/* Phonic, Helix Board 12 MkII */
	SND_BEBOB_DEV_ENTRY(VEN_PHONIC, 0x00050000),
	/* Phonic, Helix Board 18 MkII */
	SND_BEBOB_DEV_ENTRY(VEN_PHONIC, 0x00060000),
	/* Phonic, Helix Board 24 MkII */
	SND_BEBOB_DEV_ENTRY(VEN_PHONIC, 0x00070000),
	/* Phonic, Helix Board 12 Universal/18 Universal/24 Universal */
	SND_BEBOB_DEV_ENTRY(VEN_PHONIC, 0x00000000),
	/* Lynx, Aurora 8/16 (LT-FW) */
	SND_BEBOB_DEV_ENTRY(VEN_LYNX, 0x00000001),
	/* ICON, FireXon */
	SND_BEBOB_DEV_ENTRY(VEN_ICON, 0x00000001),
	/* PrismSound, Orpheus */
	SND_BEBOB_DEV_ENTRY(VEN_PRISMSOUND, 0x00010048),
	/* PrismSound, ADA-8XR */
	SND_BEBOB_DEV_ENTRY(VEN_PRISMSOUND, 0x0000ada8),
	/* IDs are unknown but able to be supported */
	/*  Apogee, Mini-ME Firewire */
	/*  Apogee, Mini-DAC Firewire */
	/*  Behringer, F-Control Audio 1616 */
	/*  Behringer, F-Control Audio 610 */
	/*  Cakawalk, Sonar Power Studio 66 */
	/*  CME, UF400e */
	/*  ESI, Quotafire XL */
	/*  Infrasonic, DewX */
	/*  Infrasonic, Windy6 */
	/*  Mackie, Digital X Bus x.200 */
	/*  Mackie, Digital X Bus x.400 */
	/*  Phonic, HB 12 */
	/*  Phonic, HB 24 */
	/*  Phonic, HB 18 */
	/*  Phonic, FireFly 202 */
	/*  Phonic, FireFly 302 */
	/*  Rolf Spuler, Firewire Guitar */
	{}
};
MODULE_DEVICE_TABLE(ieee1394, bebob_id_table);

static struct fw_driver bebob_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-bebob",
		.bus	= &fw_bus_type,
	},
	.probe    = bebob_probe,
	.update	  = bebob_update,
	.remove   = bebob_remove,
	.id_table = bebob_id_table,
};

static int __init
snd_bebob_init(void)
{
	return driver_register(&bebob_driver.driver);
}

static void __exit
snd_bebob_exit(void)
{
	driver_unregister(&bebob_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_bebob_init);
module_exit(snd_bebob_exit);
