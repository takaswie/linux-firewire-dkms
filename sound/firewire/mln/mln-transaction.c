/*
 * mln-transaction.c - a part of driver for Yamaha MLN3 board module.
 *
 * Copyright (c) 2017-2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "mln.h"

#include <linux/delay.h>

#define NO_OWNER	0xffff000000000000

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	fw_send_response(card, request, RCODE_COMPLETE);
}

static int register_notification_address(struct snd_mln *mln, bool retry)
{
	struct fw_device *device = fw_parent_device(mln->unit);
	__be64 *buffer;
	unsigned int retries;
	int err;

	retries = (retry) ? 3 : 0;

	buffer = kzalloc(sizeof(*buffer) * 3, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	buffer[0] = cpu_to_be64(NO_OWNER);
	while (1) {
		/* Use this to save old value. */
		buffer[2] = buffer[0];
		buffer[1] = cpu_to_be64((u64)device->card->node_id << 48 |
					mln->async_handler.offset);

		mln->owner_generation = device->generation;
		smp_rmb();	/* node_id vs. generation */
		err = snd_fw_transaction(mln->unit, TCODE_LOCK_COMPARE_SWAP,
				0xffffec000000, buffer, sizeof(*buffer) * 2,
				FW_FIXED_GENERATION | mln->owner_generation);
		if (err == 0) {
			/* Success. */
			if (buffer[0] == buffer[2])
				break;
			/* The address seems to be already registered. */
			if (buffer[0] == buffer[1])
				break;

			dev_err(&mln->unit->device,
				"device is already in use\n");
			err = -EBUSY;
		}

		if (err != -EAGAIN || retries-- == 0)
			break;

		msleep(20);
	}

	kfree(buffer);

	if (err < 0)
		mln->owner_generation = -1;

	return err;
}

static void unregister_notification_address(struct snd_mln *mln)
{
	struct fw_device *device = fw_parent_device(mln->unit);
	__be64 *buffer;

	buffer = kzalloc(sizeof(*buffer) * 2, GFP_KERNEL);
	if (buffer == NULL)
		return;

	buffer[0] = cpu_to_be64((u64)device->card->node_id << 48 |
				mln->async_handler.offset);
	buffer[1] = cpu_to_be64(NO_OWNER);
	snd_fw_transaction(mln->unit, TCODE_LOCK_COMPARE_SWAP,
			   0xffffec000000, buffer, sizeof(*buffer) * 2,
			   FW_FIXED_GENERATION | mln->owner_generation);
	kfree(buffer);

	mln->owner_generation = -1;
}

int snd_mln_transaction_reregister(struct snd_mln *mln)
{
	if (mln->async_handler.callback_data == NULL)
		return -EINVAL;

	return register_notification_address(mln, false);
}

int snd_mln_transaction_register(struct snd_mln *mln)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffffe0000000ull,
		.end	= 0xffffe000ffffull,
	};
	int err;

	/* Perhaps, 4 byte message is transferred. */
	mln->async_handler.length = 4;
	mln->async_handler.address_callback = handle_message;
	mln->async_handler.callback_data = mln;

	err = fw_core_add_address_handler(&mln->async_handler,
					  &resp_register_region);
	if (err < 0) {
		mln->async_handler.callback_data = NULL;
		return err;
	}

	err = register_notification_address(mln, true);
	if (err < 0) {
		fw_core_remove_address_handler(&mln->async_handler);
		mln->async_handler.callback_data = NULL;
	}

	return err;
}

void snd_mln_transaction_unregister(struct snd_mln *mln)
{
	if (mln->async_handler.callback_data == NULL)
		return;

	unregister_notification_address(mln);

	fw_core_remove_address_handler(&mln->async_handler);
	mln->async_handler.callback_data = NULL;
}
