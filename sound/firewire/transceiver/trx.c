/*
 * transceiver.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include "trx.h"

MODULE_DESCRIPTION("AMDTP transmitter to receiver units on IEEE 1394 bus");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp");
MODULE_LICENSE("GPL v2");

int snd_fw_trx_stream_add_pcm_constraints(struct amdtp_stream *stream,
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

int snd_fw_trx_name_card(struct fw_unit *unit, struct snd_card *card)
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

static int fw_trx_probe(struct fw_unit *unit,
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

static void fw_trx_update(struct fw_unit *unit)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_card *fw_card = fw_dev->card;

	if (fw_card->node_id == fw_dev->node_id)
		fw_am_unit_update(unit);
	else
		snd_fwtx_update(unit);
}

static void fw_trx_remove(struct fw_unit *unit)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_card *fw_card = fw_dev->card;

	if (fw_card->node_id == fw_dev->node_id)
		fw_am_unit_remove(unit);
	else
		snd_fwtx_remove(unit);
}

static const struct ieee1394_device_id fw_trx_id_table[] = {
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
MODULE_DEVICE_TABLE(ieee1394, fw_trx_id_table);

static struct fw_driver fw_trx_driver = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "snd-firewire-transceiver",
		.bus	= &fw_bus_type
	},
	.probe	  = fw_trx_probe,
	.update	  = fw_trx_update,
	.remove	  = fw_trx_remove,
	.id_table = fw_trx_id_table,
};

static int __init snd_fw_trx_init(void)
{
	return driver_register(&fw_trx_driver.driver);
}

static void __exit snd_fw_trx_exit(void)
{
	driver_unregister(&fw_trx_driver.driver);
}

module_init(snd_fw_trx_init)
module_exit(snd_fw_trx_exit)
