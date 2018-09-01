/*
 * lm-transaction.h - a part of driver for Focusrite Liquid Mix series
 *
 * Copyright (c) 2018 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "lm.h"

#define MSG_OFFSET		0x000000000000

#define ASCII_OFFSET_0		0x0000
#define UNKNOWN_OFFSET_0	0x0010
#define UNKNOWN_OFFSET_1	0x0014
#define UNKNOWN_OFFSET_2	0x0018
#define UNKNOWN_OFFSET_3	0x001c
#define ALLOCATION_SIZE		0x0020

static void handle_msg(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	struct snd_lm_runtime *lm = callback_data;
	int rcode;
	__be32 *buf = (__be32 *)data;
	enum snd_lm_runtime_msg_types msg_type;

	if (offset >= lm->msg_handler.offset &&
	    offset < lm->msg_handler.offset + ALLOCATION_SIZE)
		rcode = RCODE_ADDRESS_ERROR;
	else if (tcode != TCODE_WRITE_QUADLET_REQUEST)
		rcode = RCODE_TYPE_ERROR;
	else
		rcode = RCODE_COMPLETE;
	fw_send_response(card, request, rcode);

	switch (offset - lm->msg_handler.offset) {
	case ASCII_OFFSET_0:
	{
		// TODO: pick up ASCII messages in sequential
		u8 *ascii = (u8 *)buf;
		if (!strchr(ascii, '\n'))
			return;
		// transactions.
		msg_type = SND_LM_RUNTIME_MSG_TYPE_ASCII;
		break;
	}
	case UNKNOWN_OFFSET_0:
		lm->caps[0] = be32_to_cpu(*buf);
		msg_type = SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_0;
		break;
	case UNKNOWN_OFFSET_1:
		lm->caps[1] = be32_to_cpu(*buf);
		msg_type = SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_1;
		break;
	case UNKNOWN_OFFSET_2:
		lm->caps[2] = be32_to_cpu(*buf);
		msg_type = SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_2;
		break;
	case UNKNOWN_OFFSET_3:
		msg_type = SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_3;
		break;
	default:
		return;
	}

	lm->last_msg_type = msg_type;
	wake_up(&lm->wait);
}

void snd_lm_transaction_unregister(struct snd_lm_runtime *lm)
{
	__be32 reg;

	if (!lm->msg_handler.callback_data)
		return;

	fw_core_remove_address_handler(&lm->msg_handler);

	lm->msg_handler.callback_data = NULL;

	reg = cpu_to_be32(0x00000000);
	snd_fw_transaction(lm->unit, TCODE_WRITE_QUADLET_REQUEST,
			   MSG_OFFSET, &reg, sizeof(reg), FW_QUIET);
}

int snd_lm_transaction_reregister(struct snd_lm_runtime *lm)
{
	struct fw_device *fw_dev = fw_parent_device(lm->unit);
	__be32 reg;
	long timeout;
	int err;

	reg = cpu_to_be32((fw_dev->card->node_id << 16) |
			  ((lm->msg_handler.offset >> 32) & 0xffff));
	init_waitqueue_head(&lm->wait);

	err = snd_fw_transaction(lm->unit, TCODE_WRITE_QUADLET_REQUEST,
				 MSG_OFFSET, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;

	timeout = wait_event_timeout(lm->wait,
			lm->last_msg_type == SND_LM_RUNTIME_MSG_TYPE_UNKNOWN_2,
			msecs_to_jiffies(5));
	if (timeout == 0)
		return -ETIMEDOUT;

	return 0;
}

static int allocate_own_address(struct snd_lm_runtime *lm, int i)
{
	struct fw_address_region msg_region;
	int err;

	lm->msg_handler.length = ALLOCATION_SIZE;
	lm->msg_handler.address_callback = handle_msg;
	lm->msg_handler.callback_data = lm;

	msg_region.start = 0x000100000000ull * i;
	msg_region.end = msg_region.start + lm->msg_handler.length;

	err = fw_core_add_address_handler(&lm->msg_handler, &msg_region);
	if (err < 0)
		return err;

	if (lm->msg_handler.offset & 0x0000ffffffff) {
		fw_core_remove_address_handler(&lm->msg_handler);
		return -EAGAIN;
	}

	return 0;
}

int snd_lm_transaction_register(struct snd_lm_runtime *lm)
{
	int i;
	int err;

	// Controllers are allowed to register 4 bytes in MSB of the address.
	for (i = 1; i < 0xff; ++i) {
		err = allocate_own_address(lm, i);
		if (err != -EBUSY && err != -EAGAIN)
			break;
	}
	if (err < 0)
		return err;

	return snd_lm_transaction_reregister(lm);
}
