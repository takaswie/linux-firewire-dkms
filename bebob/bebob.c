#include "bebob.h"
#include <sound/core.h>

MODULE_DESCRIPTION("bridgeCo BeBoB driver");
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable BeBoB sound card");

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

int sampling_rate_table[SND_BEBOB_STREAM_FORMATION_ENTRIES] = {
	[0] = 22050,
	[1] = 24000,
	[2] = 32000,
	[3] = 44100,
	[4] = 48000,
	[5] = 88200,
	[6] = 96000,
	[7] = 176400,
	[8] = 192000,
};

int snd_bebob_get_formation_index(int sampling_rate)
{
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		if (sampling_rate_table[i] == sampling_rate)
			return i;
	}
	return -1;
}

static void set_stream_formation(u8 *buf, int len,
		     struct snd_bebob_stream_formation *formation)
{
	int channels, format;
	int e;

	for (e = 0; e < buf[4]; e += 1) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* PCM for IEC 60958-3 */
		case 0x00:
		/* PCM for IEC 61883-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* PCM for Multi bit linear audio */
		case 0x06:
		case 0x07:
			formation->pcm += channels;
			break;
		/* MIDI comformant (MMA/AMEI RP-027) */
		case 0x0d:
			formation->midi += channels;
			break;
		default:
			break;
		}
	}

	/* store this entry for future use */
	memcpy(formation->entry, buf, len);

	return;
}

/* 128 is an arbitrary number but it's enough */
#define FORMATION_MAXIMUM_LENGTH 128

static int fill_stream_formations(struct snd_bebob *bebob,
				  int direction, unsigned short plugid)
{
	int freq_table[] = {
		[0x00] = 0,	/*  22050 */
		[0x01] = 1,	/*  24000 */
		[0x02] = 2,	/*  32000 */
		[0x03] = 3,	/*  44100 */
		[0x04] = 4,	/*  48000 */
		[0x05] = 6,	/*  96000 */
		[0x06] = 7,	/* 176400 */
		[0x07] = 8,	/* 192000 */
		[0x0a] = 5	/*  88200 */
	};

	u8 *buf;
	struct snd_bebob_stream_formation *formations;
	int index;
	int len;
	int e, i;
	int err;

	buf = kmalloc(FORMATION_MAXIMUM_LENGTH, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (direction > 0)
		formations = bebob->rx_stream_formations;
	else
		formations = bebob->tx_stream_formations;

	for (e = 0, index = 0; e < 9; e += 1) {
		len = FORMATION_MAXIMUM_LENGTH;
		/* get entry */
		memset(buf, 0, len);
		err = avc_bridgeco_get_plug_stream_formation_entry(bebob->unit, direction, plugid, e, buf, &len);
		if (err < 0)
			goto end;
		/* reach the end of entries */
		else if (buf[0] != 0x0c)
			break;
		/*
		 * this module can support a hierarchy combination that:
		 *  Root:	Audio and Music (0x90)
		 *  Level 1:	AM824 Compound  (0x40)
		 */
		else if ((buf[11] != 0x90) || (buf[12] != 0x40))
			break;
		/* check formation length */
		else if (len < 1)
			break;

		/* formation information includes own value for sampling rate. */
		if ((buf[13] > 0x07) && (buf[13] != 0x0a))
			break;
		for (i = 0; i < sizeof(freq_table); i += 1) {
			if (i == buf[13])
				index = freq_table[i];
		}

		/* parse and set stream formation */
		set_stream_formation(buf + 11, len - 11, &formations[index]);
	};

	err = 0;

end:
	kfree(buf);
	return err;
}

/* In this function, 2 means input and output */
static int detect_channels(struct snd_bebob *bebob)
{
	/* the number of plugs for input and output */
	unsigned short bus_plugs[2];
	unsigned short ext_plugs[2];
	int type, i;
	int err;

	err = avc_generic_get_plug_info(bebob->unit, bus_plugs, ext_plugs);
	if (err < 0)
		goto end;

	/*
	 * This module supports one PCR input plug and one PCR output plug
	 * then ignores the others.
	 */
	for (i = 0; i < 2; i += 1) {
		if (bus_plugs[i]  == 0) {
			err = -EIO;
			goto end;
		}

		err = avc_bridgeco_get_plug_type(bebob->unit, i, 0, &type);
		if (err < 0)
			goto end;
		else if (type != 0x00) {
			err = -EIO;
			goto end;
		}
	}

	/* fill sampling rates in stream_formations */
	for (i = 0; i < 9; i += 1) {
		bebob->tx_stream_formations[i].sampling_rate =
		bebob->rx_stream_formations[i].sampling_rate =
		sampling_rate_table[i];
	}

	/* store formations */
	for (i = 0; i < 2; i += 1) {
		err = fill_stream_formations(bebob, i, 0);
		if (err < 0)
			goto end;
	}

	err = 0;

end:
	return err;
}

static int name_device(struct snd_bebob *bebob)
{
	struct snd_bebob_device_info info = {0};

	char vendor[24];
	char model[24];
	u32 id;
	u32 data[2];
	u32 revision;
	int err = 0;

	/* get vendor name
	err = fw_csr_string(bebob->unit->directory, CSR_VENDOR, vendor, 24);
	if (err < 0)
		goto end;
	*/
strcpy(vendor, "takaswie:-)");

	/* get hardware revision */
	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
				BEBOB_ADDR_REG_INFO, &info, sizeof(info));
	if (err < 0)
		goto end;

	/* get model name */
	err = fw_csr_string(bebob->unit->directory, CSR_MODEL, model, 24);
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
		"%s %s (id:%d, rev:%d), GUID %08x%08x at %s, S%d",
		vendor, model,
		id, revision,
		data[0], data[1],
		dev_name(&bebob->unit->device), 100 << bebob->device->max_speed);

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
	if (cmp_connection_update(&bebob->in_conn) < 0) {
		amdtp_stream_pcm_abort(&bebob->tx_stream);
		mutex_lock(&bebob->mutex);
		snd_bebob_stream_stop(bebob, &bebob->tx_stream);
		mutex_unlock(&bebob->mutex);
	}
	amdtp_stream_update(&bebob->tx_stream);

	/* bus reset for isochronous receive stream */
	if (cmp_connection_update(&bebob->out_conn) < 0) {
		amdtp_stream_pcm_abort(&bebob->rx_stream);
		mutex_lock(&bebob->mutex);
		snd_bebob_stream_stop(bebob, &bebob->rx_stream);
		mutex_unlock(&bebob->mutex);
	}
	amdtp_stream_update(&bebob->rx_stream);

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

static int snd_bebob_probe(struct fw_unit *unit,
			   const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_bebob *bebob;
	int card_index, err;

	mutex_lock(&devices_mutex);

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

	/* TODO: get the ops in device entry table. */

	/* device specific probing like bootloading */
	if (bebob->ops && bebob->ops->probing) {
		err = bebob->ops->probing(bebob);
		/* wait for loading firmware */
		if (err > 0)
			err = -EIO;
	} else
		err = detect_channels(bebob);
	if (err < 0)
		goto error;

	/* name device with communication */
	err = name_device(bebob);
	if (err < 0)
		goto error;

	/* create devices */
	err = snd_bebob_create_pcm_devices(bebob);
	if (err < 0)
		goto error;

	snd_bebob_create_midi_devices(bebob);

	/* proc interfaces */
	snd_bebob_proc_init(bebob);

	/* register card and device */
	snd_card_set_dev(card, &unit->device);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		goto error;
	}
	dev_set_drvdata(&unit->device, card);
	devices_used |= 1 << card_index;
	bebob->card_index = card_index;

	/* proved */
	err = 0;
	goto end;

error:
	snd_card_free(card);

end:
	mutex_unlock(&devices_mutex);
	return err;
}

static void snd_bebob_remove(struct fw_unit *unit)
{
	struct snd_card *card = dev_get_drvdata(&unit->device);
	struct snd_bebob *bebob = card->private_data;

	snd_bebob_destroy_pcm_devices(bebob);

	snd_card_disconnect(bebob->card);
	snd_card_free_when_closed(bebob->card);

	return;
}

#define VENDOR_MAUDIO1	0x00000d6c
#define VENDOR_MAUDIO2	0x000007f5
#define VENDOR_YAMAHA	0x0000a0de

#define MODEL_YAMAHA_GO44			0x0010000b
#define MODEL_YAMAHA_GO46			0x0010000c
#define MODEL_MAUDIO_OZONIC			0x0000000a
#define MODEL_MAUDIO_FW_BOOTLOADER		0x00010058
#define MODEL_MAUDIO_FW_410			0x00010046
#define MODEL_MAUDIO_AUDIOPHILE_BOTH		0x00010060
#define MODEL_MAUDIO_SOLO			0x00010062
#define MODEL_MAUDIO_FW_1814_BOOTLOADER		0x00010070
#define MODEL_MAUDIO_FW_1814			0x00010071

static const struct ieee1394_device_id snd_bebob_id_table[] = {
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_YAMAHA,
		.model_id	= MODEL_YAMAHA_GO44,
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_YAMAHA,
		.model_id	= MODEL_YAMAHA_GO46,
	},
	/* Ozonic has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_OZONIC,
	},
	/* Firewire 410 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_FW_BOOTLOADER,
		.driver_data	= (kernel_ulong_t)&maudio_bootloader_ops
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO2,
		.model_id	= MODEL_MAUDIO_FW_410,
		.driver_data	= (kernel_ulong_t)&maudio_fw410_ops
	},
	/* Firewire Audiophile has one ID for both bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_AUDIOPHILE_BOTH,
		.driver_data	= (kernel_ulong_t)&maudio_audiophile_ops
	},
	/* Firewire Solo has one ID, no bootloader */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_SOLO,
	},
	/* Firewire 1814 has two IDs, for bootloader and itself */
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_FW_1814_BOOTLOADER,
		.driver_data	= (kernel_ulong_t)&maudio_bootloader_ops
	},
	{
		.match_flags	= IEEE1394_MATCH_VENDOR_ID |
				  IEEE1394_MATCH_MODEL_ID,
		.vendor_id	= VENDOR_MAUDIO1,
		.model_id	= MODEL_MAUDIO_FW_1814,
		.driver_data	= (kernel_ulong_t)&maudio_fw1814_ops
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_bebob_id_table);

static struct fw_driver snd_bebob_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "snd-bebob",
		.bus	= &fw_bus_type,
	},
	.probe    = snd_bebob_probe,
	.update	  = snd_bebob_update,
	.remove   = snd_bebob_remove,
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
