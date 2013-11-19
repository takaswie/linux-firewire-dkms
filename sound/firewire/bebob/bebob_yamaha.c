/*
 * bebob_yamaha.c - a part of driver for BeBoB based devices
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

/*
 * NOTE:
 * Yamaha GO44 is not considered to be used as stand-alone mixer. So any streams
 * must be accompanied. If changing the state, a LED on the device starts to
 * blink and its sync status is false. In this state, the device sounds nothing
 * even if streaming. To start streaming at the current sampling rate is only
 * way to revocer this state. GO46 is better for stand-alone mixer.
 *
 * Both of them have a capability to change its sampling rate up to 192.0kHz.
 * At 192.0kHz, the device reports 4 PCM-in, 1 MIDI-in, 6 PCM-out, 1 MIDI-out.
 * But Yamaha's driver reduce 2 PCM-in, 1 MIDI-in, 2 PCM-out, 1 MIDI-out to use
 * 'Extended Stream Format Information Command - Single Request' in 'Additional
 * AVC commands' defined by BridgeCo.
 * This ALSA driver don't do this because a bit tiresome. Then isochronous
 * streaming with many asynchronous transactions brings sounds with noises.
 * Unfortunately current 'ffado-mixer' generated many asynchronous transaction
 * to observe device's state, mainly check cmp connection and signal format. I
 * reccomend users to close ffado-mixer at 192.0kHz if mixer is needless.
 */

static int
detect_dig_in(struct snd_bebob *bebob, int *detect)
{
	int err;
	u8 *buf;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* This is a vendor dependent command */
	buf[0]  = 0x01;	/* STATUS */
	buf[1]  = 0xff;	/* UNIT */
	buf[2]  = 0x00;	/* Vendor Dependent command */
	buf[3]  = 0x00;	/* Company ID high */
	buf[4]  = 0x07;	/* Company ID middle */
	buf[5]  = 0xf5;	/* Company ID low */
	buf[6]  = 0x00;	/* Unknown Subfunction*/
	buf[7]  = 0x00;	/* Unknown */
	buf[8]  = 0x01; /* Unknown */
	buf[9]  = 0x00;	/* Unknown */
	buf[10] = 0x00;	/* Unknown */
	buf[11] = 0x00;	/* Unknown */

	/* do transaction and check buf[1-6] are the same against command */
	err = fcp_avc_transaction(bebob->unit, buf, 12, buf, 12,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) |
				  BIT(5) | BIT(6));
	if (err < 0)
		goto end;
	/* IMPLEMENTED/STABLE is OK */
	if ((err < 6) || (buf[0] != 0x0c)){
		dev_err(&bebob->unit->device,
			"failed to detect clock source 0x%02X\n",
			buf[0]);
		err = -EIO;
		goto end;
	}

	/* when digital clock input exists, 10th byte is 0x01 */
	*detect = (buf[9] > 0);
	err = 0;
end:
	return err;
}

static int
get_sync_status(struct snd_bebob *bebob, bool *sync)
{
	u8 *buf;
	int err;

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;  /* AV/C STATUS */
	buf[1] = 0xFF;  /* UNIT */
	buf[2] = 0x00;  /* Vendor Specific Command */
	buf[3] = 0x01;	/* Company ID high */
	buf[4] = 0x02;	/* Company ID middle */
	buf[5] = 0x03;	/* Company ID low */
	buf[6] = 0x21;	/* unknown subfunction */
	buf[7] = 0xff;  /* status */

	err = fcp_avc_transaction(bebob->unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) |
				  BIT(5) | BIT(6));
	if (err < 0)
		goto end;
	if ((err < 6) || (buf[0] != 0x0c)) {
		dev_err(&bebob->unit->device,
			"failed to get sync status\n");
		err = -EIO;
		goto end;
	}

	/* 0x00 if losing sync */
	*sync = (buf[7] != 0x00);
	err = 0;
end:
	kfree(buf);
	return err;
}

static char *clock_labels[] = {"Internal", "SPDIF"};
static int
clock_set(struct snd_bebob *bebob, int id)
{
	int err, detect;

	if (id > 0) {
		err = detect_dig_in(bebob, &detect);
		if ((err < 0) || (detect == 0))
			return -EIO;
	}

	err = avc_audio_set_selector(bebob->unit, 0, 4, id);
	if (err < 0)
		goto end;

	/*
	 * Yamaha BeBob returns 'IN TRANSITION' status just after returning to
	 * internal clock
	 */
	if (id == 0)
		msleep(1500);

end:
	return err;
}
static int
clock_get(struct snd_bebob *bebob, int *id)
{
	return avc_audio_get_selector(bebob->unit, 0, 4, id);
}
static int
clock_synced(struct snd_bebob *bebob, bool *synced)
{
	return get_sync_status(bebob, synced);
}

static struct snd_bebob_freq_spec freq_spec = {
	.get	= &snd_bebob_stream_get_rate,
	.set	= &snd_bebob_stream_set_rate
};
static struct snd_bebob_clock_spec clock_spec = {
	.num	= ARRAY_SIZE(clock_labels),
	.labels	= clock_labels,
	.get	= &clock_get,
	.set	= &clock_set,
	.synced	= &clock_synced
};
struct snd_bebob_spec yamaha_go_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &freq_spec,
	.clock		= &clock_spec,
	.dig_iface	= NULL,
	.meter		= NULL
};
