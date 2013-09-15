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
 * Currently this module support any controls related to decision of channels
 * in stream, hardware metering and digital format. Users should utilize tools
 * which FFADO project developed.
 */

/*
 * Physical metering:
 *  the value in unavvailable channels is zero.
 */
static int
physical_metering_info(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_info *info)
{
	struct snd_efw *efw = ctl->private_data;

	info->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	info->count = (efw->input_meter_counts + efw->output_meter_counts)
			 * 4 + 2;

	return 0;
}
static int
physical_metering_get(struct snd_kcontrol *ctl,
		      struct snd_ctl_elem_value *value)
{
	struct snd_efw *efw = ctl->private_data;
	struct snd_efw_phys_meters *meters;
	int base = sizeof(struct snd_efw_phys_meters);
	int count = efw->input_meter_counts + efw->output_meter_counts;
	u32 *dst, *src;
	int i, err;

	meters = kzalloc(base + count * 4, GFP_KERNEL);
	if (meters == NULL)
		return -ENOMEM;

	err = snd_efw_command_get_phys_meters(efw, meters, base + count * 4);
	if (err < 0)
		goto end;

	value->value.bytes.data[0] = efw->input_meter_counts;
	value->value.bytes.data[1] = efw->output_meter_counts;

	dst = (u32 *)(value->value.bytes.data + 2);
	src = meters->values;

	for (i = 0; i < efw->input_meter_counts; i += 1)
		dst[i] = src[efw->output_meter_counts + i];

	for (i = 0; i < efw->output_meter_counts; i += 1)
		dst[i + efw->input_meter_counts] = src[i];

	err = 0;
end:
	kfree(meters);
	return err;
}
static const
struct snd_kcontrol_new physical_metering = {
	.iface	= SNDRV_CTL_ELEM_IFACE_CARD,
	.name	= "Physical Metering",
	.access	= SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info	= physical_metering_info,
	.get	= physical_metering_get
};

/*
 * Global Control:  Digital capture and playback mode
 *
 * S/PDIF or ADAT, Coaxial or Optical
 * snd_efw_hwinfo.flags include a flag for this control.
 */
static char *digital_iface_descs[] = {"S/PDIF Coaxial", "ADAT Coaxial",
					  "S/PDIF Optical", "ADAT Optical"};
static int
control_digital_interface_info(struct snd_kcontrol *kctl,
			       struct snd_ctl_elem_info *einf)
{
	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	int value, i;

	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(digital_iface_descs); i += 1) {
		if ((1 << i) & efw->supported_digital_interface)
			einf->value.enumerated.items += 1;
	}

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported clock source */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(digital_iface_descs); i += 1) {
		if (!((1 << i) & efw->supported_digital_interface))
			continue;
		else if (value == 0)
			break;
		else
		value -= 1;
	}

	strcpy(einf->value.enumerated.name, digital_iface_descs[i]);

	return 0;
}
static int
control_digital_interface_get(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_digital_interface digital_interface;
	int i;

	/* get current index */
	err = snd_efw_command_get_digital_interface(efw, &digital_interface);
	if (err < 0)
		goto end;

	/* check clock source */
	if ((digital_interface < 0) &&
	    (ARRAY_SIZE(digital_iface_descs) < digital_interface))
		goto end;

	/* generate user value */
	uval->value.enumerated.item[0] = 0;
	for (i = 0; i < digital_interface; i += 1) {
		if ((1 << i) & efw->supported_digital_interface)
			uval->value.enumerated.item[0] += 1;
	}

end:
	return err;
}
static int
control_digital_interface_put(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	int index, value;

	/* get index from user value */
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(digital_iface_descs); index++) {
		/* not supported */
		if (!((1 << index) & efw->supported_digital_interface))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	/* set clock */
	if (snd_efw_command_set_digital_interface(efw, index) < 0)
		goto end;

	changed = 1;

end:
	return changed;
}

/*
 * Global Control: S/PDIF format are selectable from "Professional/Consumer".
 *  Consumer:		IEC-60958 Digital audio interface
 *				Part 3:Consumer applications
 *  Professional:	IEC-60958 Digital audio interface
 *				Part 4: Professional applications
 *
 * snd_efw_hwinfo.flags include a flag for this control
 */
static char *spdif_format_descs[] = {"Consumer", "Professional"};
static int
control_spdif_format_info(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_info *einf)
{
	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;
	einf->value.enumerated.items = ARRAY_SIZE(spdif_format_descs);

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	strcpy(einf->value.enumerated.name,
		spdif_format_descs[einf->value.enumerated.item]);

	return 0;
}
static int
control_spdif_format_get(struct snd_kcontrol *kctl,
			 struct snd_ctl_elem_value *uvalue)
{
	int err = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_iec60958_format format;

	err = snd_efw_command_get_iec60958_format(efw, &format);
	if (err >= 0)
		uvalue->value.enumerated.item[0] = format;

	return 0;
}
static int
control_spdif_format_put(struct snd_kcontrol *kctl,
			 struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
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
 * snd_efw_hwinfo.min_sample_rate and struct efc_hwinfo.max_sample_rate
 * is a minimum and maximum sampling rate
 */
static char *sampling_rate_descs[] = {"5512Hz", "8000Hz", "11025Hz",
				      "16000Hz", "22050Hz", "32000Hz",
				      "44100Hz", "48000Hz", "64000Hz",
				      "88200Hz", "96000Hz", "176400Hz",
				      "192000Hz"};
static int sampling_rates[] = {5512, 8000, 11025, 16000, 22500, 32000, 44100,
			       48000, 64000, 88200, 96000, 176400, 192000};
static int
control_sampling_rate_info(struct snd_kcontrol *kctl,
			   struct snd_ctl_elem_info *einf)
{
	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	int value, i;

	/* maximum value for user */
	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(sampling_rate_descs); i += 1) {
		if ((1 << i) & efw->supported_sampling_rate)
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

	strcpy(einf->value.enumerated.name, sampling_rate_descs[i]);

	return 0;
}
static int
control_sampling_rate_get(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
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
control_sampling_rate_put(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
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
 * snd_efw_hwinfo.supported_clocks is a flags for this control
 */
static char *clock_src_descs[] = {"Internal", "SYT Match", "Word",
				     "S/PDIF", "ADAT1", "ADAT2"};
static int control_clock_source_info(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_info *einf)
{
	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	int value, i;

	/* skip unsupported clock source */
	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(clock_src_descs); i += 1) {
		if ((1 << i) & efw->supported_clock_source)
			einf->value.enumerated.items += 1;
	}

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported clock source */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(clock_src_descs); i += 1) {
		if (!((1 << i) & efw->supported_clock_source))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	strcpy(einf->value.enumerated.name, clock_src_descs[i]);

	return 0;
}
static int control_clock_source_get(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *uval)
{
	int err = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	enum snd_efw_clock_source clock_source;
	int i;

	/* get current index */
	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;

	/* check clock source */
	if ((clock_source < 0) && (ARRAY_SIZE(clock_src_descs) < clock_source))
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
static bool check_clock_input(struct snd_efw *efw,
			      enum snd_efw_clock_source source)
{
	struct snd_efw_phys_meters *meters;
	int len = sizeof(struct snd_efw_phys_meters);
	bool result;

	meters = kzalloc(len, GFP_KERNEL);
	if (meters == NULL)
		return false;

	if (snd_efw_command_get_phys_meters(efw, meters, len) < 0) {
		snd_printk(KERN_INFO"my error\n");
		result = false;
		goto end;
	}

	result = (meters->clock_in & (1 << source));
end:
	kfree(meters);
	return result;
}
static int control_clock_source_put(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *uval)
{
	int changed = 0;

	struct snd_efw *efw = snd_kcontrol_chip(kctl);
	int index, value;

	/* get index from user value */
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(clock_src_descs); index++) {
		/* not supported */
		if (!((1 << index) & efw->supported_clock_source))
			continue;
		else if (value == 0)
			break;
		else
			value -= 1;
	}

	if (!check_clock_input(efw, index))
		return 0;

	/* set clock */
	if (snd_efw_command_set_clock_source(efw, index) < 0)
		goto end;

	changed = 1;

end:
	return changed;
}

static struct snd_kcontrol_new global_clock_source_control = {
	.name	= "Clock Source",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= control_clock_source_info,
	.get	= control_clock_source_get,
	.put	= control_clock_source_put
};

static struct snd_kcontrol_new global_sampling_rate_control = {
	.name	= "Sampling Rate",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= control_sampling_rate_info,
	.get	= control_sampling_rate_get,
	.put	= control_sampling_rate_put
};

static struct snd_kcontrol_new global_digital_interface_control = {
	.name	= "Digital Mode",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= control_digital_interface_info,
	.get	= control_digital_interface_get,
	.put	= control_digital_interface_put
};

static struct snd_kcontrol_new global_iec60958_format_control = {
	.name	= "S/PDIF Format",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= control_spdif_format_info,
	.get	= control_spdif_format_get,
	.put	= control_spdif_format_put
};

int snd_efw_create_control_devices(struct snd_efw *efw)
{
	int err;
	struct snd_kcontrol *kctl;

	kctl = snd_ctl_new1(&physical_metering, efw);
	err = snd_ctl_add(efw->card, kctl);
	if (err < 0)
		goto end;

	if (efw->supported_clock_source > 0) {
		kctl = snd_ctl_new1(&global_clock_source_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}
	if (efw->supported_sampling_rate > 0) {
		kctl = snd_ctl_new1(&global_sampling_rate_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
		efw->control_id_sampling_rate = &kctl->id;
	}
	if (efw->supported_digital_interface > 0) {
		kctl = snd_ctl_new1(&global_digital_interface_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;

		kctl = snd_ctl_new1(&global_iec60958_format_control, efw);
		err = snd_ctl_add(efw->card, kctl);
		if (err < 0)
			goto end;
	}

	err = 0;
end:
	return err;
}
