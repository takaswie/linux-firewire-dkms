#include "bebob.h"
#include <sound/core.h>

struct snd_bebob_t {
	struct snd_card *card;
	struct fw_device *device;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;
};

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

static void
maudio_proving(struct fw_unit *unit)
{
	char name[16] = {0};
	__be32 data[4] = {0};
	int err;

	/* get current model name */
	err = fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name));
	if (err < 0)
		return;
	name[15] = '\0';
	snd_printk(KERN_INFO "BeBoB name: %s\n", name);

	/* get chip vendor name */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				0xFFFFC8020000, data, 8);
	if (err < 0)
		return;
	snd_printk("chip vendor: %08X %08X\n", data[0], data[1]);

	/* get bootloader timestamp */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				0xFFFFC8020020, data, 16);
	if (err < 0)
		return;
	snd_printk("bootloader timestamp: %08X %08X %08X %08X\n", data[0], data[1], data[2], data[3]);

	/* get firmware timestamp */
	err = snd_fw_transaction(unit, TCODE_READ_BLOCK_REQUEST,
				0xFFFFC8020040, data, 16);
	if (err < 0)
		return;
	snd_printk("firmware timestamp: %08X %08X %08X %08X\n", data[0], data[1], data[2], data[3]);

	return;
}

static void
snd_bebob_update(struct fw_unit *unit)
{
	struct snd_card *card = dev_get_drvdata(&unit->device);
	struct snd_bebob_t *bebob = card->private_data;

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
	struct snd_bebob_t *bebob = card->private_data;

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
	struct snd_bebob_t *bebob;
	int err;

	mutex_lock(&devices_mutex);

	/* TODO: startup firmware if it's M-Audio */
	maudio_proving(unit);

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
			THIS_MODULE, sizeof(struct snd_bebob_t), &card);
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

	/* register card and device */
	snd_card_set_dev(card, dev);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		goto end;
	}

	dev_set_drvdata(dev, card);
	devices_used |= 1 << card_index;
	bebob->card_index = card_index;

	/* proved */
	err = 0;

end:
	mutex_unlock(&devices_mutex);
	return err;
}

static int
snd_bebob_remove(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_bebob_t *bebob = card->private_data;

	/* do something */
	snd_printk(KERN_INFO "BeBoB removed\n");

	snd_card_disconnect(card);
	snd_card_free_when_closed(card);

	return 0;
}

#define VENDOR_MAUDIO_1ST			0x000007f5
#define VENDOR_MAUDIO_2ND			0x00000d6c
#define MODEL_MAUDIO_FW_410			0x00010046
#define MODEL_MAUDIO_OZONIC			0x0000000A
#define MODEL_MAUDIO_FW_BOOTLOADER		0x00010058
#define MODEL_MAUDIO_AUDIOPHILE_BOOTLOADER	0x00010060
#define MODEL_MAUDIO_SOLO			0x00010062
#define SPECIFIER_1394TA			0x0000a02d

static const struct ieee1394_device_id snd_bebob_id_table[] = {
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO_1ST,
		.model_id	= MODEL_MAUDIO_FW_410,
		.specifier_id	= SPECIFIER_1394TA,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO_1ST,
		.model_id	= MODEL_MAUDIO_FW_BOOTLOADER,
		.specifier_id	= SPECIFIER_1394TA,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO_2ND,
		.model_id	= MODEL_MAUDIO_OZONIC,
		.specifier_id	= SPECIFIER_1394TA,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO_2ND,
		.model_id	= MODEL_MAUDIO_AUDIOPHILE_BOOTLOADER,
		.specifier_id	= SPECIFIER_1394TA,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO_2ND,
		.model_id	= MODEL_MAUDIO_SOLO,
		.specifier_id	= SPECIFIER_1394TA,
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
