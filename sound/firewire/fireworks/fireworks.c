/*
 * fireworks.c - a part of driver for Fireworks based devices
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
MODULE_AUTHOR("Takashi Sakamoto <o-takashi@sakamocchi.jp>"
	      "Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS]	= SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS]	= SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS]	= SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable Fireworks sound card");

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;

#define VENDOR_LOUD			0x000ff2
#define  MODEL_MACKIE_400F		0x00400f
#define  MODEL_MACKIE_1200F		0x01200f

#define VENDOR_ECHO			0x001486
#define  MODEL_ECHO_AUDIOFIRE_12	0x00af12
#define  MODEL_ECHO_AUDIOFIRE_12HD	0x0af12d
#define  MODEL_ECHO_AUDIOFIRE_12_APPLE	0x0af12a
/* This is applied for AudioFire8 (until 2009 July) */
#define  MODEL_ECHO_AUDIOFIRE_8		0x000af8
#define  MODEL_ECHO_AUDIOFIRE_2		0x000af2
#define  MODEL_ECHO_AUDIOFIRE_4		0x000af4
/* AudioFire9 is applied for AudioFire8(since 2009 July) and AudioFirePre8 */
#define  MODEL_ECHO_AUDIOFIRE_9		0x000af9
/* unknown as product */
#define  MODEL_ECHO_FIREWORKS_8		0x0000f8
#define  MODEL_ECHO_FIREWORKS_HDMI	0x00afd1

#define VENDOR_GIBSON			0x00075b
/* for Robot Interface Pack of Dark Fire, Dusk Tiger, Les Paul Standard 2010 */
#define  MODEL_GIBSON_RIP		0x00afb2
/* unknown as product */
#define  MODEL_GIBSON_GOLDTOP		0x00afb9

#define MAX_TRIES_AFTER_BUS_RESET	5

/* hardware capability flags */
#define FLAG_CMD_RESP_ADDR_CHANGABLE	0
#define FLAG_MIRRORING_SUPPORTED	1
#define FLAG_HAS_SPDIF_COAXIAL		2
#define FLAG_HAS_AESEBU_XLR		3
#define FLAG_HAS_DSP_MIXER		4
#define FLAG_HAS_FPGA			5
#define FLAG_HAS_PHANTOM		6
#define FLAG_HAS_PLAYBACK_ROUTING	7
#define FLAG_HAS_INPUT_GAIN_ADJUSTABLE	8
#define FLAG_HAS_SPDIF_OPTICAL		9
#define FLAG_HAS_ADAT_OPTICAL		10
#define FLAG_HAS_NOMINAL_INPUT		11
#define FLAG_HAS_NOMINAL_OUTPUT		12
#define FLAG_HAS_GUITAR_HEX_CAPTURE	13
#define FLAG_HAS_GUITAR_CHARGING	14
#define FLAG_HAS_SOFT_CLIP		15

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
	if (hwinfo->flags & BIT(FLAG_CMD_RESP_ADDR_CHANGABLE))
		efw->cmd_resp_addr_changable = 1;
	if (hwinfo->flags & BIT(FLAG_HAS_SPDIF_COAXIAL))
		efw->supported_digital_interface |= BIT(0);
	if (hwinfo->flags & BIT(FLAG_HAS_AESEBU_XLR))
		efw->supported_digital_interface |= BIT(1);
	if (hwinfo->flags & BIT(FLAG_HAS_SPDIF_OPTICAL))
		efw->supported_digital_interface |= BIT(2);
	if (hwinfo->flags & BIT(FLAG_HAS_ADAT_OPTICAL))
		efw->supported_digital_interface |= BIT(3);

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
		for (i = 0; i < efw->output_group_counts; i++) {
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
		for (i = 0; i < efw->input_group_counts; i++) {
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
snd_efw_card_free(struct snd_card *card)
{
	struct snd_efw *efw = card->private_data;

	if (efw->card_index >= 0) {
		mutex_lock(&devices_mutex);
		devices_used &= ~BIT(efw->card_index);
		mutex_unlock(&devices_mutex);
	}

	if (efw->output_group_counts > 0)
		kfree(efw->output_groups);
	if (efw->input_group_counts > 0)
		kfree(efw->input_groups);

	mutex_destroy(&efw->mutex);

	return;
}

static int snd_efw_probe(struct fw_unit *unit,
			 const struct ieee1394_device_id *entry)
{
	struct snd_card *card;
	struct snd_efw *efw;
	int card_index, err;

	mutex_lock(&devices_mutex);

	/* check registered cards */
	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (!(devices_used & BIT(card_index)) && enable[card_index])
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
	init_waitqueue_head(&efw->hwdep_wait);

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

	/* create midi interface */
	if (efw->midi_output_ports || efw->midi_input_ports) {
		err = snd_efw_create_midi_devices(efw);
		if (err < 0)
			goto error;
	}

	/* create PCM interface */
	err = snd_efw_create_pcm_devices(efw);
	if (err < 0)
		goto error;

	err = snd_efw_create_hwdep_device(efw);
	if (err < 0)
		goto error;

	err = snd_efw_stream_init_duplex(efw);
	if (err < 0)
		goto error;

	/* register card and device */
	snd_card_set_dev(card, &unit->device);
	err = snd_card_register(card);
	if (err < 0)
		goto error;
	dev_set_drvdata(&unit->device, efw);
	devices_used |= BIT(card_index);
	efw->card_index = card_index;

	/* proved */
	err = 0;
	goto end;
error:
	snd_card_free(card);
end:
	mutex_unlock(&devices_mutex);
	return err;
}

static void snd_efw_update(struct fw_unit *unit)
{
	struct snd_efw *efw = dev_get_drvdata(&unit->device);
	unsigned int tries;
	int err;

	snd_efw_command_bus_reset(efw->unit);

	/*
	 * NOTE:
	 * There is a reason the application get error by bus reset during
	 * playing/recording.
	 *
	 * Fireworks doesn't sometimes respond FCP command after bus reset.
	 * Then the normal process to start streaming is failed. Here EFC
	 * identify command is used to check this. When all of trials are
	 * failed, the PCM stream is stopped, then the application fails to
	 * play/record and the users see 'input/output'
	 * error.
	 *
	 * Referring to OHCI1394, the connection should be redo within 1 sec
	 * after bus reset. Inner snd-firewire-lib, FCP commands are retried
	 * three times if failed. If identify commands are executed 5 times,
	 * totally, FCP commands are sent 15 times till completely failed. But
	 * the total time is not assume-able because it's asynchronous
	 * transactions. Here we wait 500msec between each commands. I hope
	 * total time within 1 sec.
	 */
	tries = 0;
	do {
		err = snd_efw_command_identify(efw);
		if (err == 0)
			break;
		msleep(100);
	} while (tries++ < MAX_TRIES_AFTER_BUS_RESET);

	if (err < 0) {
		snd_efw_stream_destroy_duplex(efw);
		goto end;
	}

	/*
	 * NOTE:
	 * There is another reason that the application get error by bus reset
	 * during playing/recording.
	 *
	 * As a result of Juju's rediscovering nodes at bus reset, there is a
	 * case of changing node id reflecting identified-tree. Then sometimes
	 * logical devices are removed and re-probed. When
	 * connecting/disconnecting sound cards or disconnecting, this behavior
	 * brings an issue.
	 *
	 * When connecting/disconnecting sound cards or in Firewire bus, if
	 * remove/probe is generated for the current sound cards, the ids for
	 * current sound cards are sometimes changed and character devices are
	 * also changed. Then user-land application fails to play/record and the
	 * users see 'No such device' error.
	 *
	 * Even if all is OK, the sound is not smooth, not fluent. At least,
	 * short noises, at largest, blank sound for 1-3 seconds.
	 */
	snd_efw_stream_update_duplex(efw);
end:
	return;
}

static void snd_efw_remove(struct fw_unit *unit)
{
	struct snd_efw *efw= dev_get_drvdata(&unit->device);

	snd_efw_stream_destroy_duplex(efw);

	snd_card_disconnect(efw->card);
	snd_card_free_when_closed(efw->card);
	return;
}

static const struct ieee1394_device_id snd_efw_id_table[] = {
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_400F),
	SND_EFW_DEV_ENTRY(VENDOR_LOUD, MODEL_MACKIE_1200F),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12HD),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_12_APPLE),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_2),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_4),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_AUDIOFIRE_9),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_8),
	SND_EFW_DEV_ENTRY(VENDOR_ECHO, MODEL_ECHO_FIREWORKS_HDMI),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_RIP),
	SND_EFW_DEV_ENTRY(VENDOR_GIBSON, MODEL_GIBSON_GOLDTOP),
	{}
};
MODULE_DEVICE_TABLE(ieee1394, snd_efw_id_table);

static struct fw_driver snd_efw_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "snd-fireworks",
		.bus = &fw_bus_type,
	},
	.probe    = snd_efw_probe,
	.update   = snd_efw_update,
	.remove   = snd_efw_remove,
	.id_table = snd_efw_id_table,
};

static int __init snd_efw_init(void)
{
	int err;

	err = snd_efw_command_register();
	if (err < 0)
		goto end;

	err = driver_register(&snd_efw_driver.driver);
	if (err < 0)
		snd_efw_command_unregister();

end:
	return err;
}

static void __exit snd_efw_exit(void)
{
	snd_efw_command_unregister();
	driver_unregister(&snd_efw_driver.driver);
	mutex_destroy(&devices_mutex);
}

module_init(snd_efw_init);
module_exit(snd_efw_exit);
