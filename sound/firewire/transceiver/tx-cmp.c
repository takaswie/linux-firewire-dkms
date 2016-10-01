/*
 * tx-cmp.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"
#include "../cmp.h"

static int handle_conn_req(struct fw_am_unit *am, unsigned int index,
			   __be32 *quad)
{
	u32 curr = be32_to_cpu(quad[0]);
	u32 new = be32_to_cpu(quad[1]);
	unsigned int spd, xspd, isoc_ch;
	int err;

	if (curr != am->opcr[index].reg)
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

	am->opcr[index].reg = new;
	quad[0] = cpu_to_be32(new);

	return RCODE_COMPLETE;
}

static void handle_cmp(struct fw_card *card, struct fw_request *request,
		       int tcode, int destination, int source,
		       int generation, unsigned long long offset,
		       void *data, size_t length, void *callback_data)
{
	struct fw_am_unit *am = callback_data;
	__be32 *quad = data;
	unsigned int index;
	int rcode = RCODE_COMPLETE;

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
			*quad = cpu_to_be32(am->opcr[index - 1].reg);
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
	struct pcr_resource *opcr;
	unsigned int payload;
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		opcr = &am->opcr[i];
		opcr->rate = 44100;
		opcr->pcm_channels = 2;
		amdtp_am824_set_parameters(&opcr->stream, 44100, 2, 8, false);
		payload = amdtp_stream_get_max_payload(&opcr->stream);
		opcr->reg |= ((1 << PCR_ONLINE_SHIFT) & PCR_ONLINE) | payload;
	}
}

int fw_am_unit_cmp_init(struct fw_am_unit *am)
{
	static const struct fw_address_region cmp_register_region = {
		.start = CSR_REGISTER_BASE + CSR_OMPR,
		.end = CSR_REGISTER_BASE + CSR_OPCR(OHCI1394_MIN_TX_CTX),
	};
	int err;

	initialize_ompr(am);
	initialize_opcrs(am);

	am->cmp_handler.length = CSR_OPCR(OHCI1394_MIN_TX_CTX) - CSR_OMPR;
	am->cmp_handler.address_callback = handle_cmp;
	am->cmp_handler.callback_data = am;
	err = fw_core_add_address_handler(&am->cmp_handler,
					  &cmp_register_region);
	if (err < 0)
		return err;

	return 0;
}

void fw_am_unit_cmp_update(struct fw_am_unit *am)
{
	unsigned int i;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++)
		am->opcr[i].reg &= 0x80ffc3ff;
}

void fw_am_unit_cmp_destroy(struct fw_am_unit *am)
{
	fw_core_remove_address_handler(&am->cmp_handler);
}
