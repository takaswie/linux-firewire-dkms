/*
 * fireworks.c - driver for Firewire devices from Echo Digital Audio
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
 * Copyright (c) 2013 Takashi Sakamoto
 *
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include "fireworks.h"

MODULE_DESCRIPTION("Echo Fireworks driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable Fireworks sound card");

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

#define FLAG_DYNADDR_SUPPORTED			0
#define FLAG_MIRRORING_SUPPORTED		1
#define FLAG_SPDIF_COAX_SUPPORTED		2
#define FLAG_SPDIF_AES_EBU_XLR_SUPPORTED	3
#define FLAG_HAS_DSP_MIXER			4
#define FLAG_HAS_FPGA				5
#define FLAG_HAS_PHANTOM			6
/* other flags exist but unknown... */

static int
get_hardware_info(struct snd_efw *efw)
{
	int err;

	struct snd_efw_hwinfo *hwinfo;
	char version[12];
	int size;
	int i;

	hwinfo = kzalloc(sizeof(struct snd_efw_hwinfo), GFP_KERNEL);
	if (hwinfo == NULL)
		return -ENOMEM;

	err = snd_efw_command_get_hwinfo(efw, hwinfo);
	if (err < 0)
		goto end;

	/* capabilities */
	if (hwinfo->flags & (1 << FLAG_DYNADDR_SUPPORTED))
		efw->dynaddr_support = 1;
	if (hwinfo->flags & (1 << FLAG_MIRRORING_SUPPORTED))
		efw->mirroring_support = 1;
	if (hwinfo->flags & (1 << FLAG_SPDIF_AES_EBU_XLR_SUPPORTED))
		efw->aes_ebu_xlr_support = 1;
	if (hwinfo->flags & (1 << FLAG_HAS_DSP_MIXER))
		efw->has_dsp_mixer = 1;
	if (hwinfo->flags & (1 << FLAG_HAS_FPGA))
		efw->has_fpga = 1;
	if (hwinfo->flags & (1 << FLAG_HAS_PHANTOM))
		efw->has_phantom = 1;
	if (hwinfo->flags & (1 << FLAG_SPDIF_COAX_SUPPORTED)) {
		efw->supported_digital_interface = BIT(2) | BIT(3);
		/* TODO: find better way... */
		if (strcmp(hwinfo->model_name, "AudioFire8a")
		 || strcmp(hwinfo->model_name, "AudioFirePre8"))
			efw->supported_digital_interface |= BIT(0);
	}

	/* for input physical metering */
	if (hwinfo->nb_out_groups > 0) {
		size = sizeof(struct snd_efw_phys_group) *
						hwinfo->nb_out_groups;
		efw->output_groups = kzalloc(size, GFP_KERNEL);
		if (efw->output_groups == NULL) {
			err = -ENOMEM;
			goto error;
		}

		efw->output_group_counts = hwinfo->nb_out_groups;
		for (i = 0; i < efw->output_group_counts; i += 1) {
			efw->output_groups[i].type  =
						hwinfo->out_groups[i].type;
			efw->output_groups[i].count =
						hwinfo->out_groups[i].count;
		}
	}

	/* for output physical metering */
	if (hwinfo->nb_in_groups > 0) {
		size = sizeof(struct snd_efw_phys_group) *
						hwinfo->nb_in_groups;
		efw->input_groups = kzalloc(size, GFP_KERNEL);
		if (efw->input_groups == NULL) {
			err = -ENOMEM;
			goto error;
		}

		efw->input_group_counts = hwinfo->nb_out_groups;
		for (i = 0; i < efw->input_group_counts; i += 1) {
			efw->input_groups[i].type =
						hwinfo->in_groups[i].type;
			efw->input_groups[i].count =
						hwinfo->in_groups[i].count;
		}
	}

	/* for mixer channels */
	efw->mixer_output_channels = hwinfo->mixer_playback_channels;
	efw->mixer_input_channels = hwinfo->mixer_capture_channels;

	/* fill channels sets */
	efw->pcm_capture_channels[0] = hwinfo->nb_1394_capture_channels;
	efw->pcm_capture_channels[1] = hwinfo->nb_1394_capture_channels_2x;
	efw->pcm_capture_channels[2] = hwinfo->nb_1394_capture_channels_4x;
	efw->pcm_playback_channels[0] = hwinfo->nb_1394_playback_channels;
	efw->pcm_playback_channels[1] = hwinfo->nb_1394_playback_channels_2x;
	efw->pcm_playback_channels[2] = hwinfo->nb_1394_playback_channels_4x;

	/* firmware version */
	err = sprintf(version, "%u.%u",
			(hwinfo->arm_version >> 24) & 0xff,
			(hwinfo->arm_version >> 16) & 0xff);

	/* set names */
	strcpy(efw->card->driver, "Fireworks");
	strcpy(efw->card->shortname, hwinfo->model_name);
	snprintf(efw->card->longname, sizeof(efw->card->longname),
		"%s %s v%s, GUID %08x%08x at %s, S%d",
		hwinfo->vendor_name, hwinfo->model_name, version,
		hwinfo->guid_hi, hwinfo->guid_lo,
		dev_name(&efw->unit->device), 100 << efw->device->max_speed);
	strcpy(efw->card->mixername, hwinfo->model_name);

	/* set flag for supported clock source */
	efw->supported_clock_source = hwinfo->supported_clocks;

	/* set flag for supported sampling rate */
	efw->supported_sampling_rate = 0;
	if ((hwinfo->min_sample_rate <= 22050)
	 && (22050 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_22050;
	if ((hwinfo->min_sample_rate <= 32000)
	 && (32000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_32000;
	if ((hwinfo->min_sample_rate <= 44100)
	 && (44100 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_44100;
	if ((hwinfo->min_sample_rate <= 48000)
	 && (48000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_48000;
	if ((hwinfo->min_sample_rate <= 88200)
	 && (88200 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_88200;
	if ((hwinfo->min_sample_rate <= 96000)
	 && (96000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_96000;
	if ((hwinfo->min_sample_rate <= 176400)
	 && (176400 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_176400;
	if ((hwinfo->min_sample_rate <= 192000)
	 && (192000 <= hwinfo->max_sample_rate))
		efw->supported_sampling_rate |= SNDRV_PCM_RATE_192000;

	/* MIDI inputs and outputs */
	efw->midi_output_ports = hwinfo->nb_midi_out;
	efw->midi_input_ports = hwinfo->nb_midi_in;

	err = 0;
	goto end;

error:
	if (efw->input_group_counts > 0)
		kfree(efw->input_groups);
	if (efw->output_group_counts > 0)
		kfree(efw->output_groups);
end:
	kfree(hwinfo);
	return err;
}

static int
get_hardware_meters_count(struct snd_efw *efw)
{
	int err;
	struct snd_efw_phys_meters *meters;

	meters = kzalloc(sizeof(struct snd_efw_phys_meters), GFP_KERNEL);
	if (meters == NULL)
		return -ENOMEM;

	err = snd_efw_command_get_phys_meters(efw, meters,
				sizeof(struct snd_efw_phys_meters));
	if (err < 0)
		goto end;

	efw->input_meter_counts = meters->nb_input_meters;
	efw->output_meter_counts = meters->nb_output_meters;

	err = 0;
end:
	kfree(meters);
	return err;
}

static void
snd_efw_update(struct fw_unit *unit)
{
	struct snd_card *card = dev_get_drvdata(&unit->device);
	struct snd_efw *efw = card->private_data;

	snd_efw_command_bus_reset(efw->unit);

	/* bus reset for isochronous transmit stream */
	if (cmp_connection_update(&efw->output_connection) < 0) {
		amdtp_stream_pcm_abort(&efw->receive_stream);
		mutex_lock(&efw->mutex);
		snd_efw_stream_stop(efw, &efw->receive_stream);
		mutex_unlock(&efw->mutex);
	}
	amdtp_stream_update(&efw->receive_stream);

	/* bus reset for isochronous receive stream */
	if (cmp_connection_update(&efw->input_connection) < 0) {
		amdtp_stream_pcm_abort(&efw->transmit_stream);
		mutex_lock(&efw->mutex);
		snd_efw_stream_stop(efw, &efw->transmit_stream);
		mutex_unlock(&efw->mutex);
	}
	amdtp_stream_update(&efw->transmit_stream);

	return;
}

static bool match_fireworks_device_name(struct fw_unit *unit)
{
	static const char *const models[] = {
		/* Echo Digital Audio */
		"AudioFire2",
		"AudioFire4",
		"AudioFire8",
		"AudioFire8a",
		"AudioFirePre8",
		"AudioFire12",
		"Fireworks8",
		"Fireworks HDMI",
		/* Mackie */
		"Onyx 400F",
		"Onyx 1200F",
		/* Gibson */
		"RIP",
		"Audiopunk",
		"Goldtop",
	};
	char name[16];
	unsigned int i;

	if (fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name)) < 0)
		return false;
	for (i = 0; i < ARRAY_SIZE(models); i++)
		if (!strcasecmp(name, models[i]))
			return true;
	return false;
}

static void
snd_efw_card_free(struct snd_card *card)
{
	struct snd_efw *efw = card->private_data;

	if (efw->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~(1 << efw->card_index);
		mutex_unlock(&devices_mutex);
	}

	if (efw->output_group_counts > 0)
		kfree(efw->output_groups);
	if (efw->input_group_counts > 0)
		kfree(efw->input_groups);

	mutex_destroy(&efw->mutex);

	return;
}

static int
snd_efw_probe(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	int card_index;
	struct snd_card *card;
	struct snd_efw *efw;
	int err;

	mutex_lock(&devices_mutex);

	/* check device name */
	if (!match_fireworks_device_name(unit))
		return -ENODEV;

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (!(devices_used & (1 << card_index)) && enable[card_index])
			break;
	if (card_index >= SNDRV_CARDS) {
		err = -ENOENT;
		goto end;
	}

	/* create card */
	err = snd_card_create(index[card_index], id[card_index],
				THIS_MODULE, sizeof(struct snd_efw), &card);
	if (err < 0)
		goto end;
	card->private_free = snd_efw_card_free;

	/* initialize myself */
	efw = card->private_data;
	efw->card = card;
	efw->device = fw_parent_device(unit);
	efw->unit = unit;
	efw->card_index = -1;
	mutex_init(&efw->mutex);
	spin_lock_init(&efw->lock);

	/* add command stack */
	err = snd_efw_command_create(efw);
	if (err < 0)
		goto error;

	/* identifing */
	err = snd_efw_command_identify(efw);
	if (err < 0)
		goto error;

	/* get hardware information */
	err = get_hardware_info(efw);
	if (err < 0)
		goto error;

	/* get the number of hardware meters */
	err = get_hardware_meters_count(efw);
	if (err < 0)
		goto error;

	/* create procfs interface */
	snd_efw_proc_init(efw);

	/* create control interface */
	err = snd_efw_create_control_devices(efw);
	if (err < 0)
		goto error;

	/* create PCM interface */
	err = snd_efw_create_pcm_devices(efw);
	if (err < 0)
		goto error;

	/* create midi interface */
	if (efw->midi_output_ports || efw->midi_input_ports) {
		err = snd_efw_create_midi_devices(efw);
		if (err < 0)
			goto error;
	}

	/* register card and device */
	snd_card_set_dev(card, dev);
	err = snd_card_register(card);
	if (err < 0)
		goto error;
	dev_set_drvdata(dev, card);
	devices_used |= 1 << card_index;
	efw->card_index = card_index;

	/* proved */
	err = 0;
	goto end;

error:
	snd_card_free(card);
	snd_efw_command_destroy();

end:
	mutex_unlock(&devices_mutex);
	return err;
}

static int
snd_efw_remove(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_efw *efw = card->private_data;

	snd_efw_destroy_pcm_devices(efw);
	snd_efw_command_destroy();

	snd_card_disconnect(card);
	snd_card_free_when_closed(card);

	return 0;
}

#define VENDOR_GIBSON			0x00075b
#define  MODEL_GIBSON_RIP		0x00afb2
/* #define  MODEL_GIBSON_GOLDTOP	0x?????? */

#define VENDOR_LOUD			0x000ff2
#define  MODEL_MACKIE_400F		0x00400f
#define  MODEL_MACKIE_1200F		0x01200f

#define VENDOR_ECHO_DIGITAL_AUDIO	0x001486
#define  MODEL_ECHO_AUDIOFIRE_2		0x000af2
#define  MODEL_ECHO_AUDIOFIRE_4		0x000af4
#define  MODEL_ECHO_AUDIOFIRE_8		0x000af8
/* #define  MODEL_ECHO_AUDIOFIRE_8A	0x?????? */
/* #define  MODEL_ECHO_AUDIOFIRE_PRE8	0x?????? */
#define  MODEL_ECHO_AUDIOFIRE_12	0x00af12
#define  MODEL_ECHO_FIREWORKS_8		0x0000f8
#define  MODEL_ECHO_FIREWORKS_HDMI	0x00afd1

#define SPECIFIER_1394TA		0x00a02d

static const struct ieee1394_device_id snd_efw_id_table[] = {
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_SPECIFIER_ID,
		.vendor_id = VENDOR_ECHO_DIGITAL_AUDIO,
		.specifier_id = SPECIFIER_1394TA,
	},
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_SPECIFIER_ID,
		.vendor_id = VENDOR_GIBSON,
		.specifier_id = SPECIFIER_1394TA,
	},
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_MODEL_ID,
		.vendor_id = VENDOR_LOUD,
		.model_id = MODEL_MACKIE_400F,
	},
	{
		.match_flags = IEEE1394_MATCH_VENDOR_ID |
			       IEEE1394_MATCH_MODEL_ID,
		.vendor_id = VENDOR_LOUD,
		.model_id = MODEL_MACKIE_1200F,
	},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_efw_id_table);

static struct fw_driver snd_efw_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-fireworks",
		.bus = &fw_bus_type,
		.probe = snd_efw_probe,
		.remove = snd_efw_remove,
	},
	.update = snd_efw_update,
	.id_table = snd_efw_id_table,
};

static int __init snd_efw_init(void)
{
	return driver_register(&snd_efw_driver.driver);
}

static void __exit snd_efw_exit(void)
{
	driver_unregister(&snd_efw_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_efw_init);
module_exit(snd_efw_exit);
