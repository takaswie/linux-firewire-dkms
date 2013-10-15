/*
 * bebob_control.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
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

#include "bebob.h"

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
	struct snd_bebob *bebob = ctl->private_data;
	struct snd_bebob_meter_spec *spec = bebob->spec->meter;

	info->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	info->count = 1 + spec->num * 2 * sizeof(u32);

	return 0;
}
static int
physical_metering_get(struct snd_kcontrol *ctl,
		      struct snd_ctl_elem_value *value)
{
	struct snd_bebob *bebob = ctl->private_data;
	struct snd_bebob_meter_spec *spec = bebob->spec->meter;
	u32 *dst;

	dst = (u32 *)value->value.bytes.data;
	*dst = spec->num;

	return spec->get(bebob, dst + 1, spec->num * 2 * sizeof(u32));
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
 */
static int
control_digital_interface_info(struct snd_kcontrol *kctl,
			       struct snd_ctl_elem_info *einf)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_dig_iface_spec *spec = bebob->spec->dig_iface;

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;
	einf->value.enumerated.items = spec->num;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	strcpy(einf->value.enumerated.name,
	       spec->labels[einf->value.enumerated.item]);

	return 0;
}
static int
control_digital_interface_get(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_dig_iface_spec *spec = bebob->spec->dig_iface;
	int id;

	mutex_lock(&bebob->mutex);
	if (spec->get(bebob, &id) >= 0)
		uval->value.enumerated.item[0] = id;
	mutex_unlock(&bebob->mutex);

	return 0;
}
static int
control_digital_interface_put(struct snd_kcontrol *kctl,
			      struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_dig_iface_spec *spec = bebob->spec->dig_iface;
	int value, changed = 0;

	value = uval->value.enumerated.item[0];

	if (value < spec->num) {
		mutex_lock(&bebob->mutex);
		if (spec->set(bebob, value) >= 0)
			changed = 1;
		mutex_unlock(&bebob->mutex);
	}

	return changed;
}

/*
 * Global Control: Sampling Rate Control
 *
 * refer to snd_bebob_rate_table.
 */
static int
control_sampling_rate_info(struct snd_kcontrol *kctl,
			   struct snd_ctl_elem_info *einf)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	int i, value;

	/* maximum value for user */
	einf->value.enumerated.items = 0;
	for (i = 0; i < ARRAY_SIZE(snd_bebob_rate_table); i++)
		if ((bebob->tx_stream_formations[i].pcm > 0) &&
		    (bebob->rx_stream_formations[i].pcm > 0))
			einf->value.enumerated.items++;

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	/* skip unsupported sampling rates */
	value = einf->value.enumerated.item;
	for (i = 0; i < ARRAY_SIZE(snd_bebob_rate_table); i++) {
		if ((bebob->tx_stream_formations[i].pcm == 0) ||
		    (bebob->rx_stream_formations[i].pcm == 0))
			continue;
		else if (value == 0)
			break;
		else
			value--;
	}

	sprintf(einf->value.enumerated.name, "%dHz", snd_bebob_rate_table[i]);

	return 0;
}
static int
control_sampling_rate_get(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	int i, in_rate, out_rate, index, err;

	mutex_lock(&bebob->mutex);

	err = avc_generic_get_sampling_rate(bebob->unit, &out_rate, 0, 0);
	if (err < 0)
		goto end;
	err = avc_generic_get_sampling_rate(bebob->unit, &in_rate, 1, 0);
	if (err < 0)
		goto end;

	if (out_rate != in_rate)
		goto end;

	for (index = 0; index < ARRAY_SIZE(snd_bebob_rate_table); index++)
		if (snd_bebob_rate_table[index] == out_rate)
			break;

	uval->value.enumerated.item[0] = 0;
	for (i = 0; i < index; i++)
		if ((bebob->tx_stream_formations[i].pcm != 0) ||
		    (bebob->rx_stream_formations[i].pcm != 0))
			uval->value.enumerated.item[0]++;

end:
	mutex_unlock(&bebob->mutex);
	return err;
}
static int
control_sampling_rate_put(struct snd_kcontrol *kctl,
			  struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	int value, index, rate, err, changed = 0;

	/* get index from user value*/
	value = uval->value.enumerated.item[0];
	for (index = 0; index < ARRAY_SIZE(snd_bebob_rate_table); index++) {
		if ((bebob->tx_stream_formations[index].pcm == 0) ||
		    (bebob->rx_stream_formations[index].pcm == 0))
			continue;
		else if (value == 0)
			break;
		else
			value--;
	}

	rate = snd_bebob_rate_table[index];

	mutex_lock(&bebob->mutex);
	err = avc_generic_set_sampling_rate(bebob->unit, rate, 0, 0);
	if (err < 0)
		goto end;
	err = avc_generic_set_sampling_rate(bebob->unit, rate, 1, 0);
	if (err < 0)
		goto end;
	
	/* prevent from failure of getting command just after setting */
	msleep(100);
	changed = 1;

end:
	mutex_unlock(&bebob->mutex);
	return changed;
}

/*
 * Global Control: Clock Source Control
 */
static int control_clock_source_info(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_info *einf)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_clock_spec *spec = bebob->spec->clock;

	einf->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	einf->count = 1;
	einf->value.enumerated.items = spec->num;

	if (einf->value.enumerated.item >= einf->value.enumerated.items)
		einf->value.enumerated.item = einf->value.enumerated.items - 1;

	strcpy(einf->value.enumerated.name,
	       spec->labels[einf->value.enumerated.item]);

	return 0;
}
static int control_clock_source_get(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_clock_spec *spec = bebob->spec->clock;
	int id;

	mutex_lock(&bebob->mutex);
	if (spec->get(bebob, &id) >= 0)
		uval->value.enumerated.item[0] = id;
	mutex_unlock(&bebob->mutex);

	return 0;
}
static int control_clock_source_put(struct snd_kcontrol *kctl,
				    struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_clock_spec *spec = bebob->spec->clock;
	int value, changed = 0;

	value = uval->value.enumerated.item[0];

	if (value < spec->num) {
		mutex_lock(&bebob->mutex);
		if (spec->set(bebob, value) >= 0)
			changed = 1;
		mutex_unlock(&bebob->mutex);
	}

	msleep(150);

	return changed;
}

/*
 * Global Control: Clock Sync Status
 */
static int control_clock_sync_status_info(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_info *einf)
{
	einf->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	einf->count = 1;
	einf->value.integer.min = 0;
	einf->value.integer.max = 1;

	return 0;
}
static int control_clock_sync_status_get(struct snd_kcontrol *kctl,
					 struct snd_ctl_elem_value *uval)
{
	struct snd_bebob *bebob = snd_kcontrol_chip(kctl);
	struct snd_bebob_clock_spec *spec = bebob->spec->clock;
	bool synced = 0;

	mutex_lock(&bebob->mutex);
	if (spec->synced(bebob, &synced) >= 0) {
		if (synced == false)
			uval->value.enumerated.item[0] = 0;
		else
			uval->value.enumerated.item[0] = 1;
	}
	mutex_unlock(&bebob->mutex);

	return 0;
}
static struct snd_kcontrol_new global_clock_source_control = {
	.name	= "Clock Source",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info	= control_clock_source_info,
	.get	= control_clock_source_get,
	.put	= control_clock_source_put
};

static struct snd_kcontrol_new global_clock_sync_status = {
	.name	= "Clock Sync Status",
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.access	= SNDRV_CTL_ELEM_ACCESS_READ,
	.info	= control_clock_sync_status_info,
	.get	= control_clock_sync_status_get,
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

int snd_bebob_create_control_devices(struct snd_bebob *bebob)
{
	int err;
	struct snd_kcontrol *kctl;

	kctl = snd_ctl_new1(&global_sampling_rate_control, bebob);
	err = snd_ctl_add(bebob->card, kctl);
	if (err < 0)
		goto end;

	if (bebob->spec->clock != NULL) {
		kctl = snd_ctl_new1(&global_clock_source_control, bebob);
		err = snd_ctl_add(bebob->card, kctl);
		if (err < 0)
			goto end;

		kctl = snd_ctl_new1(&global_clock_sync_status, bebob);
		err = snd_ctl_add(bebob->card, kctl);
		if (err < 0)
			goto end;
	}

	if (bebob->spec->dig_iface != NULL) {
		kctl = snd_ctl_new1(&global_digital_interface_control, bebob);
		err = snd_ctl_add(bebob->card, kctl);
		if (err < 0)
			goto end;
	}

	if (bebob->spec->meter != NULL) {
		kctl = snd_ctl_new1(&physical_metering, bebob);
		err = snd_ctl_add(bebob->card, kctl);
		if (err < 0)
			goto end;
	}

end:
	return err;
}
