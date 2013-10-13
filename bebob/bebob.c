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
#include <sound/core.h>

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

#define VENDOR_MAUDIO1	0x00000d6c
#define VENDOR_MAUDIO2	0x000007f5
#define VENDOR_YAMAHA	0x0000a0de

#define MODEL_YAMAHA_GO44			0x0010000b
#define MODEL_YAMAHA_GO46			0x0010000c
#define MODEL_MAUDIO_OZONIC			0x0000000a
#define MODEL_MAUDIO_FW410_BOOTLOADER		0x00010058
#define MODEL_MAUDIO_FW_410			0x00010046
#define MODEL_MAUDIO_AUDIOPHILE_BOTH		0x00010060
#define MODEL_MAUDIO_SOLO			0x00010062
#define MODEL_MAUDIO_FW_1814_BOOTLOADER		0x00010070
#define MODEL_MAUDIO_FW_1814			0x00010071
#define MODEL_MAUDIO_NRV10			0x00010081
#define MODEL_MAUDIO_PROJECTMIX			0x00010091

static int
name_device(struct snd_bebob *bebob, int vendor_id)
{
	char vendor[24] = {};
	char model[24] = {};
	u32 id;
	u32 data[2] = {};
	u32 revision;
	int err = 0;

	/* get vendor name */
	if ((vendor_id == VENDOR_MAUDIO1) || (vendor_id == VENDOR_MAUDIO2))
		strcpy(vendor, "M-Audio");
	else if (vendor_id == VENDOR_YAMAHA)
		strcpy(vendor, "YAMAHA");
	else
		strcpy(vendor, "Unknown");

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

static int
snd_bebob_probe(struct fw_unit *unit,
		const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_bebob *bebob;
	const struct snd_bebob_spec *spec;
	int card_index, err;

	mutex_lock(&devices_mutex);

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; card_index += 1) {
		if (!(devices_used & BIT(card_index)) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	/* if needed, load firmware and exit */
	spec = (const struct snd_bebob_spec *)entry->driver_data;
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

	if (!bebob->spec->discover)
		goto error;
	err = bebob->spec->discover(bebob);
	if (err < 0)
		goto error;

	/* name device with communication */
	err = name_device(bebob, entry->vendor_id);
	if (err < 0)
		goto error;

	/* proc interfaces */
	snd_bebob_proc_init(bebob);

	/* create interfaces */
	err = snd_bebob_create_control_devices(bebob);
	if (err < 0)
		goto error;

	err = snd_bebob_create_pcm_devices(bebob);
	if (err < 0)
		goto error;

	err = snd_bebob_create_midi_devices(bebob);
	if (err < 0)
		goto error;

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
	goto end;

error:
	snd_card_free(card);

end:
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
		goto end;

	snd_bebob_stream_destroy_duplex(bebob);

	snd_card_disconnect(bebob->card);
	snd_card_free_when_closed(bebob->card);

end:
	return;
}

static const struct ieee1394_device_id snd_bebob_id_table[] = {
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_YAMAHA,
		.model_id	= MODEL_YAMAHA_GO44,
		.driver_data	= (kernel_ulong_t)&yamaha_go_spec
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_YAMAHA,
		.model_id	= MODEL_YAMAHA_GO46,
		.driver_data	= (kernel_ulong_t)&yamaha_go_spec
	},
	/* Ozonic has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_OZONIC,
		.driver_data	= (kernel_ulong_t)&maudio_ozonic_spec
	},
	/* Firewire 410 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO2,
		.model_id	= MODEL_MAUDIO_FW410_BOOTLOADER,
		.driver_data	= (kernel_ulong_t)&maudio_bootloader_spec
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO2,
		.model_id	= MODEL_MAUDIO_FW_410,
		.driver_data	= (kernel_ulong_t)&maudio_fw410_spec
	},
	/* Firewire Audiophile has one ID for both bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_AUDIOPHILE_BOTH,
		.driver_data	= (kernel_ulong_t)&maudio_audiophile_spec
	},
	/* Firewire Solo has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_SOLO,
		.driver_data	= (kernel_ulong_t)&maudio_solo_spec
	},
	/* Firewire 1814 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_FW_1814_BOOTLOADER,
		.driver_data	= (kernel_ulong_t)&maudio_bootloader_spec
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_FW_1814,
		.driver_data	= (kernel_ulong_t)&maudio_fw1814_spec
	},
	/* NRV10 is booted just after power on. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_NRV10,
		.driver_data	= (kernel_ulong_t)&maudio_nrv10_spec
	},
	/* ProjectMix is booted just after power on. */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_PROJECTMIX,
		.driver_data	= (kernel_ulong_t)&maudio_projectmix_spec
	},
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
