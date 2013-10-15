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
 * should be accompanied. If changing the state, a LED on the device starts to
 * blink and sound nothins even if streaming. There seems to be one way to
 * revocer this state, just power-off. GO46 is better for this purpose,
 * stand-alone mixer.
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
	buf[0]  = 0x01;
	buf[1]  = 0xff;
	buf[2]  = 0x00;
	buf[3]  = 0x00;
	buf[4]  = 0x07;
	buf[5]  = 0xf5;
	buf[6]  = 0x00;
	buf[7]  = 0x00;
	buf[8]  = 0x01;
	buf[9]  = 0x00;
	buf[10] = 0x00;
	buf[11] = 0x00;

	err = fcp_avc_transaction(bebob->unit, buf, 12, buf, 12, 0);
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

static char *clock_labels[] = {"Internal", "SPDIF"};

static int clock_set(struct snd_bebob *bebob, int id)
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

static int clock_get(struct snd_bebob *bebob, int *id)
{
	return avc_audio_get_selector(bebob->unit, 0, 4, id);
}

static int
check_prepared(struct snd_bebob *bebob, bool *prepared)
{
	int err;
	u8 *buf;

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* NOTE: this command should be NOTIFY but use STATUS to be simple */
	buf[0] = 0x01;	/* STATUS */
	buf[1] = 0xFF;	/* UNIT */
	buf[2] = 0x00;	/* Vendor Specific Command */
	buf[3] = 0x01;
	buf[4] = 0x02;
	buf[5] = 0x03;
	buf[6] = 0x21;
	buf[7] = 0xff;	/* preparation status */

	err = fcp_avc_transaction(bebob->unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	/* TOOD: */
	if ((err < 6) || (buf[0] != 0x0c)) {
		dev_err(&bebob->unit->device,
			"failed to get preparation status by Yamaha command");
		err = -EIO;
		goto end;
	}

	/*
	 * Just after changing sampling rate or formation of stream, this value
	 * becomes 0xff. When the device is prepared, this value bacomes 0x00.
	 * Once stream starts, this value becomes 0x01 and keep till next
	 * changing.
	 */
	*prepared = (buf[7] != 0xff);
end:
	kfree(buf);
	return true;
}
static int
clock_synced(struct snd_bebob *bebob, bool *synced)
{
	/* because the clock is changable when detecting digital input */
	*synced = true;
	return 0;
}

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
	.clock		= &clock_spec,
	.dig_iface	= NULL,
	.meter		= NULL
};
