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

int snd_fwtxrx_stream_add_pcm_constraints(struct amdtp_stream *stream,
					  struct snd_pcm_runtime *runtime)
{
	struct snd_pcm_hardware *hw = &runtime->hw;
	unsigned int i;

	hw->info = SNDRV_PCM_INFO_MMAP |
		   SNDRV_PCM_INFO_MMAP_VALID |
		   SNDRV_PCM_INFO_BATCH |
		   SNDRV_PCM_INFO_INTERLEAVED |
		   SNDRV_PCM_INFO_BLOCK_TRANSFER;

	if (stream->direction == AMDTP_IN_STREAM)
		hw->formats = AM824_IN_PCM_FORMAT_BITS;
	else
		hw->formats = AM824_OUT_PCM_FORMAT_BITS;

	/* TODO: PCM channels */
	hw->channels_min = 2;
	hw->channels_max = 2;

	for (i = 0; i < CIP_SFC_COUNT; i++)
		hw->rates |= snd_pcm_rate_to_rate_bit(amdtp_rate_table[i]);
	snd_pcm_limit_hw_rates(runtime);

	hw->periods_min = 2;		/* SNDRV_PCM_INFO_BATCH */
	hw->periods_max = UINT_MAX;

	hw->period_bytes_min = 4 * hw->channels_max;	/* bytes for a frame */

	/* Just to prevent from allocating much pages. */
	hw->period_bytes_max = hw->period_bytes_min * 2048;
	hw->buffer_bytes_max = hw->period_bytes_max * hw->periods_min;

	return amdtp_am824_add_pcm_hw_constraints(stream, runtime);
}

int snd_fwtxrx_name_card(struct fw_unit *unit, struct snd_card *card)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	char vendor[24];
	char model[32];
	int err;

	err = fw_csr_string(fw_dev->config_rom + 5, CSR_VENDOR,
			    vendor, sizeof(vendor));
	if (err < 0)
		return err;

	err = fw_csr_string(unit->directory, CSR_MODEL,
			    model, sizeof(model));
	if (err < 0)
		return err;

	strcpy(card->shortname, model);
	strcpy(card->mixername, model);

	snprintf(card->longname, sizeof(card->longname),
		 "%s %s, GUID %08x%08x at %s, S%d",
		 vendor, model, fw_dev->config_rom[3], fw_dev->config_rom[4],
		 dev_name(&unit->device), 100 << fw_dev->max_speed);

	return 0;
}

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
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_card *fw_card = fw_dev->card;
	int err;

	err = check_unit_directory(unit);
	if (err < 0)
		return err;

	if (fw_card->node_id == fw_dev->node_id)
		err = fw_am_unit_probe(unit);
	else
		err = snd_fwtx_probe(unit);

	return err;
}

static void fwtxrx_update(struct fw_unit *unit)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_card *fw_card = fw_dev->card;

	if (fw_card->node_id == fw_dev->node_id)
		fw_am_unit_update(unit);
	else
		snd_fwtx_update(unit);
}

static void fwtxrx_remove(struct fw_unit *unit)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_card *fw_card = fw_dev->card;

	if (fw_card->node_id == fw_dev->node_id)
		fw_am_unit_remove(unit);
	else
		snd_fwtx_remove(unit);
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

	err = fw_am_cmp_init();
	if (err < 0)
		return err;

	err = fw_am_fcp_init();
	if (err < 0) {
		fw_am_cmp_destroy();
		return err;
	}

	err = driver_register(&fwtxrx_driver.driver);
	if (err < 0) {
		fw_am_fcp_destroy();
		fw_am_cmp_destroy();
		return err;
	}

	err = fw_core_add_descriptor(&am_unit_directory);
	if (err < 0) {
		driver_unregister(&fwtxrx_driver.driver);
		fw_am_fcp_destroy();
		fw_am_cmp_destroy();
	}

	return err;
}

static void __exit snd_fwtxrx_exit(void)
{
	fw_core_remove_descriptor(&am_unit_directory);
	driver_unregister(&fwtxrx_driver.driver);
	fw_am_fcp_destroy();
	fw_am_cmp_destroy();
}

module_init(snd_fwtxrx_init)
module_exit(snd_fwtxrx_exit)
