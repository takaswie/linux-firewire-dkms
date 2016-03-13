/*
 * am-fcp.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"

static DEFINE_SPINLOCK(instance_list_lock);
static LIST_HEAD(instance_list);

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
		t->frame[0] = 0x08;	/* Not implemented */

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
	struct fw_am_unit *am;
	struct fcp_transaction *t, *transactions;
	bool queued;
	unsigned int i;

	/* Seek an instance to which this request is sent. */
	spin_lock(&instance_list_lock);
	list_for_each_entry(am, &instance_list, list_for_fcp) {
		if (fw_parent_device(am->unit)->card == card)
			break;
	}
	spin_unlock(&instance_list_lock);
	if (am == NULL) {
		/* Linux FireWire subsystem already responds this request. */
		return;
	}

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

int fw_am_unit_fcp_register(struct fw_am_unit *am)
{
	am->transactions = kmalloc_array(sizeof(struct fcp_transaction),
					 TRANSACTION_SLOTS, GFP_KERNEL);
	if (am->transactions == NULL)
		return -ENOMEM;

	INIT_WORK(&am->fcp_work, handle_request);
	mutex_init(&am->transactions_mutex);

	spin_lock(&instance_list_lock);
	list_add_tail(&am->list_for_fcp, &instance_list);
	spin_unlock(&instance_list_lock);

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

void fw_am_unit_fcp_unregister(struct fw_am_unit *am)
{
	spin_lock(&instance_list_lock);
	list_del(&am->list_for_fcp);
	spin_unlock(&instance_list_lock);

	cancel_work_sync(&am->fcp_work);
}

static struct fw_address_handler fcp_handler = {
	.length = CSR_FCP_RESPONSE - CSR_FCP_COMMAND,
	.address_callback = handle_fcp,
};

int fw_am_fcp_init(void)
{
	static const struct fw_address_region fcp_register_region = {
		.start = CSR_REGISTER_BASE + CSR_FCP_COMMAND,
		.end = CSR_REGISTER_BASE + CSR_FCP_RESPONSE,
	};

	return fw_core_add_address_handler(&fcp_handler, &fcp_register_region);
}

void fw_am_fcp_destroy(void)
{
	fw_core_remove_address_handler(&fcp_handler);
}
