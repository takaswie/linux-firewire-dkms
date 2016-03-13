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

static void handle_fcp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	/* Linux FireWire subsystem already responds this request. */
}

int fw_am_unit_fcp_register(struct fw_am_unit *am)
{
	spin_lock(&instance_list_lock);
	list_add_tail(&am->list_for_fcp, &instance_list);
	spin_unlock(&instance_list_lock);

	return 0;
}

void fw_am_unit_fcp_update(struct fw_am_unit *am)
{
	return;
}

void fw_am_unit_fcp_unregister(struct fw_am_unit *am)
{
	spin_lock(&instance_list_lock);
	list_del(&am->list_for_fcp);
	spin_unlock(&instance_list_lock);
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
