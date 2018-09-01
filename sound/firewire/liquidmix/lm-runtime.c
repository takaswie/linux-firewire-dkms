/*
 * lm-runtime.h - a part of driver for Focusrite Liquid Mix series
 *
 * Copyright (c) 2018 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "lm.h"

static int name_card(struct snd_lm_runtime *lm)
{
	struct fw_device *fw_dev = fw_parent_device(lm->unit);
	struct fw_csr_iterator it;
	int key, val;
	int version;
	int suffix;

	version = 0;
	fw_csr_iterator_init(&it, lm->unit->directory);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		switch (key) {
		case CSR_VERSION:
			version = val;
			break;
		default:
			break;
		}
	}

	if (version == 0x200)
		suffix = 32;
	else
		suffix = 16;

	strcpy(lm->card->driver, "FW-LM");
	snprintf(lm->card->shortname, sizeof(lm->card->shortname),
		 "LiquidMix%u", suffix);
	snprintf(lm->card->shortname, sizeof(lm->card->shortname),
		 "LiquidMix%u", suffix);
	snprintf(lm->card->longname, sizeof(lm->card->longname),
		 "Focusrite Liquid Mix %u (runtime version v2.3.4) at %s, %d",
		 suffix, dev_name(&lm->unit->device), 100 << fw_dev->max_speed);

	return 0;
}

static void lm_runtime_free(struct snd_lm_runtime *lm)
{
	snd_lm_transaction_unregister(lm);

	fw_unit_put(lm->unit);

	mutex_destroy(&lm->mutex);
}

static void lm_card_free(struct snd_card *card)
{
	lm_runtime_free(card->private_data);
}

static void do_registration(struct work_struct *work)
{
	struct snd_lm_runtime *lm = container_of(work, struct snd_lm_runtime,
						 dwork.work);
	int err;

	err = snd_card_new(&lm->unit->device, -1, NULL, THIS_MODULE, 0,
			   &lm->card);
	if (err < 0)
		return;

	err = name_card(lm);
	if (err < 0)
		goto error;

	err = snd_lm_transaction_register(lm);
	if (err < 0)
		goto error;

	err = snd_card_register(lm->card);
	if (err < 0)
		goto error;

	/*
	 * After registered, lm instance can be released corresponding to
	 * releasing the sound card instance.
	 */
	lm->card->private_free = lm_card_free;
	lm->card->private_data = lm;
	lm->registered = true;

	return;
error:
	snd_lm_transaction_unregister(lm);
	snd_card_free(lm->card);
	dev_info(&lm->unit->device,
		 "Sound card registration failed: %d\n", err);
}

int snd_lm_runtime_probe(struct fw_unit *unit)
{
	struct snd_lm_runtime *lm;

	lm = kzalloc(sizeof(struct snd_lm_runtime), GFP_KERNEL);
	if (!lm)
		return -ENOMEM;
	lm->type = SND_LM_TYPE_RUNTIME;
	lm->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, lm);

	mutex_init(&lm->mutex);

	INIT_DEFERRABLE_WORK(&lm->dwork, do_registration);
	snd_fw_schedule_registration(unit, &lm->dwork);

	return 0;
}

void snd_lm_runtime_bus_update(struct fw_unit *unit)
{
	struct snd_lm_runtime *lm = dev_get_drvdata(&unit->device);

	if (!lm->registered)
		snd_fw_schedule_registration(unit, &lm->dwork);
	else
		snd_lm_transaction_reregister(lm);
}

void snd_lm_runtime_remove(struct fw_unit *unit)
{
	struct snd_lm_runtime *lm = dev_get_drvdata(&unit->device);

	cancel_delayed_work_sync(&lm->dwork);

	if (lm->registered) {
		snd_card_free_when_closed(lm->card);
	} else {
		lm_runtime_free(lm);
	}
}
