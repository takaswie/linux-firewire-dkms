/*
 * receiver.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "receiver.h"

#define PROBE_DELAY_MS		(2 * MSEC_PER_SEC)

static void fwtx_free(struct snd_fwtx *fwtx)
{
	snd_fwtx_stream_destroy_simplex(fwtx);

	mutex_destroy(&fwtx->mutex);
	fw_unit_put(fwtx->unit);
	kfree(fwtx);
}

static void fwtx_card_free(struct snd_card *card)
{
	fwtx_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct snd_fwtx *fwtx = container_of(work, struct snd_fwtx, dwork.work);
	int err;

	if (fwtx->registered)
		return;

	err = snd_card_new(&fwtx->unit->device, -1, NULL, THIS_MODULE, 0,
			   &fwtx->card);
	if (err < 0)
		return;

	err = snd_fwtxrx_name_card(fwtx->unit, fwtx->card);
	if (err < 0)
		goto error;
	strcpy(fwtx->card->driver, "FW-Receiver");

	err = snd_fwtx_create_midi_devices(fwtx);
	if (err < 0)
		goto error;

	err = snd_card_register(fwtx->card);
	if (err < 0)
		goto error;

	fwtx->card->private_free = fwtx_card_free;
	fwtx->card->private_data = fwtx;
	fwtx->registered = true;

	return;
error:
	snd_card_free(fwtx->card);
	dev_info(&fwtx->unit->device,
		 "Sound card registration failed: %d\n", err);
}

static void schedule_registration(struct snd_fwtx *fwtx)
{
	struct fw_card *fw_card = fw_parent_device(fwtx->unit)->card;
	u64 now, delay;

	now = get_jiffies_64();
	delay = fw_card->reset_jiffies + msecs_to_jiffies(PROBE_DELAY_MS);

	if (time_after64(delay, now))
		delay -= now;
	else
		delay = 0;

	mod_delayed_work(system_wq, &fwtx->dwork, delay);
}

int snd_fwtx_probe(struct fw_unit *unit)
{
	struct snd_fwtx *fwtx;
	int err;

	/* Allocate this independent of sound card instance. */
	fwtx = kzalloc(sizeof(struct snd_fwtx), GFP_KERNEL);
	if (fwtx == NULL)
		return -ENOMEM;

	fwtx->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, fwtx);

	mutex_init(&fwtx->mutex);
	spin_lock_init(&fwtx->lock);

	err = snd_fwtx_stream_init_simplex(fwtx);
	if (err < 0)
		return err;

	/* Allocate and register this sound card later. */
	INIT_DEFERRABLE_WORK(&fwtx->dwork, do_registration);
	schedule_registration(fwtx);

	return 0;
}

void snd_fwtx_update(struct fw_unit *unit)
{
	struct snd_fwtx *fwtx = dev_get_drvdata(&unit->device);

	if (!fwtx->registered)
		schedule_registration(fwtx);

	if (fwtx->registered)
		snd_fwtx_stream_update_simplex(fwtx);
}

void snd_fwtx_remove(struct fw_unit *unit)
{
	struct snd_fwtx *fwtx = dev_get_drvdata(&unit->device);

	/*
	 * Confirm to stop the work for registration before the sound card is
	 * going to be released. The work is not scheduled again because bus
	 * reset handler is not called anymore.
	 */
	cancel_delayed_work_sync(&fwtx->dwork);

	if (fwtx->registered) {
		/* No need to wait for releasing card object in this context. */
		snd_card_free_when_closed(fwtx->card);
	} else {
		/* Don't forget this case. */
		fwtx_free(fwtx);
	}
}
