/*
 * transmitter.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"

static void am_unit_card_free(struct snd_card *card)
{
	struct fw_am_unit *am = card->private_data;

	fw_am_unit_stream_destroy(am);

	mutex_destroy(&am->mutex);
	fw_unit_put(am->unit);
	kfree(am);
}

int fw_am_unit_probe(struct fw_unit *unit)
{
	struct snd_card *card;
	struct fw_am_unit *am;
	int err;

	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(struct fw_am_unit), &card);
	if (err < 0)
		return err;

	am = card->private_data;
	am->card = card;
	card->private_free = am_unit_card_free;

	am->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, am);

	mutex_init(&am->mutex);
	spin_lock_init(&am->lock);

	/* Prepare for packet streaming. */
	err = fw_am_unit_stream_init(am);
	if (err < 0)
		goto error;

	/* Prepare for ALSA character devices. */
	err = snd_fw_trx_name_card(unit, card);
	if (err < 0)
		goto error;
	strcpy(am->card->driver, "FW-AM-UNIT");
	err = fw_am_unit_create_midi_devices(am);
	if (err < 0)
		goto error;
	err = fw_am_unit_create_pcm_devices(am);
	if (err < 0)
		goto error;

	/*
	 * Register handlers for addresses in IEC 61883-1. In peer system,
	 * corresponding driver is loaded but character devices do not appear
	 * yet because of postponed registration.
	 */
	err = fw_am_unit_cmp_init(am);
	if (err < 0)
		goto error;
	err = fw_am_unit_fcp_init(am);
	if (err < 0) {
		fw_am_unit_cmp_destroy(am);
		goto error;
	}

	/* Register and add ALSA character devices. */
	err = snd_card_register(card);
	if (err < 0) {
		fw_am_unit_stream_destroy(am);
		fw_am_unit_fcp_destroy(am);
		fw_am_unit_cmp_destroy(am);
		goto error;
	}

	return 0;
error:
	snd_card_free(card);
	return err;
}

void fw_am_unit_update(struct fw_unit *unit)
{
	struct fw_am_unit *am = dev_get_drvdata(&unit->device);

	fw_am_unit_stream_update(am);
	fw_am_unit_cmp_update(am);
	fw_am_unit_fcp_update(am);
}

void fw_am_unit_remove(struct fw_unit *unit)
{
	struct fw_am_unit *am = dev_get_drvdata(&unit->device);

	/* Release handlers in advance. */
	fw_am_unit_cmp_destroy(am);
	fw_am_unit_fcp_destroy(am);

	snd_card_free_when_closed(am->card);
}
