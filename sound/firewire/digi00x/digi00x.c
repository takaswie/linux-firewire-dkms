/*
 * digi00x.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

MODULE_DESCRIPTION("Digidesign 002/003 Driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

#define VENDOR_DIGIDESIGN	0x00a07e
#define MODEL_DIGI00X		0x000002

static void handle_message(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *data, size_t length, void *callback_data)
{
	int rcode;
	u32 *buf = (__be32 *)data;

	rcode = RCODE_COMPLETE;

	/* NOTE: offset may be one of 0x00 or 0x08. */
	snd_printk(KERN_INFO"%08llx: %08x\n", offset, be32_to_cpu(*buf));

	fw_send_response(card, request, rcode);
}

static void snd_dg00x_message_unregister(struct snd_dg00x *dg00x)
{
	if (dg00x->message_handler.offset)
		fw_core_remove_address_handler(&dg00x->message_handler);
	dg00x->message_handler.offset = 0;
}

static int snd_dg00x_message_register(struct snd_dg00x *dg00x)
{
	static const struct fw_address_region resp_register_region = {
		.start	= 0xffff00000000ull,
		.end	= 0xffff0000ffffull,
	};
	struct fw_device *device = fw_parent_device(dg00x->unit);
	__be32 data[2];
	int err;

	dg00x->message_handler.length = 8;
	dg00x->message_handler.address_callback = handle_message;
	dg00x->message_handler.callback_data = (void *)dg00x;

	err = fw_core_add_address_handler(&dg00x->message_handler,
					  &resp_register_region);
	if (err < 0)
		goto end;

	/* Unknown. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (dg00x->message_handler.offset >> 32));
	data[1] = cpu_to_be32(dg00x->message_handler.offset);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				 0xffffe0000008ull, &data, sizeof(data), 0);
	if (err < 0)
		goto end;

	/* For 0x7051/0x7058 message. Unknown. */
	data[0] = cpu_to_be32((device->card->node_id << 16) |
			      (dg00x->message_handler.offset >> 32));
	data[1] = cpu_to_be32(dg00x->message_handler.offset + 4);
	err = snd_fw_transaction(dg00x->unit, TCODE_WRITE_BLOCK_REQUEST,
				 0xffffe0000014ull, &data, sizeof(data), 0);
end:
	if (err < 0)
		snd_dg00x_message_unregister(dg00x);
	return err;
}

static int name_card(struct snd_dg00x *dg00x)
{
	struct fw_device *fw_dev = fw_parent_device(dg00x->unit);
	char name[32] = {0};
	char *model;
	int err;

	err = fw_csr_string(dg00x->unit->directory, CSR_MODEL, name,
			    sizeof(name));
	if (err < 0)
		goto end;

	model = name;
	if (model[0] == ' ')
		model = strchr(model, ' ') + 1;
	
	strcpy(dg00x->card->driver, "Digi00x");
	strcpy(dg00x->card->shortname, model);
	strcpy(dg00x->card->mixername, model);
	snprintf(dg00x->card->longname, sizeof(dg00x->card->longname),
		 "Digidesign %s, GUID %08x%08x at %s, S%d", model,
		 cpu_to_be32(fw_dev->config_rom[3]),
		 cpu_to_be32(fw_dev->config_rom[4]),
		 dev_name(&dg00x->unit->device), 100 << fw_dev->max_speed);
end:
	return err;
}

static void dg00x_card_free(struct snd_card *card)
{
	struct snd_dg00x *dg00x = card->private_data;

	snd_dg00x_stream_destroy_duplex(dg00x);
	snd_dg00x_message_unregister(dg00x);

	fw_unit_put(dg00x->unit);

	mutex_destroy(&dg00x->mutex);

	return;
}

static int snd_dg00x_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_dg00x *dg00x;
	int err;

	/* create card */
	err = snd_card_new(&unit->device, -1, NULL, THIS_MODULE,
			   sizeof(struct snd_dg00x), &card);
	if (err < 0)
		return err;
	card->private_free = dg00x_card_free;

	/* initialize myself */
	dg00x = card->private_data;
	dg00x->card = card;
	dg00x->unit = fw_unit_get(unit);

	mutex_init(&dg00x->mutex);
	spin_lock_init(&dg00x->lock);
	init_waitqueue_head(&dg00x->hwdep_wait);

	err = snd_dg00x_message_register(dg00x);
	if (err < 0)
		goto error;

	err = name_card(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_create_hwdep_device(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_create_midi_devices(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_create_pcm_devices(dg00x);
	if (err < 0)
		goto error;

	err = snd_dg00x_stream_init_duplex(dg00x);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, dg00x);

	return err;
error:
	snd_card_free(card);
	return err;
}

static void snd_dg00x_update(struct fw_unit *unit)
{
	struct snd_dg00x *dg00x = dev_get_drvdata(&unit->device);

	mutex_lock(&dg00x->mutex);
	snd_dg00x_stream_update_duplex(dg00x);
	mutex_unlock(&dg00x->mutex);
}

static void snd_dg00x_remove(struct fw_unit *unit)
{
	struct snd_dg00x *dg00x = dev_get_drvdata(&unit->device);

	/* No need to wait for releasing card object in this context. */
	snd_card_free_when_closed(dg00x->card);
}


static const struct ieee1394_device_id snd_dg00x_id_table[] =
{
	/* Both of 002/003 use the same ID. */
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_MODEL_ID,
		.vendor_id = VENDOR_DIGIDESIGN,
		.model_id = MODEL_DIGI00X,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_dg00x_id_table);

static struct fw_driver dg00x_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-digi00x",
		.bus = &fw_bus_type,
	},
	.probe    = snd_dg00x_probe,
	.update   = snd_dg00x_update,
	.remove   = snd_dg00x_remove,
	.id_table = snd_dg00x_id_table,
};

static int __init snd_dg00x_init(void)
{
	return driver_register(&dg00x_driver.driver);
}

static void __exit snd_dg00x_exit(void)
{
	driver_unregister(&dg00x_driver.driver);
}

module_init(snd_dg00x_init);
module_exit(snd_dg00x_exit);
