/*
 * bebob_presonus.c - a part of driver for BeBoB based devices
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

#include "./bebob.h"

/* FIREBOX specific controls */
static char *firebox_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "Digital Coaxial"
};
static int
firebox_clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;
	unsigned int stype, sid, pid;

	err = avc_ccm_get_sig_src(bebob->unit,
				  &stype, &sid, &pid, 0x0c, 0x00, 0x05);
	if (err < 0)
		goto end;

	if ((stype != 0x1f) && (sid != 0x07) && (pid != 0x83))
		*id = 0;
	else
		*id = 1;
end:
	return err;
}
static int
firebox_clk_src_set(struct snd_bebob *bebob, unsigned int id)
{
	unsigned int stype, sid, pid;

	if (id != 1) {
		stype = 0x1f;
		sid = 0x07;
		pid = 0x83;
	} else {
		stype = 0x0c;
		sid = 0x00;
		pid = 0x01;
	}

	return avc_ccm_set_sig_src(bebob->unit,
				   stype, sid, pid, 0x0c, 0x00, 0x05);
}

/* FIREBOX specification */
static struct snd_bebob_clock_spec firebox_clk_spec = {
	.num		= ARRAY_SIZE(firebox_clk_src_labels),
	.labels		= firebox_clk_src_labels,
	.get_src	= &firebox_clk_src_get,
	.set_src	= &firebox_clk_src_set,
	.get_freq	= &snd_bebob_stream_get_rate,
	.set_freq	= &snd_bebob_stream_set_rate,
	.synced		= NULL
};
struct snd_bebob_spec presonus_firebox_spec = {
	.load	= NULL,
	.clock	= &firebox_clk_spec,
	.meter	= NULL
};
