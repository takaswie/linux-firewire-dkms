/*
 * bebob_maudio.c - a part of driver for BeBoB based devices
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
 * For Firewire 410 and Firewire Audiophile, this module requests firmware
 * version 5058 or later. With former version, BeBoB chipset needs downloading
 * firmware and the driver should do this. To do this with ALSA, I need to
 * examinate whether it's OK or not to include firmware binary blob to
 * alsa-firmware package. With later version, the firmware is in ROM of chipset
 * and the driver just send a cue to load it when probing the device. This cue
 * is sent just once.
 *
 * For streaming, both of output and input streams are needed for Firewire 410
 * and Ozonic. The single stream is OK for the other devices even if the clock
 * source is not SYT-Match (and no devices use SYT-Match).
 *
 * Without streaming, the devices except for Firewire Audiophile can mix any
 * input and output. For this purpose, use ffado-mixer. Audiophile need to
 * any stream for this purpose.
 *
 * Firewire 1814 and ProjectMix I/O uses special firmware. It will be freezed
 * if receiving any commands which the firmware can't understand. These devices
 * utilize completely different system to control. It is read/write transaction
 * directly into a certain address.
 */

#define MAUDIO_BOOTLOADER_CUE1	0x01000000
#define MAUDIO_BOOTLOADER_CUE2	0x00001101
#define MAUDIO_BOOTLOADER_CUE3	0x00000000

#define MAUDIO_SPECIFIC_ADDRESS	0xffc700000000

#define METER_OFFSET		0x00600000

/* some device has sync info after metering data */
#define METER_SIZE_SPECIAL	84	/* with sync info */
#define METER_SIZE_FW410	76	/* with sync info */
#define METER_SIZE_AUDIOPHILE	60	/* with sync info */
#define METER_SIZE_SOLO		52	/* with sync info */
#define METER_SIZE_OZONIC	48
#define METER_SIZE_NRV10	80

/* labels for metering */
#define ANA_IN		"Analog In"
#define ANA_OUT		"Analog Out"
#define DIG_IN		"Digital In"
#define SPDIF_IN	"S/PDIF In"
#define ADAT_IN		"ADAT In"
#define DIG_OUT		"Digital Out"
#define SPDIF_OUT	"S/PDIF Out"
#define ADAT_OUT	"ADAT Out"
#define STRM_IN		"Stream In"
#define AUX_OUT		"Aux Out"
#define HP_OUT		"HP Out"
/* for NRV */
#define UNKNOWN_METER	"Unknown"

/*
 * FW1814/ProjectMix don't use AVC for control. The driver cannot refer to
 * current parameters by asynchronous transaction. The driver is allowed to
 * write transaction so MUST remember the current values.
 */
#define	MAUDIO_CONTROL_OFFSET	0x00700000
/*
 * GAIN for inputs:
 * Write 32bit. upper 16bit for left chennal and lower 16bit for right.
 * The value is between 0x8000(low) to 0x0000(high) as the same as '10.3.1
 * Volume Control' in 'AV/C Audio Subunit Specification 1.0 (1394TA 1999008)'.
 */
#define	GAIN_STM_12_IN		0x00
#define	GAIN_STM_34_IN		0x04
#define GAIN_ANA_12_OUT		0x08
#define GAIN_ANA_34_OUT		0x0c
#define GAIN_ANA_12_IN		0x10
#define GAIN_ANA_34_IN		0x14
#define GAIN_ANA_56_IN		0x18
#define GAIN_ANA_78_IN		0x1c
#define GAIN_SPDIF_12_IN	0x20
#define GAIN_ADAT_12_IN		0x24
#define GAIN_ADAT_34_IN		0x28
#define GAIN_ADAT_56_IN		0x2c
#define GAIN_ADAT_78_IN		0x30
#define GAIN_AUX_12_OUT		0x34
#define GAIN_HP_12_OUT		0x38
#define GAIN_HP_34_OUT		0x3c
/*
 * LR balance:
 * Write 32 bit, upper 16bit for left channel and lower 16bit for right.
 * The value is between 0x800(L) to 0x7FFE(R) as the same as '10.3.3 LR Balance
 * Control' in 'AV/C Audio Subunit Specification 1.0 (1394TA 1999008)'.
 */
#define LR_ANA_12_IN	0x40
#define LR_ANA_34_IN	0x44
#define LR_ANA_56_IN	0x48
#define LR_ANA_78_IN	0x4c
#define LR_SPDIF_12_IN	0x50
#define LR_ADAT_12_IN	0x54
#define LR_ADAT_34_IN	0x58
#define LR_ADAT_56_IN	0x5c
#define LR_ADAT_78_IN	0x60
/*
 * AUX inputs:
 * This is the same as 'gain' control above.
 */
#define AUX_STM_12_IN	0x64
#define AUX_STM_34_IN	0x68
#define AUX_ANA_12_IN	0x6c
#define AUX_ANA_34_IN	0x70
#define AUX_ANA_56_IN	0x74
#define AUX_ANA_78_IN	0x78
#define AUX_SPDIF_12_IN	0x7c
#define AUX_ADAT_12_IN	0x80
#define AUX_ADAT_34_IN	0x84
#define AUX_ADAT_56_IN	0x88
#define AUX_ADAT_78_IN	0x8c
/*
 * MIXER inputs:
 * There are bit flags. If flag is 0x01, it means on.
 *
 *  MIX_ANA_DIG_IN:
 *  Write 32bits, upper 16bit for digital inputs and lowe 16bit for analog inputs.
 *   Digital inputs:
 *    Lower 2bits are used. upper for 'to Mix3/4' and lower for 'to Mix1/2'.
 *   Analog inputs:
 *    Lower 8bits are used. upper 4bits for 'to Mix3/4' and lower for 'to
 *    Mix1/2'. Inner the 4bit, for 'from Ana7/8', for 'from Ana5/6', for 'from
 *    Ana3/4', for 'from Ana1/2'.
 *
 *  MIX_STM_IN:
 *   Write 32bits, lower 4bits are used. upper 2bits for 'from Stm1/2' and lower
 *   for 'from Stm3/4'. Inner the 2bits, for 'to Mix3/4' and for 'to
 *   Mix1/2'.
 */
#define MIX_ANA_DIG_IN	0x90
#define MIX_STM_IN	0x94
/*
 * SRC for output:
 * Write 32bit. There are bit flags. If the flag is 0x01, it means on.
 *
 *  SRC_HP_OUT:
 *  Lower 3bits are used, 'from Aux12', 'from Mix34', 'from
 *  Mix12'.
 *
 *  SRC_ANA_OUT:
 *  Lower 2 bits are used, 'to Ana34', 'to Ana12'. If bit is 0x01, it
 *  means 'from Aux12' else 'From Mix12 (or Mix34)'.
 */
#define SRC_HP_OUT	0x98
#define SRC_ANA_OUT	0x9c

/* If we make any transaction to load firmware, the operation may failed. */
/* TODO: change snd-firewire-lib and use it */
static int
run_a_transaction(struct fw_unit *unit, int tcode,
		  u64 offset, void *buffer, size_t length)
{
	struct fw_device *device = fw_parent_device(unit);
	int generation, rcode;

	generation = device->generation;
	smp_rmb();	/* node id vs. generation*/
	rcode = fw_run_transaction(device->card, tcode,
				   device->node_id, generation,
				   device->max_speed, offset,
				   buffer, length);
	if (rcode == RCODE_COMPLETE)
		return 0;

	dev_err(&unit->device, "Failed to send a queue to load firmware\n");
	return -EIO;
}

/*
 * For some M-Audio devices, this module just send cue to load
 * firmware. After loading, the device generates bus reset and
 * newly detected.
 */
static int
firmware_load(struct fw_unit *unit, const struct ieee1394_device_id *entry)
{
	__be32 cues[3];

	cues[0] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE1);
	cues[1] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE2);
	cues[2] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE3);

	return run_a_transaction(unit, TCODE_WRITE_BLOCK_REQUEST,
				 BEBOB_ADDR_REG_REQ, cues, sizeof(cues));
}

static inline int
get_meter(struct snd_bebob *bebob, void *buf, int size)
{
	return snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
				  MAUDIO_SPECIFIC_ADDRESS + METER_OFFSET,
				  buf, size);
}

/*
 * BeBoB don't tell drivers to detect digital input, just show clock sync or not.
 */
static int
get_clock_freq(struct snd_bebob *bebob, int size, int *rate)
{
	int err;
	u8 *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, buf, size);
	if (err < 0)
		goto end;

	/* if synced, this value is the same of SFC of FDF in CIP header */
	if (buf[size - 2] == 0xff)
		err = -EIO;

	*rate = get_rate_from_sfc(buf[size - 2]);
	if (*rate < 0)
		err = *rate;

end:
	kfree(buf);
	return err;
}

/*
 * FW1814 and ProjectMix cannot give a way to read its control. So drivers
 * should remember the value for each parameters. Windows driver, after loading
 * firmware, write all parameters just after flushing.
 */
static int
reset_device(struct snd_bebob *bebob)
{
	u8 buf[8];

	buf[0] = 0x00;	/* control */
	buf[1] = 0xff;	/* unit */
	buf[2] = 0x00;	/* vendor dependent command */
	buf[3] = 0x02;	/* unknown */
	buf[4] = 0x00;	/* unknown */
	buf[5] = 0x00;	/* unknown */
	buf[6] = 0x00;	/* unknown */
	buf[7] = 0x00;	/* unknown */

	return fcp_avc_transaction(bebob->unit, buf, 8, buf, 8, 0);
}

/*
 * dig_fmt: 0x00:S/PDIF, 0x01:ADAT
 * clk_lock: 0x00:unlock, 0x01:lock
 */
static int
special_set_clock_params(struct snd_bebob *bebob, int clk_src,
			 int in_dig_fmt, int out_dig_fmt, int clk_lock)
{
	int err;
	u8 *buf;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0]  = 0x00;		/* CONTROL */
	buf[1]  = 0xff;		/* UNIT */
	buf[2]  = 0x00;		/* vendor dependent */
	buf[3]  = 0x04;
	buf[4]  = 0x00;
	buf[5]  = 0x04;			/* has 4 parameters */
	buf[6]  = 0xff & clk_src;	/* clock source */
	buf[7]  = 0xff & in_dig_fmt;	/* input digital format */
	buf[8]  = 0xff & out_dig_fmt;	/* output digital format */
	buf[9]  = 0xff & clk_lock;	/* lock these settings */
	buf[10] = 0x00;		/* padding  */
	buf[11] = 0x00;		/* padding */

	err = fcp_avc_transaction(bebob->unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	if ((err < 6) || (buf[0] != 0x09)) {
		dev_err(&bebob->unit->device,
			"failed to set clock params\n");
		err = -EIO;
		goto end;
	}

	bebob->clk_src		= buf[6];
	/* handle both of input and output in this member */
	bebob->in_dig_fmt	= buf[7];
	bebob->out_dig_fmt	= buf[8];
	bebob->clk_lock		= buf[9];

	err = 0;
end:
	kfree(buf);
	return err;
}
/*
 * For special customized devices.
 * The driver can't receive response from this firmware frequently.
 * So need to reduce execution of command.
 */
static void
special_stream_formation_set(struct snd_bebob *bebob)
{
	int i;

	/*
	 * the stream formation is different depending on digital interface
	 */
	if (bebob->in_dig_fmt== 0x01) {
		bebob->tx_stream_formations[3].pcm = 16;
		bebob->tx_stream_formations[4].pcm = 16;
		bebob->tx_stream_formations[5].pcm = 12;
		bebob->tx_stream_formations[6].pcm = 12;
		bebob->tx_stream_formations[7].pcm = 2;
		bebob->tx_stream_formations[8].pcm = 2;
	} else {
		bebob->tx_stream_formations[3].pcm = 10;
		bebob->tx_stream_formations[4].pcm = 10;
		bebob->tx_stream_formations[5].pcm = 10;
		bebob->tx_stream_formations[6].pcm = 10;
		bebob->tx_stream_formations[7].pcm = 2;
		bebob->tx_stream_formations[8].pcm = 2;
	}

	if (bebob->out_dig_fmt == 0x01) {
		bebob->rx_stream_formations[3].pcm = 12;
		bebob->rx_stream_formations[4].pcm = 12;
		bebob->rx_stream_formations[5].pcm = 8;
		bebob->rx_stream_formations[6].pcm = 8;
		bebob->rx_stream_formations[7].pcm = 4;
		bebob->rx_stream_formations[8].pcm = 4;
	} else {
		bebob->rx_stream_formations[3].pcm = 6;
		bebob->rx_stream_formations[4].pcm = 6;
		bebob->rx_stream_formations[5].pcm = 6;
		bebob->rx_stream_formations[6].pcm = 6;
		bebob->rx_stream_formations[7].pcm = 4;
		bebob->rx_stream_formations[8].pcm = 4;
	}

	for (i = 3; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i++) {
		bebob->tx_stream_formations[i].midi = 1;
		bebob->rx_stream_formations[i].midi = 1;
	}
}
static int
special_discover(struct snd_bebob *bebob)
{
	int err;

	/* initialize these parameters because doesn't allow driver to ask */
	err = special_set_clock_params(bebob, 0x03, 0x00, 0x00, 0x00);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"failed to initialize clock params\n");
	}

	err = avc_audio_get_selector(bebob->unit, 0x00, 0x04,
				     &bebob->in_dig_iface);
	if (err < 0) {
		dev_err(&bebob->unit->device,
			"failed to get current dig iface.");
	}

	special_stream_formation_set(bebob);

	/* TODO: ProjectMix has 2? */
	bebob->midi_input_ports = 1;
	bebob->midi_output_ports = 1;

	bebob->maudio_special_quirk = true;

	return 0;
}

static int special_get_freq(struct snd_bebob *bebob, int *rate)
{
	return get_clock_freq(bebob, METER_SIZE_SPECIAL, rate);
}

static char *special_clock_labels[] = {
	"Internal with Digital Mute", "Digital",
	"Word Clock", "Internal"};
static int
special_clock_get(struct snd_bebob *bebob, int *id)
{
	*id = bebob->clk_src;
	return 0;
}
static int
special_clock_set(struct snd_bebob *bebob, int id)
{
	return special_set_clock_params(bebob, id,
					bebob->in_dig_fmt, bebob->out_dig_fmt,
					bebob->clk_lock);
}
static int
special_clock_synced(struct snd_bebob *bebob, bool *synced)
{
	int rate, err;
	err = get_clock_freq(bebob, METER_SIZE_SPECIAL, &rate);
	*synced = !(err < 0);
	return err;
}

static char *special_dig_iface_labels[] = {
	"S/PDIF Optical", "S/PDIF Coaxial", "ADAT Optical" 
};
static int
special_dig_iface_get(struct snd_bebob *bebob, int *id)
{
	/* for simplicity, the same value for input/output */
	*id = (bebob->in_dig_fmt << 1) | (bebob->in_dig_iface & 0x01);

	return 0;
}
static int
special_dig_iface_set(struct snd_bebob *bebob, int id)
{
	int err;
	int dig_fmt;
	int in_dig_iface;

	dig_fmt = (id >> 1) & 0x01;
	in_dig_iface = id & 0x01;

	/* for simplicity, the same value for input/output */
	err = special_set_clock_params(bebob, bebob->clk_src, dig_fmt, dig_fmt,
				       bebob->clk_lock);
	if (err < 0)
		goto end;

	err = avc_audio_set_selector(bebob->unit, 0x00, 0x04, in_dig_iface);
	if (err < 0)
		goto end;

	bebob->in_dig_iface = in_dig_iface;

	special_stream_formation_set(bebob);
end:
	return err;
}
static char *special_meter_labels[] = {
	ANA_IN, ANA_IN, ANA_IN, ANA_IN,
	SPDIF_IN,
	ADAT_IN, ADAT_IN, ADAT_IN, ADAT_IN,
	ANA_OUT, ANA_OUT,
	SPDIF_OUT,
	ADAT_OUT, ADAT_OUT, ADAT_OUT, ADAT_OUT,
	HP_OUT, HP_OUT,
	AUX_OUT
};
static int
special_meter_get(struct snd_bebob *bebob, u32 *target, int size)
{
	u16 *buf;
	int i, c, channels, err;

	channels = ARRAY_SIZE(special_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	/* omit last 5 bytes because it's clock info. */
	buf = kmalloc(METER_SIZE_SPECIAL - 4, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, (void *)buf, METER_SIZE_SPECIAL - 4);
	if (err < 0)
		goto end;

	/* some channels are not used and convert u16 to u32 */
	for (i = 0, c = 2; c < channels + 2; c++)
		target[i++] = be16_to_cpu(buf[c]) << 8;
end:
	kfree(buf);
	return err;
}

/* Firewire 410 specific controls */
static char *fw410_clock_labels[] = {
	"Internal", "Digital Optical", "Digital Coaxial"
};
static int
fw410_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, stype, sid, pid;

	err = avc_ccm_get_signal_source(bebob->unit,
					&stype, &sid, &pid, 0x0c, 0x00, 0x01);
	if (err < 0)
		goto end;

	*id = 0;
	if ((stype == 0x1f) && (sid == 0x07)) {
		if (pid == 0x82)
			*id = 2;
		else if (pid == 0x83)
			*id = 1;
	}
end:
	return err;
}
static int
fw410_clock_set(struct snd_bebob *bebob, int id)
{
	int stype, sid, pid;

	if (id == 0) {
		stype = 0x0c;
		sid = 0x00;
		pid = 0x01;
	} else if (id == 1) {
		stype = 0x1f;
		sid = 0x07;
		pid = 0x83;
	} else {
		stype = 0x1f;
		sid = 0x07;
		pid = 0x82;
	}

	return avc_ccm_set_signal_source(bebob->unit,
					stype, sid, pid, 0x0c, 0x00, 0x01);
}
static int
fw410_clock_synced(struct snd_bebob *bebob, bool *synced)
{
	int rate, err;
	err = get_clock_freq(bebob, METER_SIZE_FW410, &rate);
	*synced = !(err < 0);
	return err;
}
static char *fw410_dig_iface_labels[] = {
	"S/PDIF Optical", "S/PDIF Coaxial"
};
static int
fw410_dig_iface_get(struct snd_bebob *bebob, int *id)
{
	return avc_audio_get_selector(bebob->unit, 0, 1, id);
}
static int
fw410_dig_iface_set(struct snd_bebob *bebob, int id)
{
	return avc_audio_set_selector(bebob->unit, 0, 1, id);
}
static char *fw410_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT
};
static int
fw410_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
{

	int c, channels, err;

	channels = ARRAY_SIZE(fw410_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	/* omit last 4 bytes because it's clock info. */
	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	for (c = 0; c < channels; c++)
		buf[c] = be32_to_cpu(buf[c]);

end:
	return err;
}

/* Firewire Audiophile specific controls */
static char *audiophile_clock_labels[] = {
	"Internal", "Digital Coaxial"
};
static int
audiophile_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, stype, sid, pid;

	err = avc_ccm_get_signal_source(bebob->unit,
					&stype, &sid, &pid, 0x0c, 0x00, 0x01);
	if (err < 0)
		goto end;

	if ((stype == 0x1f) && (sid == 0x07) && (pid == 0x82))
		*id = 1;
	else
		*id = 0;
end:
	return err;
}
static int
audiophile_clock_set(struct snd_bebob *bebob, int id)
{
	int stype, sid, pid;

	if (id == 0) {
		stype = 0x0c;
		sid = 0x00;
		pid = 0x01;
	} else {
		stype = 0x1f;
		sid = 0x07;
		pid = 0x82;
	}

	return avc_ccm_set_signal_source(bebob->unit,
					 stype, sid, pid, 0x0c, 0x00, 0x01);
}
static int
audiophile_clock_synced(struct snd_bebob *bebob, bool *synced)
{
	int rate, err;
	err = get_clock_freq(bebob, METER_SIZE_AUDIOPHILE, &rate);
	*synced = !(err < 0);
	return err;
}
static char *audiophile_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT, AUX_OUT,
};
static int
audiophile_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
{
	int c, channels, err;

	channels = ARRAY_SIZE(audiophile_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	/* omit last 4 bytes because it's clock info. */
	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	for (c = 0; c < channels; c++)
		buf[c] = be32_to_cpu(buf[c]);

end:
	return err;
}

/* Firewire Solo specific controls */
static char *solo_clock_labels[] = {
	"Internal", "Digital Coaxial"
};
static int
solo_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, stype, sid, pid;

	err = avc_ccm_get_signal_source(bebob->unit,
					&stype, &sid, &pid, 0x0c, 0x00, 0x01);
	if (err < 0)
		goto end;

	if ((stype == 0x1f) && (sid = 0x07) && (pid== 0x81))
		*id = 1;
	else
		*id = 0;
end:
	return err;
}
static int
solo_clock_set(struct snd_bebob *bebob, int id)
{
	int stype, sid, pid;

	if (id == 0) {
		stype = 0x0c;
		sid = 0x00;
		pid = 0x01;
	} else {
		stype = 0x1f;
		sid = 0x07;
		pid = 0x81;
	}

	return avc_ccm_set_signal_source(bebob->unit,
					 stype, sid, pid, 0x0c, 0x00, 0x01);
}
static int
solo_clock_synced(struct snd_bebob *bebob, bool *synced)
{
	int rate, err;
	err = get_clock_freq(bebob, METER_SIZE_SOLO, &rate);
	*synced = !(err < 0);
	return err;
}
static char *solo_meter_labels[] = {
	ANA_IN, DIG_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, DIG_OUT
};
static int
solo_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
{
	int c, err;
	u32 tmp;

	if (size < ARRAY_SIZE(solo_meter_labels) * 2 * sizeof(u32))
		return -ENOMEM;

	/* omit last 4 bytes because it's clock info. */
	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	c = 0;
	do 
		buf[c] = be32_to_cpu(buf[c]);
	while (++c < 4);

	/* swap stream channels because inverted */
	tmp = be32_to_cpu(buf[c]);
	buf[c] = be32_to_cpu(buf[c + 2]);
	buf[c + 2] = tmp;
	tmp = be32_to_cpu(buf[c + 1]);
	buf[c + 1] = be32_to_cpu(buf[c + 3]);
	buf[c + 3] = tmp;

	c += 4;
	do
		buf[c] = be32_to_cpu(buf[c]);
	while (++c < 12);

end:
	return err;
}

/* Ozonic specific controls */
static char *ozonic_meter_labels[] = {
	ANA_IN, ANA_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, ANA_OUT
};
static int
ozonic_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
{
	int c, channels, err;

	channels = ARRAY_SIZE(ozonic_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	for (c = 0; c < channels; c++)
		buf[c] = be32_to_cpu(buf[c]);

end:
	return err;
}

/* NRV10 specific controls */
static char *nrv10_meter_labels[] = {
	/* TODO: this is based on my assumption */
	ANA_IN, ANA_IN, ANA_IN, ANA_IN,
	DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT,
	DIG_IN
};
static int
nrv10_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
{
	int c, channels, err;

	channels = ARRAY_SIZE(nrv10_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	/* TODO: are there clock info or not? */
	err = get_meter(bebob, (void *)buf, size);
	if (err < 0)
		goto end;

	for (c = 0; c < channels; c++)
		buf[c] = be32_to_cpu(buf[c]);

end:
	return err;
}


/* BeBoB bootloader specification */
struct snd_bebob_spec maudio_bootloader_spec = {
	.load		= &firmware_load,
	.discover	= NULL,
	.clock		= NULL,
	.dig_iface	= NULL
};

/* for special customized devices */
static struct snd_bebob_freq_spec special_freq_spec = {
	.get	= &special_get_freq,
	/* Ssetting sampling rate don't wotk without streams perhaps... */
	.set	= &snd_bebob_stream_set_rate
};
static struct snd_bebob_clock_spec special_clock_spec = {
	.num	= ARRAY_SIZE(special_clock_labels),
	.labels	= special_clock_labels,
	.get	= &special_clock_get,
	.set	= &special_clock_set,
	.synced	= &special_clock_synced
};
static struct snd_bebob_dig_iface_spec special_dig_iface_spec = {
	.num	= ARRAY_SIZE(special_dig_iface_labels),
	.labels	= special_dig_iface_labels,
	.get	= &special_dig_iface_get,
	.set	= &special_dig_iface_set
};
static struct snd_bebob_meter_spec special_meter_spec = {
	.num	= ARRAY_SIZE(special_meter_labels),
	.labels	= special_meter_labels,
	.get	= &special_meter_get
};
struct snd_bebob_spec maudio_special_spec = {
	.load		= NULL,
	.discover	= &special_discover,
	.map   		= NULL,
	.freq		= &special_freq_spec,
	.clock		= &special_clock_spec,
	.dig_iface	= &special_dig_iface_spec,
	.meter		= &special_meter_spec
};

struct snd_bebob_freq_spec normal_freq_spec = {
	.get	= &snd_bebob_stream_get_rate,
	.set	= &snd_bebob_stream_set_rate
};

/* Firewire 410 specification */
static struct snd_bebob_clock_spec fw410_clock_spec = {
	.num	= ARRAY_SIZE(fw410_clock_labels),
	.labels	= fw410_clock_labels,
	.get	= &fw410_clock_get,
	.set	= &fw410_clock_set,
	.synced	= &fw410_clock_synced
};
static struct snd_bebob_dig_iface_spec fw410_dig_iface_spec = {
	.num	= ARRAY_SIZE(fw410_dig_iface_labels),
	.labels	= fw410_dig_iface_labels,
	.get	= &fw410_dig_iface_get,
	.set	= &fw410_dig_iface_set
};
static struct snd_bebob_meter_spec fw410_meter_spec = {
	.num	= ARRAY_SIZE(fw410_meter_labels),
	.labels	= fw410_meter_labels,
	.get	= &fw410_meter_get
};
struct snd_bebob_spec maudio_fw410_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &normal_freq_spec,
	.clock		= &fw410_clock_spec,
	.dig_iface	= &fw410_dig_iface_spec,
	.meter		= &fw410_meter_spec
};

/* Firewire Audiophile specification */
static struct snd_bebob_clock_spec audiophile_clock_spec = {
	.num	= ARRAY_SIZE(audiophile_clock_labels),
	.labels	= audiophile_clock_labels,
	.get	= &audiophile_clock_get,
	.set	= &audiophile_clock_set,
	.synced	= &audiophile_clock_synced
};
static struct snd_bebob_meter_spec audiophile_meter_spec = {
	.num	= ARRAY_SIZE(audiophile_meter_labels),
	.labels	= audiophile_meter_labels,
	.get	= &audiophile_meter_get
};
struct snd_bebob_spec maudio_audiophile_spec = {
	.load		= &firmware_load,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &normal_freq_spec,
	.clock		= &audiophile_clock_spec,
	.dig_iface	= NULL,
	.meter		= &audiophile_meter_spec
};

/* Firewire Solo specification */
static struct snd_bebob_clock_spec solo_clock_spec = {
	.num	= ARRAY_SIZE(solo_clock_labels),
	.labels	= solo_clock_labels,
	.get	= &solo_clock_get,
	.set	= &solo_clock_set,
	.synced	= &solo_clock_synced
};
static struct snd_bebob_meter_spec solo_meter_spec = {
	.num	= ARRAY_SIZE(solo_meter_labels),
	.labels	= solo_meter_labels,
	.get	= &solo_meter_get
};
struct snd_bebob_spec maudio_solo_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &normal_freq_spec,
	.clock		= &solo_clock_spec,
	.dig_iface	= NULL,
	.meter		= &solo_meter_spec
};

/* Ozonic specification */
static struct snd_bebob_meter_spec ozonic_meter_spec = {
	.num	= ARRAY_SIZE(ozonic_meter_labels),
	.labels	= ozonic_meter_labels,
	.get	= &ozonic_meter_get
};
struct snd_bebob_spec maudio_ozonic_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &normal_freq_spec,
	.clock		= NULL,
	.dig_iface	= NULL,
	.meter		= &ozonic_meter_spec
};

/* NRV10 specification */
static struct snd_bebob_meter_spec nrv10_meter_spec = {
	.num	= ARRAY_SIZE(nrv10_meter_labels),
	.labels	= nrv10_meter_labels,
	.get	= &nrv10_meter_get
};
struct snd_bebob_spec maudio_nrv10_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.freq		= &normal_freq_spec,
	.clock		= NULL,
	.dig_iface	= NULL,
	.meter		= &nrv10_meter_spec
};

