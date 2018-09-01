/*
 * lm-loader.h - a part of driver for Focusrite Liquid Mix series
 *
 * Copyright (c) 2018 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/firmware.h>
#include <linux/delay.h>

#include "lm.h"

#define LOAD_OFFSET	0x000100000000

#define REBOOT_OFFSET	0x000080000000
#define SENTINEL_OFFSET	0x6100
#define END_OFFSET	0x6180

struct snd_lm_loader {
	struct fw_unit *unit;

	bool loaded;
	struct delayed_work dwork;
};

static void do_upload(struct work_struct *work)
{
	struct snd_lm_loader *lm =
			container_of(work, struct snd_lm_loader, dwork.work);
	struct fw_device *fw_dev = fw_parent_device(lm->unit);
	const struct firmware *handle;
	u64 offset;
	u8 *buf;
	int err;

	if (lm->loaded)
		return;

	err = request_firmware(&handle, SND_LM_FIRMWARE_NAME,
			       &lm->unit->device);
	if (err < 0)
		return;

	// Invalid size of blob for firmware.
	if (handle->size != 99840)
		goto end;

	offset = 0;
	buf = (u8 *)handle->data;

	while (offset < END_OFFSET) {
		size_t size;
		int tcode;
		int generation;

		if (offset < SENTINEL_OFFSET) {
			size = 512;
		} else if (offset == SENTINEL_OFFSET) {
			size = 508;
		} else {
			// Final.
			size = 4;
			offset |= REBOOT_OFFSET;
		}

		if (size > 4)
			tcode = TCODE_WRITE_BLOCK_REQUEST;
		else
			tcode = TCODE_WRITE_QUADLET_REQUEST;

		generation = fw_dev->generation;
		smp_rmb();	// node_id vs. generation
		err = snd_fw_transaction(lm->unit, tcode, LOAD_OFFSET + offset,
					 buf, size,
					 FW_FIXED_GENERATION | generation);
		if (err < 0) {
			snd_fw_schedule_registration(lm->unit, &lm->dwork);
			goto end;
		}

		buf += size;
		offset += size / 4;

		msleep(50);
	}

	lm->loaded = true;
end:
	release_firmware(handle);
}

int snd_lm_loader_probe(struct fw_unit *unit)
{
	struct snd_lm_loader *lm;

	lm = kzalloc(sizeof(struct snd_lm_loader), GFP_KERNEL);
	if (!lm)
		return -ENOMEM;

	lm->unit = fw_unit_get(unit);
	dev_set_drvdata(&unit->device, lm);

	INIT_DEFERRABLE_WORK(&lm->dwork, do_upload);
	snd_fw_schedule_registration(unit, &lm->dwork);

	return 0;
}

void snd_lm_loader_bus_update(struct fw_unit *unit)
{
	struct snd_lm_loader *lm = dev_get_drvdata(&unit->device);

	if (!lm->loaded)
		snd_fw_schedule_registration(unit, &lm->dwork);
}

void snd_lm_loader_remove(struct fw_unit *unit)
{
	struct snd_lm_loader *lm = dev_get_drvdata(&unit->device);

	cancel_work_sync(&lm->dwork.work);

	kfree(lm);
}
