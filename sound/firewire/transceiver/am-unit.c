/*
 * am-unit.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"

#define PROBE_DELAY_MS		(2 * MSEC_PER_SEC)

static void am_unit_free(struct fw_am_unit *am)
{
	fw_am_unit_stream_destroy(am);
	fw_am_unit_cmp_unregister(am);
	fw_am_unit_fcp_unregister(am);

	mutex_destroy(&am->mutex);
	fw_unit_put(am->unit);
	kfree(am);
}

static void am_unit_card_free(struct snd_card *card)
{
	am_unit_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct fw_am_unit *am =
			container_of(work, struct fw_am_unit, dwork.work);
	int err;

	if (am->registered)
		return;

	err = snd_card_new(&am->unit->device, -1, NULL, THIS_MODULE, 0,
			   &am->card);
	if (err < 0)
		return;

	err = snd_fwtxrx_name_card(am->unit, am->card);
	if (err < 0)
		goto error;
	strcpy(am->card->driver, "FW-Transmitter");

	err = fw_am_unit_create_midi_devices(am);
	if (err < 0)
		goto error;

	err = snd_card_register(am->card);
	if (err < 0)
		goto error;

	am->card->private_free = am_unit_card_free;
	am->card->private_data = am;
	am->registered = true;

	return;
error:
	snd_card_free(am->card);
        dev_info(&am->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static void schedule_registration(struct fw_am_unit *am)
{
	struct fw_card *fw_card = fw_parent_device(am->unit)->card;
	u64 now, delay;

	now = get_jiffies_64();
	delay = fw_card->reset_jiffies + msecs_to_jiffies(PROBE_DELAY_MS);

	if (time_after64(delay, now))
		delay -= now;
	else
		delay = 0;

	mod_delayed_work(system_wq, &am->dwork, delay);
}

int fw_am_unit_probe(struct fw_unit *unit)
{
	struct fw_am_unit *am;
	int err;

	/* Allocate this independent of sound card instance. */
	am = kzalloc(sizeof(struct fw_am_unit), GFP_KERNEL);
	if (am == NULL)
		return -ENOMEM;

	am->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, am);

	mutex_init(&am->mutex);
	spin_lock_init(&am->lock);

	err = fw_am_unit_stream_init(am);
	if (err < 0)
		return err;

	err = fw_am_unit_cmp_register(am);
	if (err < 0) {
		fw_am_unit_stream_destroy(am);
		return err;
	}

	err = fw_am_unit_fcp_register(am);
	if (err < 0) {
		fw_am_unit_stream_destroy(am);
		fw_am_unit_cmp_unregister(am);
		return err;
	}

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&am->dwork, do_registration);
	schedule_registration(am);

	return 0;
}

void fw_am_unit_update(struct fw_unit *unit)
{
	struct fw_am_unit *am = dev_get_drvdata(&unit->device);

	if (!am->registered) {
		schedule_registration(am);
	} else {
		fw_am_unit_stream_update(am);
		fw_am_unit_cmp_update(am);
		fw_am_unit_fcp_update(am);
	}
}

void fw_am_unit_remove(struct fw_unit *unit)
{
	struct fw_am_unit *am = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&am->dwork);

	if (am->registered) {
		/* No need to wait for releasing card object in this context. */
		snd_card_free_when_closed(am->card);
	} else {
		/* Don't forget this case. */
		am_unit_free(am);
	}
}
