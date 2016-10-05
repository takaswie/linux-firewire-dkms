/*
 * tx-fcp.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"

enum fcp_state {
	STATE_IDLE,
	STATE_PENDING,
	STATE_WAITING,
};

#define TRANSACTION_SLOTS		10
#define TRANSACTION_FRAME_MAX_SIZE	256

struct fcp_transaction {
	struct fw_card *card;
	int destination;

	int generation;

	enum fcp_state state;
	unsigned int size;
	u8 frame[TRANSACTION_FRAME_MAX_SIZE];

	struct fw_transaction request;
};

/*
 * AV/C plug info command on
 *  AV/C Digital Interface Command Set General Specification Version 4.2
 */
static void handle_avc_plug_info(struct fw_am_unit *am,
				 u8* frame, unsigned int len)
{

	if (frame[0] != 0x01)
		goto rejected;
	if (frame[1] != 0xff)
		goto rejected;
	if (frame[3] != 0x00)
		goto rejected;

	frame[0] = 0x0c;	/* Implemented/Status */
	frame[4] = 0x00;	/* PCR Input plugs */
	frame[5] = OHCI1394_MIN_TX_CTX;	/* PCR Output plugs */
	frame[6] = 0x00;	/* External input plugs */
	frame[7] = 0x00;	/* External output plugs */

	return;

rejected:
	frame[0] = 0x0a;
}

/*
 * AV/C unit info command on
 *  AV/C Digital Interface Command Set General Specification Version 4.2
 */
static void handle_avc_unit_info(struct fw_am_unit *am,
				 u8* frame, unsigned int len)
{
	struct fw_device *fw_dev = fw_parent_device(am->unit);
	struct fw_csr_iterator it;
	int key, val;
	u32 vendor_id;

	if (frame[0] != 0x01)
		goto rejected;
	if (frame[1] != 0xff)
		goto rejected;

	vendor_id = 0;
	fw_csr_iterator_init(&it, fw_dev->config_rom + 5);
	while (fw_csr_iterator_next(&it, &key, &val)) {
		if (key == CSR_VENDOR) {
			vendor_id = val;
			break;
		}
	}
	if (vendor_id == 0)
		goto not_implemented;

	frame[0] = 0x0c;	/* Implemented/Status */
	frame[1] = 0xff;	/* Unit */
	frame[2] = 0x30;	/* Unit Info */
	frame[3] = 0x07;	/* Fixed value */
	frame[4] = (0x01 << 3);	/* Audio subunit */
	frame[5] = (vendor_id >> 4) & 0xff;
	frame[6] = (vendor_id >> 2) & 0xff;
	frame[7] = vendor_id & 0xff;

	return;
rejected:
	frame[0] = 0x0a;
	return;
not_implemented:
	frame[0] = 0x08;
}

/*
 * Output plug signal format command on
 *  AV/C Digital Interface Command Set General Specification Version 4.2
 */
static void handle_avc_out_signal_format(struct fw_am_unit *am,
					 u8 *frame, unsigned int len)
{
	unsigned int index, sfc, i;

	mutex_lock(&am->mutex);

	index = frame[3];
	if (frame[1] != 0xff || index >= OHCI1394_MIN_TX_CTX)
		goto rejected;

	/* Control */
	if (frame[0] == 0x00) {
		if (amdtp_stream_running(&am->opcr[index].stream))
			goto rejected;

		sfc = frame[5];
		if (sfc >= CIP_SFC_COUNT)
			goto rejected;

		am->opcr[index].rate = amdtp_rate_table[sfc];
		frame[0] = 0x09;
	}

	/* Status */
	if (frame[0] == 0x01) {
		for (i = 0; i < CIP_SFC_COUNT; ++i) {
			if (amdtp_rate_table[i] == am->opcr[index].rate)
				break;
		}
		frame[0] = 0x0c;	/* Implemented/stable */
		frame[5] = i;
	}

	mutex_unlock(&am->mutex);
	return;
rejected:
	frame[0] = 0x0a;
	mutex_unlock(&am->mutex);
}

static void handle_avc_stream_format(struct fw_am_unit *am,
				     u8 *frame, unsigned int len)
{
	unsigned int i, index;
	struct avc_stream_formation formation;

	/* SINGLE subfunction is supported only. */
	if (frame[3] != 0xc0)
		goto not_implemented;

	/* Check address. */
	if (frame[1] != 0xff || frame[4] != AVC_GENERAL_PLUG_DIR_OUT ||
	    frame[5] !=0x00 || frame[6] != 0x00 ||
	    frame[7] >= OHCI1394_MIN_TX_CTX)
		goto not_implemented;

	/* The index of PCR unit. */
	index = frame[7];

	/* Control. */
	if (frame[0] == 0x00) {
		err = avc_stream_parse_format(&frame[10], &formation);
		if (err < 0)
			goto rejected;

		am->opcr[index].rate = formation->rate;;
		am->opcr[index].pcm_channels = formation->pcm;

		frame[0] = 0x09;	/* Accepted. */
	/* Status. */
	} else if (frame[0] == 0x01) {
		for (i = 0; i < ARRAY_SIZE(avc_stream_rate_table); ++i) {
			if (am->opcr[index].rate == avc_stream_rate_table[i])
				break;
		}
		if (i == ARRAY_SIZE(avc_stream_rate_table))
			goto rejected;

		frame[9] = 0x00;	/* Support_status is active. */
		frame[10] = 0x90;	/* Root is Audio and Music. */
		frame[11] = 0x40;	/* Level 1 is AM824 compound. */
		frame[12] = avc_stream_rate_codes[i];
		frame[13] = 0x02;	/* Command rate ctl is not supported. */
		frame[14] = 0x01;	/* One entry. */
		frame[15] = am->opcr[index].pcm_channels;
		frame[16] = 0x06;	/* Multi bit linear audio (raw). */

		frame[0] = 0x0c;	/* Implemented/stable */
	} else {
		goto not_implemented;
	}

rejected:
	frame[0] = 0x0a;
	return;
not_implemented:
	frame[0] = 0x08;
}

/* TODO: remove callback. */
static void response_callback(struct fw_card *card, int rcode,
			      void *payload, size_t length, void *data)
{
	struct fw_am_unit *am = data;
	struct fcp_transaction *transaction = data;

	/* TODO: check error. */
	if (rcode == RCODE_COMPLETE)
		transaction->state = STATE_IDLE;
	else if (rcode == RCODE_TYPE_ERROR || rcode == RCODE_ADDRESS_ERROR)
		/* To start next transaction immediately for recovery. */
		schedule_work(&am->fcp_work);
}

static void handle_request(struct work_struct *work)
{
	struct fw_am_unit *am = container_of(work, struct fw_am_unit, fcp_work);
	struct fw_device *fw_dev = fw_parent_device(am->unit);
	struct fcp_transaction *t, *transactions;
	int generation;
	int i;

	mutex_lock(&am->transactions_mutex);

	for (i = 0; i < TRANSACTION_SLOTS; i++) {
		transactions = am->transactions;
		t = &transactions[i];
		if (t->state != STATE_PENDING)
			continue;

		/* Bus reset occurs perhaps. */
		if (t->generation != fw_dev->generation)
			continue;

		t->state = STATE_WAITING;

		switch (t->frame[2]) {
		case 0x02:
			handle_avc_plug_info(am, t->frame, t->size);
			break;
		case 0x30:	/* Unit info */
			handle_avc_unit_info(am, t->frame, t->size);
		case 0x18:	/* Output signal format */
			handle_avc_out_signal_format(am, t->frame, t->size);
			break;
		case 0x31:	/* Subunit info */
		case 0x19:	/* Input signal format */
		default:
			t->frame[0] = 0x08;	/* Not implemented */
			break;
		}

		/* The generation is updated after destination. */
		generation = fw_dev->generation;
		smp_rmb();

		fw_send_request(t->card, &t->request,
				TCODE_WRITE_BLOCK_REQUEST,
				t->destination, generation,
				t->card->link_speed,
				CSR_REGISTER_BASE + CSR_FCP_RESPONSE,
				t->frame, t->size, response_callback, t);
	}

	mutex_unlock(&am->transactions_mutex);
}

static void handle_fcp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	struct fw_am_unit *am = callback_data;
	struct fcp_transaction *t, *transactions;
	bool queued;
	unsigned int i;

	/* The address for FCP command is fixed. */
	if (offset != CSR_REGISTER_BASE + CSR_FCP_COMMAND)
		return;
	if (tcode != TCODE_WRITE_BLOCK_REQUEST)
		return;

	mutex_lock(&am->transactions_mutex);

	queued = false;
	for (i = 0; i < TRANSACTION_SLOTS; i++) {
		transactions = am->transactions;
		t = &transactions[i];

		if (t->state != STATE_IDLE)
			continue;

		t->state = STATE_PENDING;
		t->card = card;
		t->destination = source;
		t->generation = generation;
		memset(t->frame, 0, TRANSACTION_FRAME_MAX_SIZE);
		memcpy(t->frame, data, length);
		t->size = length;

		queued = true;
	}

	mutex_unlock(&am->transactions_mutex);

	/* This work should start after the response. So wait 20 msec. */
	if (queued)
		schedule_work(&am->fcp_work);
}

int fw_am_unit_fcp_init(struct fw_am_unit *am)
{
	static const struct fw_address_region fcp_register_region = {
		.start = CSR_REGISTER_BASE + CSR_FCP_COMMAND,
		.end = CSR_REGISTER_BASE + CSR_FCP_RESPONSE,
	};
	int err;

	am->transactions = kmalloc_array(sizeof(struct fcp_transaction),
					 TRANSACTION_SLOTS, GFP_KERNEL);
	if (am->transactions == NULL)
		return -ENOMEM;

	am->fcp_handler.length = CSR_FCP_RESPONSE - CSR_FCP_COMMAND;
	am->fcp_handler.address_callback = handle_fcp;
	err = fw_core_add_address_handler(&am->fcp_handler,
					  &fcp_register_region);
	if (err < 0) {
		kfree(am->transactions);
		return err;
	}

	INIT_WORK(&am->fcp_work, handle_request);
	mutex_init(&am->transactions_mutex);

	return 0;
}

void fw_am_unit_fcp_update(struct fw_am_unit *am)
{
	struct fcp_transaction *t, *transactions;
	unsigned int i;

	for (i = 0; i < TRANSACTION_SLOTS; i++) {
		transactions = am->transactions;
		t = &transactions[i];
		if (t->state == STATE_PENDING)
			t->state = STATE_IDLE;
	}
}

void fw_am_unit_fcp_destroy(struct fw_am_unit *am)
{
	cancel_work_sync(&am->fcp_work);

	fw_core_remove_address_handler(&am->fcp_handler);
}
