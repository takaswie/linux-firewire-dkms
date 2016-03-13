/*
 * am-cmp.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"

static DEFINE_SPINLOCK(instance_list_lock);
static LIST_HEAD(instance_list);

static void handle_cmp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	struct fw_am_unit *am;
	int rcode;

	/* Seek an instance to which this request is sent. */
	spin_lock(&instance_list_lock);
	list_for_each_entry(am, &instance_list, list_for_cmp) {
		if (fw_parent_device(am->unit)->card == card)
			break;
	}
	spin_unlock(&instance_list_lock);
	if (am == NULL)
		rcode = RCODE_ADDRESS_ERROR;
	else
		rcode = RCODE_DATA_ERROR;

	fw_send_response(card, request, rcode);
}

int fw_am_unit_cmp_register(struct fw_am_unit *am)
{
	spin_lock(&instance_list_lock);
	list_add_tail(&am->list_for_cmp, &instance_list);
	spin_unlock(&instance_list_lock);

	return 0;
}

void fw_am_unit_cmp_update(struct fw_am_unit *am)
{
	return;
}

void fw_am_unit_cmp_unregister(struct fw_am_unit *am)
{
	spin_lock(&instance_list_lock);
	list_del(&am->list_for_cmp);
	spin_unlock(&instance_list_lock);
}

static struct fw_address_handler cmp_handler = {
	.length = CSR_OPCR(OHCI1394_MIN_TX_CTX) - CSR_OMPR,
	.address_callback = handle_cmp,
};

int fw_am_cmp_init(void)
{
	static const struct fw_address_region cmp_register_region = {
		.start = CSR_REGISTER_BASE + CSR_OMPR,
		.end = CSR_REGISTER_BASE + CSR_OPCR(OHCI1394_MIN_TX_CTX),
	};

	return fw_core_add_address_handler(&cmp_handler, &cmp_register_region);
}

void fw_am_cmp_destroy(void)
{
	fw_core_remove_address_handler(&cmp_handler);
}
