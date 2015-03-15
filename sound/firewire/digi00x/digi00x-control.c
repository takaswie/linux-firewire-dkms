/*
 * digi00x_control.c - a part of driver for Digidesign 002/003 devices
 *
 * Copyright (c) Damien Zammit <damien@zamaudio.com>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "digi00x.h"

enum control_action { CTL_READ, CTL_WRITE };

static int dg00x_clock_command(struct snd_dg00x *dg00x, int *value,
			     enum control_action action)
{
	int err;
	if (action == CTL_READ) {
		err = snd_dg00x_stream_get_clock(dg00x, value);
	} else {
		err = snd_dg00x_stream_set_clock(dg00x, *value);
	}
	return err;
}

static int dg00x_clock_get(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_dg00x *dg00x = control->private_data;
	value->value.enumerated.item[0] = dg00x->clock;
	return 0;
}

static int dg00x_clock_put(struct snd_kcontrol *control,
			 struct snd_ctl_elem_value *value)
{
	struct snd_dg00x *dg00x = control->private_data;
	int err;

	int cur_val, new_val;

	cur_val = dg00x->clock;
	new_val = value->value.enumerated.item[0];

	err = dg00x_clock_command(dg00x, &new_val, CTL_WRITE);
	if (err < 0)
		goto err;
	dg00x->clock = new_val;

err:
	return err < 0 ? err : 1;
}

static int dg00x_clock_info(struct snd_kcontrol *control,
			    struct snd_ctl_elem_info *info)
{
	static const char *const texts[4] = {
		"Internal",
		"S/PDIF",
		"ADAT",
		"WordClock"
	};

	return snd_ctl_enum_info(info, 1, ARRAY_SIZE(texts), texts);
}

static struct snd_kcontrol_new snd_dg00x_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Clock Source",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = dg00x_clock_info,
		.get = dg00x_clock_get,
		.put = dg00x_clock_put,
		.private_value = 0
	},
};

int snd_dg00x_create_mixer(struct snd_dg00x *dg00x)
{
	unsigned int i;
	int err;

	err = dg00x_clock_command(dg00x, &dg00x->clock, CTL_READ);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(snd_dg00x_controls); ++i) {
		err = snd_ctl_add(dg00x->card,
				  snd_ctl_new1(&snd_dg00x_controls[i], dg00x));
		if (err < 0)
			return err;
	}

	return 0;
}
