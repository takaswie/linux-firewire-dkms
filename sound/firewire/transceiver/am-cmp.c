/*
 * am-cmp.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "am-unit.h"
#include "../cmp.h"

static DEFINE_SPINLOCK(instance_list_lock);
static LIST_HEAD(instance_list);

static int handle_conn_req(struct fw_am_unit *am, unsigned int index,
			   __be32 *quad)
{
	u32 curr = be32_to_cpu(quad[0]);
	u32 new = be32_to_cpu(quad[1]);
	unsigned int spd, xspd, isoc_ch;
	int err;

	if (curr != am->opcr[index])
		return RCODE_DATA_ERROR;

	/* Check speed. */
	xspd = (new & OPCR_XSPEED_MASK) >> OPCR_XSPEED_SHIFT;
	spd = (new & OPCR_SPEED_MASK) >> OPCR_SPEED_SHIFT;
	if (xspd > 0 && spd != SCODE_BETA)
		return RCODE_DATA_ERROR;

	spd += xspd;
	if (spd > fw_parent_device(am->unit)->max_speed)
		return RCODE_DATA_ERROR;

	/* Check peer-to-peer connection. */
	if ((curr & PCR_P2P_CONN_MASK) == 0 && (new & PCR_P2P_CONN_MASK) == 1) {
		/* Peer should reserve the isochronous resources. */
		isoc_ch = (new & PCR_CHANNEL_MASK) >> PCR_CHANNEL_SHIFT;

		mutex_lock(&am->mutex);
		err = fw_am_unit_stream_start(am, index, isoc_ch, spd);
		mutex_unlock(&am->mutex);
		if (err < 0)
			return RCODE_CONFLICT_ERROR;
	} else if ((curr & PCR_P2P_CONN_MASK) == 1 &&
		   (new & PCR_P2P_CONN_MASK) == 0) {
		mutex_lock(&am->mutex);
		fw_am_unit_stream_stop(am, index);
		mutex_unlock(&am->mutex);
	} else {
		return RCODE_DATA_ERROR;
	}

	am->opcr[index] = new;
	quad[0] = cpu_to_be32(new);

	return RCODE_COMPLETE;
}

static void handle_cmp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	struct fw_am_unit *am;
	__be32 *quad = data;
	unsigned int index;
	int rcode = RCODE_COMPLETE;

	/* Seek an instance to which this request is sent. */
	spin_lock(&instance_list_lock);
	list_for_each_entry(am, &instance_list, list_for_cmp) {
		if (fw_parent_device(am->unit)->card == card)
			break;
	}
	spin_unlock(&instance_list_lock);
	if (am == NULL) {
		rcode = RCODE_ADDRESS_ERROR;
		goto end;
	}

	/* Address should be aligned by quadlet. */
	if (offset & 0x03) {
		rcode = RCODE_ADDRESS_ERROR;
		goto end;
	} else if (tcode != TCODE_READ_QUADLET_REQUEST &&
		   tcode != TCODE_LOCK_COMPARE_SWAP) {
		rcode = RCODE_TYPE_ERROR;
		goto end;
	}

	index = offset - (CSR_REGISTER_BASE + CSR_OMPR);
	if (index == 0) {
		if (tcode == TCODE_READ_QUADLET_REQUEST)
			*quad = cpu_to_be32(am->ompr);
		else
			rcode = RCODE_DATA_ERROR;
	} else {
		if (tcode == TCODE_READ_QUADLET_REQUEST)
			*quad = cpu_to_be32(am->opcr[index - 1]);
		else
			rcode = handle_conn_req(am, index - 1, quad);
	}
end:
	fw_send_response(card, request, rcode);
}

/* According to IEC 61883-1:2008. */
static void initialize_ompr(struct fw_am_unit *am)
{
	struct fw_device *fw_dev = fw_parent_device(am->unit);

	am->ompr = (fw_dev->max_speed << MPR_SPEED_SHIFT) & MPR_SPEED_MASK;
	if (fw_dev->max_speed > SCODE_BETA)
		am->ompr |= (fw_dev->max_speed << MPR_XSPEED_SHIFT) &
								MPR_PLUGS_MASK;
	am->ompr |= OHCI1394_MIN_TX_CTX & MPR_PLUGS_MASK;
}

static void initialize_opcrs(struct fw_am_unit *am)
{
	unsigned int payload;
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		payload = amdtp_stream_get_max_payload(&am->tx_streams[i]);
		am->opcr[i] |= ((1 << PCR_ONLINE_SHIFT) & PCR_ONLINE) | payload;
	}
}

int fw_am_unit_cmp_register(struct fw_am_unit *am)
{
	initialize_ompr(am);
	initialize_opcrs(am);

	spin_lock(&instance_list_lock);
	list_add_tail(&am->list_for_cmp, &instance_list);
	spin_unlock(&instance_list_lock);

	return 0;
}

void fw_am_unit_cmp_update(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		am->opcr[i] &= 0x80ffc3ff;
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
