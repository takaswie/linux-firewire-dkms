/*
 * fireworks/control.c - driver for Firewire devices from Echo Digital Audio
 *
 * Copyright (c) 2013 Takashi Sakamoto <o-takashi@sakamocchi.jp>
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

/*
 * Physical metering:
 *  available channels differ depending on current sampling rate
 */
static int snd_efw_physical_metering_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	struct snd_efw_t *efw = ctl->private_data;

	info->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	info->count = 2 + (efw->input_meter_counts + efw->output_meter_counts) * 4;

	return 0;
}
static int snd_efw_physical_metering_get(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	int err;

	struct snd_efw_t *efw = ctl->private_data;

	value->value.bytes.data[0] = efw->pcm_playback_channels;
	value->value.bytes.data[1] = efw->pcm_capture_channels;

	err = snd_efw_command_get_phys_meters(efw,
		efw->input_meter_counts + efw->output_meter_counts, (u32 *)(value->value.bytes.data + 2));
	if (err < 0)
		goto end;

	memmove(value->value.bytes.data + 2 + efw->pcm_playback_channels * 4,
		value->value.bytes.data + 2 + efw->output_meter_counts * 4,
		efw->pcm_capture_channels * 4);
	memset(value->value.bytes.data + 2 + efw->pcm_playback_channels * 4 + efw->pcm_capture_channels * 4,
		0, 2 + (efw->output_meter_counts - efw->pcm_playback_channels) * 4 +
			(efw->input_meter_counts - efw->pcm_capture_channels) * 4);

end:
	return err;
}
static const struct snd_kcontrol_new snd_efw_physical_metering = {
	.iface	= SNDRV_CTL_ELEM_IFACE_CARD,
	.name	= "Physical Metering",
	.access	= SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info	= snd_efw_physical_metering_info,
	.get	= snd_efw_physical_metering_get
};

/* Playback Control: PCM Playback Gain: */
static const DECLARE_TLV_DB_SCALE(snd_efw_playback_db_scale, -6000, 100, 0);
static int snd_efw_playback_gain_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	einf->count = 2;
	einf->value.integer.min = -128;
	einf->value.integer.max = 0;

	return err;
}
static int snd_efw_playback_gain_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_playback_gain_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Playback Control: playback solo of each channels */
static int snd_efw_playback_solo_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_playback_solo_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_playback_solo_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Playback Control: playback mute of each channels */
static int snd_efw_playback_mute_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_playback_mute_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_playback_mute_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

static struct snd_kcontrol_new snd_efw_playback_controls[] = {
	{
		.name	= "PCM Playback Gain",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE |
		          SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE,
		.info	= snd_efw_playback_gain_info,
		.get	= snd_efw_playback_gain_get,
		.put	= snd_efw_playback_gain_put,
		.tlv = { .p =  snd_efw_playback_db_scale }
	},
	{
		.name	= "PCM Playback Solo",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_playback_solo_info,
		.get	= snd_efw_playback_solo_get,
		.put	= snd_efw_playback_solo_put,
	},
	{
		.name	= "PCM Playback Mute",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_playback_mute_info,
		.get	= snd_efw_playback_mute_get,
		.put	= snd_efw_playback_mute_put,
	},
};

/* Capture Control: capture gain of each channels */
static const DECLARE_TLV_DB_SCALE(snd_efw_capture_db_scale, -6000, 100, 0);
static int snd_efw_capture_gain_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	einf->count = 2;
	einf->value.integer.min = -128;
	einf->value.integer.max = 0;

	return err;
}
static int snd_efw_capture_gain_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_capture_gain_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Capture Control: capture mute of each channels */
static int snd_efw_capture_mute_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_capture_mute_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_capture_mute_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Capture Control: capture solo of each channels */
static int snd_efw_capture_solo_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_capture_solo_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_capture_solo_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Capture Control: capture solo of each channels */
static int capture_nominal_tmp = 0;
static char *capture_nominal_descs[] = {"-10dBV", "+4dBu"};
static int snd_efw_capture_nominal_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;
	einf->value.enumerated.items = ARRAY_SIZE(capture_nominal_descs);

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	strcpy(einf->value.enumerated.name, capture_nominal_descs[einf->value.enumerated.item]);

	return err;
}
static int snd_efw_capture_nominal_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uval)
{
	int err = 0;

	uval->value.enumerated.item[0] = capture_nominal_tmp;

	return err;
}
static int snd_efw_capture_nominal_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uval)
{
	int changed = 0;
	int value = uval->value.enumerated.item[0];

	if (value <= ARRAY_SIZE(capture_nominal_descs)) {
		changed = 1;
		capture_nominal_tmp = value;
	}

	return changed;
}

/* Capture Control: capture solo of each channels */
static int snd_efw_capture_pan_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	einf->count = 1;
	einf->value.integer.min = -64;
	einf->value.integer.max = 64;

	return err;
}
static int snd_efw_capture_pan_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uval)
{
	int err = 0;
	return err;
}
static int snd_efw_capture_pan_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uval)
{
	int err = 0;
	return err;
}

static struct snd_kcontrol_new snd_efw_capture_controls[] = {
	{
		.name	= "PCM Capture Gain",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE |
		          SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE,
		.info	= snd_efw_capture_gain_info,
		.get	= snd_efw_capture_gain_get,
		.put	= snd_efw_capture_gain_put,
		.tlv = { .p =  snd_efw_capture_db_scale }
	},
	{
		.name	= "PCM Capture Mute",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_capture_mute_info,
		.get	= snd_efw_capture_mute_get,
		.put	= snd_efw_capture_mute_put,
	},
	{
		.name	= "PCM Capture Solo",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_capture_solo_info,
		.get	= snd_efw_capture_solo_get,
		.put	= snd_efw_capture_solo_put,
	},
	{
		.name	= "PCM Capture Nominal",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_capture_nominal_info,
		.get	= snd_efw_capture_nominal_get,
		.put	= snd_efw_capture_nominal_put,
	},
	{
		.name	= "PCM Capture Pan",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_capture_pan_info,
		.get	= snd_efw_capture_pan_get,
		.put	= snd_efw_capture_pan_put,
	}
};

/* Output Control: Master Output Gain */
static const DECLARE_TLV_DB_SCALE(snd_efw_output_db_scale, -6000, 100, 0);
static int snd_efw_output_gain_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	einf->count = 2;
	einf->value.integer.min = -128;
	einf->value.integer.max = 0;

	return err;
}
static int snd_efw_output_gain_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_output_gain_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Output Control: Master Output Mute */
static int snd_efw_output_mute_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_output_mute_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_output_mute_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

/* Output Control: Master Output Solo */
static int snd_efw_output_solo_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *einf)
{
	int err = 0;

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return err;
}
static int snd_efw_output_solo_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}
static int snd_efw_output_solo_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	int err = 0;
	return err;
}

static struct snd_kcontrol_new snd_efw_output_controls[] = {
	{
		.name	= "Master Output Gain",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE |
		          SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE,
		.info	= snd_efw_output_gain_info,
		.get	= snd_efw_output_gain_get,
		.put	= snd_efw_output_gain_put,
		.tlv = { .p =  snd_efw_output_db_scale }
	},
	{
		.name	= "Master Output Mute",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_output_mute_info,
		.get	= snd_efw_output_mute_get,
		.put	= snd_efw_output_mute_put
	},
	{
		.name	= "Master Output Solo",
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info	= snd_efw_output_solo_info,
		.get	= snd_efw_output_solo_get,
		.put	= snd_efw_output_solo_put
	},
};

/*
 * Global Control:  Digital capture and playback mode
 *
 * S/PDIF or ADAT, Coaxial or Optical
 * struct efc_hwinfo_t.flags include a flag for this control
 */
static char *digital_mode_descs[] = {"S/PDIF Coaxial", "ADAT Coaxial",
						"S/PDIF Optical", "ADAT Optical"};
static int snd_efw_control_digital_mode_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *einf)
{
	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int value, i;

	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(digital_mode_descs); i += 1) {
		if ((1 << i) & efw->supported_digital_mode)
			einf->value.enumerated.items += 1;
        }

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported clock source */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(digital_mode_descs); i += 1) {
		if (!((1 << i) & efw->supported_digital_mode))
			continue;
		else if (value == 0)
			break;
		else
		value -= 1;
	}

	strcpy(einf->value.enumerated.name, digital_mode_descs[i]);

	return 0;
}
static int snd_efw_control_digital_mode_get(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_digital_mode_t digital_mode;
	int i;

	/* get current index */
	err = snd_efw_command_get_digital_mode(efw, &digital_mode);
	if (err < 0)
		goto end;

	/* check clock source */
	if ((digital_mode < 0) && (ARRAY_SIZE(digital_mode_descs) < digital_mode))
		goto end;

	/* generate user value */
	uval->value.enumerated.item[0] = 0;
	for (i = 0; i < digital_mode; i += 1) {
		if ((1 << i) & efw->supported_digital_mode)
			uval->value.enumerated.item[0] += 1;
	}

end:
	return err;
}
static int snd_efw_control_digital_mode_put(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int index, value;

	/* get index from user value */
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(digital_mode_descs); index += 1) {
		/* not supported */
		if (!((1 << index) & efw->supported_digital_mode))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	/* set clock */
	if (snd_efw_command_set_digital_mode(efw, index) < 0)
		goto end;

	changed = 1;

end:
	return changed;
}

/*
 * Global Control: S/PDIF format are selectable from "Professional/Consumer".
 *  Consumer:		IEC-60958 Digital audio interface – Part 3: Consumer applications
 *  Professional:	IEC-60958 Digital audio interface – Part 4: Professional applications
 *
 * struct efc_hwinfo_t.flags include a flag for this control
 */
static char *spdif_format_descs[] = {"Consumer", "Professional"};
static int snd_efw_control_spdif_format_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *einf)
{
	/* TODO: this control should be unavailable when ADAT is selected */

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;
	einf->value.enumerated.items = ARRAY_SIZE(spdif_format_descs);

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	strcpy(einf->value.enumerated.name, spdif_format_descs[einf->value.enumerated.item]);

        return 0;
}
static int snd_efw_control_spdif_format_get(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uvalue)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_iec60958_format_t format;

	err = snd_efw_command_get_iec60958_format(efw, &format);
	if (err >= 0)
		uvalue->value.enumerated.item[0] = format;

	return 0;
}
static int snd_efw_control_spdif_format_put(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int value = uval->value.enumerated.item[0];

	if (value < ARRAY_SIZE(spdif_format_descs)) {
		if (snd_efw_command_set_iec60958_format(efw, value) < 0)
			goto end;
		changed = 1;
	}

end:
	return changed;
}

/*
 * Global Control: Sampling Rate Control
 *
 * struct efc_hwinfo_t.min_sample_rate and struct efc_hwinfo_t.max_sample_rate
 * is a minimum and maximum sampling rate
 */
static char *sampling_rate_descs[] = {"5512Hz", "8000Hz", "11025Hz", "16000Hz",
	"22050Hz", "32000Hz", "44100Hz", "48000Hz", "64000Hz",
	"88200Hz", "96000Hz", "176400Hz", "192000Hz"};
static int sampling_rates[] = {5512, 8000, 11025, 16000,
	22500, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000};
static int
snd_efw_control_sampling_rate_info(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_info *einf)
{
	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	const char *desc;
	int value, i;

	/* TODO: this control is unavailable when isochronous stream runs
	if (efw->something > 0)
		goto end;
	 */

	/* maximum value for user */
	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(snd_efw_multiplier_conditions); i += 1) {
		sampling_rate = snd_efw_multiplier_conditions[i].sampling_rate;
		if (efw->supported_sampling_rate & snd_pcm_rate_to_rate_bit(sampling_rate))
			einf->value.enumerated.items += 1;
	}

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported clock source */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(sampling_rate_descs); i += 1) {
		if (!((1 << i) & efw->supported_sampling_rate))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	desc = snd_efw_multiplier_conditions[i].desc;
	strcpy(einf->value.enumerated.name, desc);

        return 0;
}
static int
snd_efw_control_sampling_rate_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int sampling_rate;
	int index, i;

	/* get current sampling rate */
	err = snd_efw_command_get_sampling_rate(efw, &sampling_rate);
	if (err < 0)
		goto end;

	/* get index */
	for (index = 0; index < ARRAY_SIZE(sampling_rates); index += 1) {
		if (sampling_rates[index] == sampling_rate)
			break;
	}

	/* get user value */
	uval->value.enumerated.item[0] = 0;
	for (i = 0; i < index; i += 1) {
		if ((1 << i) & efw->supported_sampling_rate)
			uval->value.enumerated.item[0] += 1;
	}

end:
	return err;
}
static int
snd_efw_control_sampling_rate_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int index, value;

	/* get index from user value */
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(sampling_rates); index += 1) {
		/* not supported */
		if (!((1 << index) & efw->supported_sampling_rate))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	/* set sampling rate */
	if (snd_efw_command_set_sampling_rate(efw, sampling_rates[index]) < 0)
		goto end;

	changed = 1;

end:
	return changed;
}

/*
 * Global Control: Clock Source Control
 *
 * struct efw_hwinfo_t.supported_clocks is a flags for this control
 */
static char *clock_source_descs[] = {"Internal", "SYT Match", "Word",
						"S/PDIF", "ADAT1", "ADAT2"};
static int snd_efw_control_clock_source_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *einf)
{
	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int value, i;

	/* skip unsupported clock source */
	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(clock_source_descs); i += 1) {
		if ((1 << i) & efw->supported_clock_source)
			einf->value.enumerated.items += 1;
	}

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported clock source */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(clock_source_descs); i += 1) {
		if (!((1 << i) & efw->supported_clock_source))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	strcpy(einf->value.enumerated.name, clock_source_descs[i]);

        return 0;
}
static int
snd_efw_control_clock_source_get(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_clock_source_t clock_source;
	int i;

	/* get current index */
	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;

	/* check clock source */
	if ((clock_source < 0) && (ARRAY_SIZE(clock_source_descs) < clock_source))
		goto end;

	/* generate user value */
	uval->value.enumerated.item[0] = 0;
	for (i = 0; i < clock_source; i += 1) {
		if ((1 << i) & efw->supported_clock_source)
			uval->value.enumerated.item[0] += 1;
	}

end:
	return err;
}
static int snd_efw_control_clock_source_put(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int index, value;

	/* get index from user value */
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(clock_source_descs); index += 1) {
		/* not supported */
		if (!((1 << index) & efw->supported_clock_source))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	/* set clock */
	if (snd_efw_command_set_clock_source(efw, index) < 0)
		goto end;

	changed = 1;

end:
	return changed;
}

/*
 * Global Control: Phantom Power Control
 *
 * struct efc_hwinfo_t.flags include a flag for this control
 */
static int snd_efw_control_phantom_state_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *einf)
{

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

        return 0;
}
static int snd_efw_control_phantom_state_get(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int state;

	err = snd_efw_command_get_phantom_state(efw, &state);
	if (err >= 0)
		uval->value.integer.value[0] = (state > 0) ? 1: 0;

	return 0;
}
static int snd_efw_control_phantom_state_put(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int value = (uval->value.integer.value[0] > 0) ? 1: 0;

	if (snd_efw_command_set_phantom_state(efw, value) > 0)
		changed = 1;

	return changed;
}

/*
 * Global Control: DSP Mixer Usable Control
 *
 * If it's On, this driver can change Hardware Matrix Mixer.
 * If it's Off, this driver cannot change Hardware Matrix Mixer and it's fixed.
 */
static int snd_efw_control_mixer_usable_info(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_info *einf)
{

	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

        return 0;
}
static int snd_efw_control_mixer_usable_get(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int usable;

	err = snd_efw_command_get_mixer_usable(efw, &usable);
	if (err >= 0)
		uval->value.integer.value[0] = (usable > 0) ? 1: 0;

	return 0;
}
static int snd_efw_control_mixer_usable_put(struct snd_kcontrol *kctl,
						struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw_t *efw = snd_kcontrol_chip(kctl);
	int value = (uval->value.integer.value[0] > 0) ? 1: 0;

	if (snd_efw_command_set_mixer_usable(efw, value) > 0)
		changed = 1;

	return changed;
}

static struct snd_kcontrol_new snd_efw_clock_source_control =
{
	.name	= "Clock Source",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_clock_source_info,
	.get	= snd_efw_control_clock_source_get,
	.put	= snd_efw_control_clock_source_put
};

static struct snd_kcontrol_new snd_efw_sampling_rate_control =
{
	.name	= "Sampling Rate",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_sampling_rate_info,
	.get	= snd_efw_control_sampling_rate_get,
	.put	= snd_efw_control_sampling_rate_put
};

static struct snd_kcontrol_new snd_efw_digital_mode_control =
{
	.name	= "Digital Mode",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_digital_mode_info,
	.get	= snd_efw_control_digital_mode_get,
	.put	= snd_efw_control_digital_mode_put
};

static struct snd_kcontrol_new snd_efw_global_iec60958_format_control =
{
	.name	= "S/PDIF Format",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_spdif_format_info,
	.get	= snd_efw_control_spdif_format_get,
	.put	= snd_efw_control_spdif_format_put
};

static struct snd_kcontrol_new snd_efw_global_phantom_state_control =
{
	.name	= "Phantom Power",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_phantom_state_info,
	.get	= snd_efw_control_phantom_state_get,
	.put	= snd_efw_control_phantom_state_put
};

static struct snd_kcontrol_new snd_efw_global_mixer_usable_control =
{
	.name	= "DSP Mixer",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= snd_efw_control_mixer_usable_info,
	.get	= snd_efw_control_mixer_usable_get,
	.put	= snd_efw_control_mixer_usable_put
};

int snd_efw_create_control_devices(struct snd_efw_t *efw)
{
	unsigned int i;
	int err;
	struct snd_kcontrol *kctl;

	kctl = snd_ctl_new1(&snd_efw_physical_metering, efw);
	err = snd_ctl_add(efw->card, kctl);
	if (err < 0)
		goto end;

	for (i = 0; i < ARRAY_SIZE(snd_efw_output_controls); ++i) {
		kctl = snd_ctl_new1(&snd_efw_output_controls[i], efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

	for (i = 0; i < ARRAY_SIZE(snd_efw_playback_controls); ++i) {
		kctl = snd_ctl_new1(&snd_efw_playback_controls[i], efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

	for (i = 0; i < ARRAY_SIZE(snd_efw_capture_controls); i += 1) {
		snd_efw_capture_controls[i].index = 0;
		kctl = snd_ctl_new1(&snd_efw_capture_controls[i], efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

	/* capabilities */
	if (efw->supported_clock_source > 0) {
		kctl = snd_ctl_new1(&snd_efw_clock_source_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
		efw->control_id_clock_source = &kctl->id;
	}
	if (efw->supported_sampling_rate > 0) {
		kctl = snd_ctl_new1(&snd_efw_sampling_rate_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
		efw->control_id_sampling_rate = &kctl->id;
	}
	if (efw->supported_digital_mode > 0) {
		kctl = snd_ctl_new1(&snd_efw_digital_mode_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
		kctl = snd_ctl_new1(&snd_efw_global_iec60958_format_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}
	if (efw->has_phantom > 0) {
		kctl = snd_ctl_new1(&snd_efw_global_phantom_state_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

	if (efw->has_dsp_mixer > 0) {
		kctl = snd_ctl_new1(&snd_efw_global_mixer_usable_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

end:
	return err;
}
