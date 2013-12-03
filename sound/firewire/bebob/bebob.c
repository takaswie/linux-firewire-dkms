/*
 * bebob.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include "bebob.h"

MODULE_DESCRIPTION("bridgeCo BeBoB driver");
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
static unsigned int devices_used;

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
#define VEN_MAUDIO1	0x00000d6c
#define VEN_MAUDIO2	0x000007f5
#define VEN_FOCUSRITE	0x0000130e
#define VEN_TERRATEC	0x00000aac
#define VEN_YAMAHA	0x0000a0de

#define MODEL_MAUDIO_AUDIOPHILE_BOTH	0x00010060
#define MODEL_MAUDIO_FW1814		0x00010071
#define MODEL_MAUDIO_PROJECTMIX		0x00010091
#define MODEL_FOCUSRITE_SAFFIRE_BOTH	0x00000000

static int
name_device(struct snd_bebob *bebob, unsigned int vendor_id)
{
	char vendor[24] = {};
	char model[24] = {};
	u32 id;
	u32 data[2] = {};
	u32 revision;
	int err;

	/* get vendor name */
	if (vendor_id == VEN_EDIROL)
		strcpy(vendor, "Edirol");
	else if (vendor_id == VEN_PRESONUS)
		strcpy(vendor, "Presonus");
	else if (vendor_id == VEN_BRIDGECO)
		strcpy(vendor, "BridgeCo");
	else if (vendor_id == VEN_MACKIE)
		strcpy(vendor, "Mackie");
	else if (vendor_id == VEN_STANTON)
		strcpy(vendor, "Stanton");
	else if (vendor_id == VEN_TASCAM)
		strcpy(vendor, "Tacsam");
	else if (vendor_id == VEN_BEHRINGER)
		strcpy(vendor, "Behringer");
	else if (vendor_id == VEN_APOGEE)
		strcpy(vendor, "Apogee");
	else if (vendor_id == VEN_ESI)
		strcpy(vendor, "ESI");
	else if (vendor_id == VEN_ACOUSTIC)
		strcpy(vendor, "AcousticReality");
	else if (vendor_id == VEN_CME)
		strcpy(vendor, "CME");
	else if (vendor_id == VEN_PHONIC)
		strcpy(vendor, "Phonic");
	else if (vendor_id == VEN_LYNX)
		strcpy(vendor, "Lynx");
	else if (vendor_id == VEN_ICON)
		strcpy(vendor, "ICON");
	else if (vendor_id == VEN_PRISMSOUND)
		strcpy(vendor, "PrismSound");
	else if ((vendor_id == VEN_MAUDIO1) || (vendor_id == VEN_MAUDIO2))
		strcpy(vendor, "M-Audio");
	else if (vendor_id == VEN_TERRATEC)
		strcpy(vendor, "Terratec");
	else if (vendor_id == VEN_YAMAHA)
		strcpy(vendor, "YAMAHA");

	/* get model name */
	err = fw_csr_string(bebob->unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		goto end;

	/* get hardware id */
	err = snd_bebob_read_quad(bebob, INFO_OFFSET_HW_MODEL_ID,
				  &id, sizeof(id));
	if (err < 0)
		goto end;

	/* get hardware revision */
	err = snd_bebob_read_quad(bebob, INFO_OFFSET_HW_MODEL_REVISION,
				  &revision, sizeof(revision));
	if (err < 0)
		goto end;

	/* get GUID */
	err = snd_bebob_read_block(bebob, INFO_OFFSET_GUID,
				   data, sizeof(data));
	if (err < 0)
		goto end;

	strcpy(bebob->card->driver, "BeBoB");
	strcpy(bebob->card->shortname, model);
	snprintf(bebob->card->longname, sizeof(bebob->card->longname),
		 "%s %s (id:%d, rev:%d), GUID %08x%08x at %s, S%d",
		 vendor, model, id, revision,
		 data[0], data[1],
		 dev_name(&bebob->unit->device),
		 100 << bebob->device->max_speed);
end:
	return err;
}

static void
snd_bebob_card_free(struct snd_card *card)
{
	struct snd_bebob *bebob = card->private_data;

	if (bebob->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~BIT(bebob->card_index);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&bebob->mutex);

	return;
}

static bool
check_audiophile_booted(struct fw_unit *unit)
{
	char name[24] = {0};

	if (fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name)) < 0)
		return false;

	if (strncmp(name, "FW Audiophile Bootloader", 15) == 0)
		return false;
	else
		return true;
}

static const struct snd_bebob_spec *
get_saffire_spec(struct fw_unit *unit)
{
	char name[24] = {0};

	if (fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name)) < 0)
		return NULL;

	if (strcmp(name, "SaffireLE") == 0)
		return &saffire_le_spec;
	else
		return &saffire_spec;
}


static int
snd_bebob_probe(struct fw_unit *unit,
		const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_bebob *bebob;
	const struct snd_bebob_spec *spec;
	unsigned int card_index;
	int err;

	mutex_lock(&devices_mutex);

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; card_index++) {
		if (!(devices_used & BIT(card_index)) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	if (entry->model_id == MODEL_FOCUSRITE_SAFFIRE_BOTH)
		spec = get_saffire_spec(unit);
	else
		spec = (const struct snd_bebob_spec *)entry->driver_data;

	if (spec == NULL) {
		err = -EIO;
		goto end;
	}

	/* if needed, load firmware and exit */
	if ((spec->load) &&
	    ((entry->model_id != MODEL_MAUDIO_AUDIOPHILE_BOTH) ||
	     (!check_audiophile_booted(unit)))) {
		spec->load(unit, entry);
		dev_info(&unit->device,
			 "loading firmware for 0x%08X:0x%08X\n",
			 entry->vendor_id, entry->model_id);
		err = 0;
		goto end;
	}

	/* create card */
	err = snd_card_create(index[card_index], id[card_index],
			THIS_MODULE, sizeof(struct snd_bebob), &card);
	if (err < 0)
		goto end;
	card->private_free = snd_bebob_card_free;

	/* initialize myself */
	bebob = card->private_data;
	bebob->card = card;
	bebob->device = fw_parent_device(unit);
	bebob->unit = unit;
	bebob->card_index = -1;
	bebob->spec = spec;
	mutex_init(&bebob->mutex);
	spin_lock_init(&bebob->lock);
	init_waitqueue_head(&bebob->hwdep_wait);

	/* discover */
	if ((entry->vendor_id == VEN_MAUDIO1) &&
	    (entry->model_id == MODEL_MAUDIO_FW1814))
		err = snd_bebob_maudio_special_discover(bebob, true);
	else if ((entry->vendor_id == VEN_MAUDIO1) &&
	         (entry->model_id == MODEL_MAUDIO_PROJECTMIX))
		err = snd_bebob_maudio_special_discover(bebob, false);
	else
		err = snd_bebob_stream_discover(bebob);
	if (err < 0)
		goto error;

	/* name device with communication */
	err = name_device(bebob, entry->vendor_id);
	if (err < 0)
		goto error;

	err = snd_bebob_create_hwdep_device(bebob);
	if (err < 0)
		goto error;

	err = snd_bebob_create_pcm_devices(bebob);
	if (err < 0)
		goto error;

	if ((bebob->midi_input_ports > 0) ||
	    (bebob->midi_output_ports > 0)) {
		err = snd_bebob_create_midi_devices(bebob);
		if (err < 0)
			goto error;
	}

	err = snd_bebob_create_control_devices(bebob);
	if (err < 0)
		goto error;

	snd_bebob_proc_init(bebob);

	err = snd_bebob_stream_init_duplex(bebob);
	if (err < 0)
		goto error;

	/* register card and device */
	snd_card_set_dev(card, &unit->device);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		goto error;
	}
	dev_set_drvdata(&unit->device, bebob);
	devices_used |= BIT(card_index);
	bebob->card_index = card_index;

	/* proved */
	err = 0;
end:
	mutex_unlock(&devices_mutex);
	return err;
error:
	snd_card_free(card);
	mutex_unlock(&devices_mutex);
	return err;
}

static void
snd_bebob_update(struct fw_unit *unit)
{
	struct snd_bebob *bebob = dev_get_drvdata(&unit->device);

	/* this is for firmware bootloader */
	if (bebob == NULL)
		return;

	fcp_bus_reset(bebob->unit);
	snd_bebob_stream_update_duplex(bebob);
}


static void snd_bebob_remove(struct fw_unit *unit)
{
	struct snd_bebob *bebob = dev_get_drvdata(&unit->device);

	/* this is for firmware bootloader */
	if (bebob == NULL)
		return;

	snd_bebob_stream_destroy_duplex(bebob);
	snd_card_disconnect(bebob->card);
	snd_card_free_when_closed(bebob->card);
}

struct snd_bebob_clock_spec normal_clk_spec = {
	.get_freq	= &snd_bebob_stream_get_rate,
	.set_freq	= &snd_bebob_stream_set_rate
};
static const struct snd_bebob_spec spec_normal = {
	.load		= NULL,
	.clock		= &normal_clk_spec,
	.meter		= NULL
};

static const struct ieee1394_device_id snd_bebob_id_table[] = {
	/* Edirol, FA-66 */
	SND_BEBOB_DEV_ENTRY(VEN_EDIROL, 0x00010049, spec_normal),
	/* Edirol, FA-101 */
	SND_BEBOB_DEV_ENTRY(VEN_EDIROL, 0x00010048, spec_normal),
	/* Presonus, FIREBOX */
	SND_BEBOB_DEV_ENTRY(VEN_PRESONUS, 0x00010000, spec_normal),
	/* PreSonus FIREPOD */
	SND_BEBOB_DEV_ENTRY(VEN_PRESONUS, 0x00010066, spec_normal),
	/* BridgeCo, RDAudio1 */
	SND_BEBOB_DEV_ENTRY(VEN_BRIDGECO, 0x00010048, spec_normal),
	/* BridgeCo, Audio5 */
	SND_BEBOB_DEV_ENTRY(VEN_BRIDGECO, 0x00010049, spec_normal),
	/* Mackie, Onyx 820/1220/1620/1640 (Firewire I/O Card) */
	SND_BEBOB_DEV_ENTRY(VEN_MACKIE, 0x00010065, spec_normal),
	SND_BEBOB_DEV_ENTRY(VEN_MACKIE, 0x00010067, spec_normal),
	/* Mackie, d.2 (Firewire Option) */
	SND_BEBOB_DEV_ENTRY(VEN_MACKIE, 0x00010067, spec_normal),
	/* Stanton, ScratchAmp */
	SND_BEBOB_DEV_ENTRY(VEN_STANTON, 0x00000001, spec_normal),
	/* Tascam, IF-FW/DM */
	SND_BEBOB_DEV_ENTRY(VEN_TASCAM, 0x00010067, spec_normal),
	/* ApogeeElectronics, Rosetta 200/400 (X-FireWire card) */
	/* ApogeeElectronics, DA/AD/DD-16X (X-FireWire card) */
	SND_BEBOB_DEV_ENTRY(VEN_APOGEE, 0x00010048, spec_normal),
	/* ESI, Quatafire610 */
	SND_BEBOB_DEV_ENTRY(VEN_ESI, 0x00010064, spec_normal),
	/* AcousticReality, eARMasterOne */
	SND_BEBOB_DEV_ENTRY(VEN_ACOUSTIC, 0x00000002, spec_normal),
	/* CME, MatrixKFW */
	SND_BEBOB_DEV_ENTRY(VEN_CME, 0x00030000, spec_normal),
	/* Phonic, HB 12/12 MkII/12 Universal */
	/* Phonic, HB 18/18 MkII/18 Universal */
	/* Phonic, HB 24/24 MkII/24 Universal */
	/* Phonic, FireFly 202/302 */
	SND_BEBOB_DEV_ENTRY(VEN_PHONIC, 0x00000000, spec_normal),
	/* Lynx, Aurora 8/16 (LT-FW) */
	SND_BEBOB_DEV_ENTRY(VEN_LYNX, 0x00000001, spec_normal),
	/* ICON, FireXon */
	SND_BEBOB_DEV_ENTRY(VEN_ICON, 0x00000001, spec_normal),
	/* PrismSound, Orpheus */
	SND_BEBOB_DEV_ENTRY(VEN_PRISMSOUND, 0x00010048, spec_normal),
	/* PrismSound, ADA-8XR */
	SND_BEBOB_DEV_ENTRY(VEN_PRISMSOUND, 0x0000ada8, spec_normal),
	/* M-Audio, Ozonic */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, 0x0000000a, maudio_ozonic_spec),
	/* M-Audio, Firewire 410.  */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO2, 0x00010058, maudio_bootloader_spec),
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO2, 0x00010046, maudio_fw410_spec),
	/* M-Audio, Firewire Audiophile, both of bootloader and firmware */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, MODEL_MAUDIO_AUDIOPHILE_BOTH,
						maudio_audiophile_spec),
	/* M-Audio, Firewire Solo */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, 0x00010062, maudio_solo_spec),
	/* M-Audio NRV10 */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, 0x00010081, maudio_nrv10_spec),
	/* Firewire 1814 */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, 0x00010070, maudio_bootloader_spec),
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, MODEL_MAUDIO_FW1814,
						maudio_special_spec),
	/* M-Audio ProjectMix */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, MODEL_MAUDIO_PROJECTMIX,
						maudio_special_spec),
	/* M-Audio, ProFireLightbridge */
	SND_BEBOB_DEV_ENTRY(VEN_MAUDIO1, 0x000100a1, spec_normal),
	/* Focusrite, SaffirePro 26 I/O */
	SND_BEBOB_DEV_ENTRY(VEN_FOCUSRITE, 0x00000003, saffirepro_26_spec),
	/* Focusrite, SaffirePro 10 I/O */
	SND_BEBOB_DEV_ENTRY(VEN_FOCUSRITE, 0x00000006, saffirepro_10_spec),
	/* Focusrite, Saffire(no label and LE) */
	SND_BEBOB_DEV_ENTRY(VEN_FOCUSRITE, MODEL_FOCUSRITE_SAFFIRE_BOTH,
			    saffire_spec),
	/* TerraTec Electronic GmbH, PHASE 88 Rack FW */
	SND_BEBOB_DEV_ENTRY(VEN_TERRATEC, 0x00000003, phase88_rack_spec),
	/* TerraTec Electronic GmbH, PHASE 24 FW */
	SND_BEBOB_DEV_ENTRY(VEN_TERRATEC, 0x00000004, phase24_series_spec),
	/* TerraTec Electronic GmbH, Phase X24 FW */
	SND_BEBOB_DEV_ENTRY(VEN_TERRATEC, 0x00000007, phase24_series_spec),
	/* TerraTec Electronic GmbH, EWS MIC2/MIC8 */
	SND_BEBOB_DEV_ENTRY(VEN_TERRATEC, 0x00000005, spec_normal),
	/* Terratec Electronic GmbH, Aureon 7.1 Firewire */
	SND_BEBOB_DEV_ENTRY(VEN_TERRATEC, 0x00000002, spec_normal),
	/* Yamaha, GO44 */
	SND_BEBOB_DEV_ENTRY(VEN_YAMAHA, 0x0010000b, yamaha_go_spec),
	/* YAMAHA, GO46 */
	SND_BEBOB_DEV_ENTRY(VEN_YAMAHA, 0x0010000c, yamaha_go_spec),
	/* Ids are unknown but able to be supported */
	/*  PreSonus, Inspire 1394 */
	/*  Mackie, Digital X Bus x.200 */
	/*  Mackie, Digital X Bus x.400 */
	/*  CME, UF400e */
	/*  Infrasonic, DewX */
	/*  Infrasonic, Windy6 */
	/*  Rolf Spuler, Firewire Guitar */
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_bebob_id_table);

static struct fw_driver snd_bebob_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-bebob",
		.bus	= &fw_bus_type,
	},
	.probe    = snd_bebob_probe,
	.update	  = snd_bebob_update,
	.remove   = snd_bebob_remove,
	.id_table = snd_bebob_id_table,
};

static int __init
snd_bebob_init(void)
{
	return driver_register(&snd_bebob_driver.driver);
}

static void __exit
snd_bebob_exit(void)
{
	driver_unregister(&snd_bebob_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_bebob_init);
module_exit(snd_bebob_exit);
