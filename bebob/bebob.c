#include "bebob.h"
#include <sound/core.h>

#define VENDOR_MAUDIO	0x00000d6c

MODULE_DESCRIPTION("bridgeCo BeBoB driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable BeBoB sound card");

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

/*
 * return value:
 *  < 0: error code
 *  = 0: now loading
 *  > 0: loaded
 */
static int
snd_bebob_loader(struct fw_unit *unit)
{
	struct fw_device *fw_dev = fw_parent_device(unit);
	struct fw_csr_iterator i;
	int key, value;

	fw_csr_iterator_init(&i, fw_dev->config_rom);
	while (fw_csr_iterator_next(&i, &key, &value)) {
		if (key == CSR_VENDOR) {
			switch (value) {
			case VENDOR_MAUDIO:
				return snd_bebob_maudio_detect(unit);
			}
		}
	}
	return -1;
}

static int
snd_bebob_get_hardware_info(struct snd_bebob *bebob)
{
	struct snd_bebob_device_info info = {0};

	char vendor[24];
	char model[24];
	u32 id;
	u32 data[2];
	u32 revision;
	int err = 0;

	/* get vendor name
	err = fw_csr_string(bebob->unit->directory,
			    CSR_VENDOR, vendor, 24);
	if (err < 0)
		goto end;*/
	strcpy(vendor, "M-Audio");

	/* get hardware revision */
	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
				BEBOB_ADDR_REG_INFO, &info, sizeof(info));
	if (err < 0)
		goto end;

	/* get model name */
	err = fw_csr_string(bebob->unit->directory,
			    CSR_MODEL, model, 24);
	if (err < 0)
		goto end;

	/* get hardware id */
	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
			0xffffc8020018, &id, 4);
	if (err < 0)
		goto end;

	/* get hardware revision */
	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
			0xffffc802001c, &revision, 4);
	if (err < 0)
		goto end;

	/* get GUID */
	err = snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
			0xffffc8020010, data, 8);
	if (err < 0)
		goto end;

	strcpy(bebob->card->driver, "BeBoB");
	strcpy(bebob->card->shortname, model);
	snprintf(bebob->card->longname, sizeof(bebob->card->longname),
		"%s %s (%d) r%d, GUID %08x%08x at %s, S%d",
		vendor, model,
		id, revision,
		data[0], data[1],
		dev_name(&bebob->unit->device), 100 << bebob->device->max_speed);

	/* TODO: set hardware specification */
	bebob->pcm_capture_channels = 4;
	bebob->pcm_playback_channels = 4;

end:
	return err;
}

static void
snd_bebob_update(struct fw_unit *unit)
{
	struct snd_card *card = dev_get_drvdata(&unit->device);
	struct snd_bebob *bebob = card->private_data;

	fcp_bus_reset(bebob->unit);

	/* bus reset for isochronous transmit stream */
//	if (cmp_connection_update(&efw->receive_stream.conn) < 0) {
//		amdtp_out_stream_pcm_abort(&efw->receive_stream.strm);
//		mutex_lock(&efw->mutex);
//		snd_efw_stream_stop(&efw->receive_stream);
//		mutex_unlock(&efw->mutex);
//	}
//	amdtp_out_stream_update(&efw->receive_stream.strm);

	/* bus reset for isochronous receive stream */
//	if (cmp_connection_update(&efw->transmit_stream.conn) < 0) {
//		amdtp_out_stream_pcm_abort(&efw->transmit_stream.strm);
//		mutex_lock(&efw->mutex);
//		snd_efw_stream_stop(&efw->transmit_stream);
//		mutex_unlock(&efw->mutex);
//	}
//	amdtp_out_stream_update(&efw->transmit_stream.strm);

	return;
}

static void
snd_bebob_card_free(struct snd_card *card)
{
	struct snd_bebob *bebob = card->private_data;

	if (bebob->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~(1 << bebob->card_index);
		mutex_unlock(&devices_mutex);
	}

	mutex_destroy(&bebob->mutex);

	return;
}

static int
snd_bebob_probe(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	int card_index;
	struct snd_card *card;
	struct snd_bebob *bebob;
	int err = 0;

	mutex_lock(&devices_mutex);

	/* some M-Audio devices need cue to load firmware, then bus reset */
	if (snd_bebob_loader(unit) <= 0 )
		goto end;

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; card_index += 1) {
		if (!(devices_used & (1 << card_index)) && enable[card_index])
			break;
	}
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	/* create card */
	err = snd_card_create(index[card_index], id[card_index],
			THIS_MODULE, sizeof(struct snd_bebob), &card);
	if (err < 0)
		goto end;
	card->private_free = snd_bebob_card_free;

	/* initialize myself */
	bebob = card->private_data;
	bebob->card = card;
	bebob->device = fw_parent_device(unit);
	bebob->unit = unit;
	bebob->card_index = -1;
	mutex_init(&bebob->mutex);
	spin_lock_init(&bebob->lock);
	bebob->loaded = false;

	/* retrieve hardware information */
	err = snd_bebob_get_hardware_info(bebob);
	if (err < 0)
		goto error;

	/* create devices */
	err = snd_bebob_create_pcm_devices(bebob);
	if (err < 0)
		goto error;

	/* register card and device */
	snd_card_set_dev(card, dev);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		goto error;
	}

	dev_set_drvdata(dev, card);
	devices_used |= 1 << card_index;
	bebob->card_index = card_index;

	/* proved */
	goto end;

error:
	snd_card_free(card);

end:
	mutex_unlock(&devices_mutex);
	return err;
}

static int
snd_bebob_remove(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_bebob *bebob = card->private_data;

	if (!card)
		goto end;

	/* destroy devices */
	snd_bebob_destroy_pcm_devices(bebob);

	/* do something */
	snd_printk(KERN_INFO "BeBoB removed\n");

	snd_card_disconnect(card);
	snd_card_free_when_closed(card);

end:
	return 0;
}

#define MODEL_MAUDIO_OZONIC			0x0000000A
#define MODEL_MAUDIO_FW_BOOTLOADER		0x00010058
#define MODEL_MAUDIO_FW_410			0x00010046
#define MODEL_MAUDIO_AUDIOPHILE_BOTH		0x00010060
#define MODEL_MAUDIO_SOLO			0x00010062
#define MODEL_MAUDIO_FW_1814_BOOTLOADER		0x00010070
#define MODEL_MAUDIO_FW_1814			0x00010071

static const struct ieee1394_device_id snd_bebob_id_table[] = {
	/* Ozonic has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_OZONIC,
	},
	/* Firewire 410 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_FW_BOOTLOADER,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_FW_410,
	},
	/* Firewire Audiophile has one ID for both bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_AUDIOPHILE_BOTH,
	},
	/* Firewire Solo has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_SOLO,
	},
	/* Firewire 1814 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_FW_1814_BOOTLOADER,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO,
		.model_id	= MODEL_MAUDIO_FW_1814,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_bebob_id_table);

static struct fw_driver snd_bebob_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-bebob",
		.bus	= &fw_bus_type,
		.probe	= snd_bebob_probe,
		.remove	= snd_bebob_remove,
	},
	.update	= snd_bebob_update,
	.id_table = snd_bebob_id_table,
};

static int __init
snd_bebob_init(void)
{
	return driver_register(&snd_bebob_driver.driver);
}

static void __exit
snd_bebob_exit(void)
{
	driver_unregister(&snd_bebob_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_bebob_init);
module_exit(snd_bebob_exit);
