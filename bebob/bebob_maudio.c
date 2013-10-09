#include "./bebob.h"

#define MAUDIO_BOOTLOADER_CUE1	0x01000000
#define MAUDIO_BOOTLOADER_CUE2	0x00001101
#define MAUDIO_BOOTLOADER_CUE3	0x00000000

#define MAUDIO_SPECIFIC_ADDRESS	0xffc700000000

#define METER_OFFSET		0x00600000

/* some device has sync info after metering data */
#define METER_SIZE_FW1814	84	/* with sync info */
#define METER_SIZE_PROJECTMIX	84	/* with sync info */
#define METER_SIZE_FW410	76	/* with sync info */
#define METER_SIZE_AUDIOPHILE	60	/* with sync info */
#define METER_SIZE_SOLO		52	/* with sync info */
#define METER_SIZE_OZONIC	48
#define METER_SIZE_NRV10	80

/* labels for metering */
#define ANA_IN		"Analog In"
#define ANA_OUT		"Analog Out"
#define DIG_IN		"Digital_in"
#define DIG_OUT		"Digital Out"
#define STRM_IN		"Stream In"
#define AUX_OUT		"Aux Out"
#define HP_OUT		"HP Out"
/* for ProjectMix and NRV */
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
#define	GAIN_STM_12_IN	0x00
#define	GAIN_STM_34_IN	0x04
#define GAIN_ANA_12_OUT	0x08
#define GAIN_ANA_34_OUT	0x0c
#define GAIN_ANA_12_IN	0x10
#define GAIN_ANA_34_IN	0x14
#define GAIN_ANA_56_IN	0x18
#define GAIN_ANA_78_IN	0x1c
#define GAIN_DIG_12_IN	0x20
/* Unknown or unused from 0x24 to 0x30 */
#define GAIN_AUX_12_OUT	0x34
#define GAIN_HP_12_OUT	0x38
#define GAIN_HP_34_OUT	0x3c
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
#define LR_DIG_12_IN	0x50
/* Unknown or unused from 0x54 to 0x60 */
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
#define AUX_DIG_12_IN	0x7c
/* Unknown or unused from 0x80 to 0x8c */
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
firmware_load(struct snd_bebob *bebob)
{
	__be32 cues[3];

	cues[0] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE1);
	cues[1] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE2);
	cues[2] = cpu_to_be32(MAUDIO_BOOTLOADER_CUE3);

	return run_a_transaction(bebob->unit, TCODE_WRITE_BLOCK_REQUEST,
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
check_sync(struct snd_bebob *bebob, int size, int *sync)
{
	int err;
	u8 *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, buf, size);
	if (err < 0)
		goto end;

	*sync = (buf[size - 3] != 0xff);
	err = 0;
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
set_clock_params(struct snd_bebob *bebob, int clk_src,
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
 * dig_iface: 0x00:Optical, 0x01:Coaxial
 */
static int
set_dig_iface(struct snd_bebob *bebob, int in_dig_iface, int out_dig_iface)
{
	int err;
	u8 *buf;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0]  = 0x00;			/* CONTROL */
	buf[1]  = 0xff;			/* UNIT */
	buf[2]  = 0x00;			/* vendor dependent */
	buf[3]  = 0x04;
	buf[4]  = 0x04;
	buf[5]  = 0x10;
	buf[6]  = 0x02;			/* has 2 parameters */
	buf[7]  = 0xff & in_dig_iface;	/* input digital interface */
	buf[8]  = 0xff & out_dig_iface;	/* output digital interface */
	buf[9]  = 0x00;			/* padding */
	buf[10] = 0x00;			/* padding */
	buf[11] = 0x00;			/* padding */

	err = fcp_avc_transaction(bebob->unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	if ((err < 6) || (buf[0] != 0x09)) {
		dev_err(&bebob->unit->device,
			"failed to set digital interface\n");
		err = -EIO;
		goto end;
	}

	bebob->in_dig_iface = buf[7];
	bebob->out_dig_iface = buf[8];
	err = 0;

end:
	kfree(buf);
	return err;
}

/*
 * I guess this is SIGNAL SOURCE command in 'Connection and Compatibility
 * Management 1.0 (1394TA 199032)'.
 */
static int
get_signal_source(struct fw_unit *u, int *unit, int *plugid)
{
	int err;
	u8 *buf;

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;	/* STATUS */
	buf[1] = 0xff;	/* UNIT */
	buf[2] = 0x1A;	/* SIGNAL SOURCE? */
	buf[3] = 0x0f;
	buf[4] = 0xff;
	buf[5] = 0xfe;
	buf[6] = 0x60;
	buf[7] = 0x01;

	err = fcp_avc_transaction(u, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 0) || (buf[0] != 0x0c)) {
		dev_err(&u->device,
			"failed to get signal status\n");
		err = -EIO;
		goto end;
	}

	*unit = buf[4];
	*plugid = buf[5];
end:
	kfree(buf);
	return err;
}
static int
set_signal_source(struct fw_unit *u, int unit, int plugid)
{
	int err;
	u8 *buf;

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x00;	/* CONTROL */
	buf[1] = 0xff;	/* UNIT */
	buf[2] = 0x1A;	/* SIGNAL SOURCE? */
	buf[3] = 0x0f;
	buf[4] = 0xff & unit;
	buf[5] = 0xff & plugid;
	buf[6] = 0x60;
	buf[7] = 0x01;

	err = fcp_avc_transaction(u, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 0) || ((buf[0] != 0x09) && (buf[0] != 0x0f))) {
		dev_err(&u->device,
			"failed to set signal status\n");
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

/* for special customized devices */
static char *special_clock_labels[] = {
	"Interna1l with Digital Mute", "Digital",
	"Word Clock", "Internal"};
static int special_clock_get(struct snd_bebob *bebob, int *id)
{
	*id = bebob->clk_src;
	return 0;
}
static int special_clock_set(struct snd_bebob *bebob, int id)
{
	return set_clock_params(bebob, id,
				bebob->in_dig_fmt, bebob->out_dig_fmt,
				bebob->clk_lock);
}
static char *special_dig_iface_labels[] = {
	"ADAT Optical", "S/PDIF Optical", "S/PDIF Coaxial"
};
static int special_dig_iface_get(struct snd_bebob *bebob, int *id)
{
	/* for simplicity, the same value for input/output */
	*id = (0x01 & bebob->in_dig_fmt) | ((bebob->in_dig_iface & 0x01) << 1);

	/* normalizing */
	if (*id > 0)
		(*id)--;
	return 0;
}
static int special_dig_iface_set(struct snd_bebob *bebob, int id)
{
	int err;
	int dig_fmt;
	int dig_iface;

	/* normalizing */
	if (id > 0)
		id++;

	dig_fmt = id & 0x01;
	dig_iface = (id >> 1) & 0x01;

	/* for simplicity, the same value for input/output */
	err = set_clock_params(bebob, bebob->clk_src, dig_fmt, dig_fmt,
			       bebob->clk_lock);
	if (err < 0)
		goto end;

	err = set_dig_iface(bebob, dig_iface, dig_iface);
end:
	return err;
}

/* Firewire 1814 specific controls */
static int fw1814_discover(struct snd_bebob *bebob)
{
	int err;

	/* initialize these parameters because doesn't allow driver to ask */
	err = set_clock_params(bebob, 0x03, 0x00, 0x00, 0x00);
	if (err < 0)
		dev_err(&bebob->unit->device,
			"Failed to initialize clock params\n");

	err = set_dig_iface(bebob, 0x00, 0x00);
	if (err < 0)
		dev_err(&bebob->unit->device,
			"Failed to initialize digital interface\n");

	return 0;
}
static char *fw1814_meter_labels[] = {
	ANA_IN, ANA_IN, ANA_IN, ANA_IN,
	DIG_IN,
	ANA_OUT, ANA_OUT,
	HP_OUT, HP_OUT,
	AUX_OUT
};
static int fw1814_meter_get(struct snd_bebob *bebob, u32 *target, int size)
{
	u16 *buf;
	int i, c, err;

	if (size < ARRAY_SIZE(fw1814_meter_labels) * 2 * sizeof(u32))
		return -EINVAL;

	/* omit last 4 bytes because it's clock info. */
	buf = kmalloc(METER_SIZE_FW1814, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, (void *)buf, METER_SIZE_FW1814);
	if (err < 0)
		goto end;

	/* some channels are not used and convert u16 to u32 */
	c = 0;
	for (i  = 2; i < 12; i++)
		target[c++] = be16_to_cpu(buf[i]) << 8;
	for (i = 20; i < 24; i++)
		target[c++] = be16_to_cpu(buf[i]) << 8;
	for (i = 34; i < 40; i++)
		target[c++] = be16_to_cpu(buf[i]) << 8;

end:
	kfree(buf);
	return err;
}

/* ProjectMix I/O specific controls */
static int projectmix_discover(struct snd_bebob *bebob) {
	return 0;
}
static char *projectmix_meter_labels[] = {
	UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER,
	UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER,
	UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER,
	UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER,
	UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER, UNKNOWN_METER
};
static int projectmix_meter_get(struct snd_bebob *bebob, u32 *target, int size)
{
	u16 *buf;
	int c, channels, err;

	channels = ARRAY_SIZE(projectmix_meter_labels) * 2;
	if (size < channels * sizeof(u32))
		return -EINVAL;

	/* omit last 4 bytes because it's clock info. */
	buf = kmalloc(METER_SIZE_PROJECTMIX, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, (void *)buf, METER_SIZE_PROJECTMIX);
	if (err < 0)
		goto end;

	/* some channels are not used and convert u16 to u32 */
	/* TODO: breakdown */
	for (c = 0; c < channels; c++)
		target[c] = be32_to_cpu(buf[c]) << 8;

end:
	kfree(buf);
	return err;
}

/* Firewire 410 specific controls */
static char *fw410_clock_labels[] = {
	"Internal", "Digital Optical", "Digital Coaxial"
};
static int fw410_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, unit, plugid;

	err = get_signal_source(bebob->unit, &unit, &plugid);
	if (err < 0)
		goto end;

	if ((unit == 0xff) && (plugid == 0x82))
		*id = 2;
	else if ((unit == 0xff) && (plugid == 0x83))
		*id = 1;
	else
		*id = 0;
end:
	return err;
}
static int fw410_clock_set(struct snd_bebob *bebob, int id)
{
	int unit, plugid;

	if (id == 0) {
		unit = 0x60;
		plugid = 0x01;
	} else if (id == 1) {
		unit = 0xff;
		plugid = 0x83;
	} else {
		unit = 0xff;
		plugid = 0x82;
	}

	return set_signal_source(bebob->unit, unit, plugid);
}
static char *fw410_dig_iface_labels[] = {
	"S/PDIF Optical", "S/PDIF Coaxial"
};
static int fw410_dig_iface_get(struct snd_bebob *bebob, int *id)
{
	return avc_audio_get_selector(bebob->unit, 0, 1, id);
}
static int fw410_dig_iface_set(struct snd_bebob *bebob, int id)
{
	return avc_audio_set_selector(bebob->unit, 0, 1, id);
}
static char *fw410_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT
};
static int fw410_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
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
static int audiophile_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, unit, plugid;

	err = get_signal_source(bebob->unit, &unit, &plugid);
	if (err < 0)
		goto end;

	if ((unit == 0xff) && (plugid == 0x82))
		*id = 1;
	else
		*id = 0;
end:
	return err;
	return *id;
}
static int audiophile_clock_set(struct snd_bebob *bebob, int id)
{
	int unit, plugid;

	if (id == 0) {
		unit = 0x60;
		plugid = 0x01;
	} else {
		unit = 0xff;
		plugid = 0x82;
	}

	return set_signal_source(bebob->unit, unit, plugid);
}
static char *audiophile_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, DIG_OUT,
	HP_OUT, AUX_OUT,
};
static int audiophile_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
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
static int solo_clock_get(struct snd_bebob *bebob, int *id)
{
	int err, unit, plugid;

	err = get_signal_source(bebob->unit, &unit, &plugid);
	if (err < 0)
		goto end;

	if ((unit == 0xff) && (plugid == 0x81))
		*id = 1;
	else
		*id = 0;
end:
	return err;
}
static int solo_clock_set(struct snd_bebob *bebob, int id)
{
	int unit, plugid;

	if (id == 0) {
		unit = 0x60;
		plugid = 0x01;
	} else {
		unit = 0xff;
		plugid = 0x81;
	}

	return set_signal_source(bebob->unit, unit, plugid);
}
static char *solo_meter_labels[] = {
	ANA_IN, DIG_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, DIG_OUT
};
static int solo_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
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
static int ozonic_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
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
static int nrv10_meter_get(struct snd_bebob *bebob, u32 *buf, int size)
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
static struct snd_bebob_clock_spec special_clock_spec = {
	.num	= ARRAY_SIZE(special_clock_labels),
	.labels	= special_clock_labels,
	.get	= &special_clock_get,
	.set	= &special_clock_set
};
static struct snd_bebob_dig_iface_spec special_dig_iface_spec = {
	.num	= ARRAY_SIZE(special_dig_iface_labels),
	.labels	= special_dig_iface_labels,
	.get	= &special_dig_iface_get,
	.set	= &special_dig_iface_set
};

/* Firewire 1814 specification */
static struct snd_bebob_meter_spec fw1814_meter_spec = {
	.num	= ARRAY_SIZE(fw1814_meter_labels),
	.labels	= fw1814_meter_labels,
	.get	= &fw1814_meter_get
};
struct snd_bebob_spec maudio_fw1814_spec = {
	.load		= NULL,
	.discover	= &fw1814_discover,
	.clock		= &special_clock_spec,
	.dig_iface	= &special_dig_iface_spec,
	.meter		= &fw1814_meter_spec
};

/* ProjectMix specification */
static struct snd_bebob_meter_spec projectmix_meter_spec = {
	.num	= ARRAY_SIZE(projectmix_meter_labels),
	.labels	= projectmix_meter_labels,
	.get	= &projectmix_meter_get
};
struct snd_bebob_spec maudio_projectmix_spec = {
	.load		= NULL,
	.discover	= &projectmix_discover,
	.clock		= &special_clock_spec,
	.dig_iface	= &special_dig_iface_spec,
	.meter		= &projectmix_meter_spec
};

/* Firewire 410 specification */
static struct snd_bebob_clock_spec fw410_clock_spec = {
	.num	= ARRAY_SIZE(fw410_clock_labels),
	.labels	= fw410_clock_labels,
	.get	= &fw410_clock_get,
	.set	= &fw410_clock_set
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
	.discover	= &snd_bebob_discover,
	.clock		= &fw410_clock_spec,
	.dig_iface	= &fw410_dig_iface_spec,
	.meter		= &fw410_meter_spec
};

/* Firewire Audiophile specification */
static struct snd_bebob_clock_spec audiophile_clock_spec = {
	.num	= ARRAY_SIZE(audiophile_clock_labels),
	.labels	= audiophile_clock_labels,
	.get	= &audiophile_clock_get,
	.set	= &audiophile_clock_set
};
static struct snd_bebob_meter_spec audiophile_meter_spec = {
	.num	= ARRAY_SIZE(audiophile_meter_labels),
	.labels	= audiophile_meter_labels,
	.get	= &audiophile_meter_get
};
struct snd_bebob_spec maudio_audiophile_spec = {
	.load		= &firmware_load,
	.discover	= &snd_bebob_discover,
	.clock		= &audiophile_clock_spec,
	.dig_iface	= NULL,
	.meter		= &audiophile_meter_spec
};

/* Firewire Solo specification */
static struct snd_bebob_clock_spec solo_clock_spec = {
	.num	= ARRAY_SIZE(solo_clock_labels),
	.labels	= solo_clock_labels,
	.get	= &solo_clock_get,
	.set	= &solo_clock_set
};
static struct snd_bebob_meter_spec solo_meter_spec = {
	.num	= ARRAY_SIZE(solo_meter_labels),
	.labels	= solo_meter_labels,
	.get	= &solo_meter_get
};
struct snd_bebob_spec maudio_solo_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_discover,
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
	.discover	= &snd_bebob_discover,
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
	.discover	= &snd_bebob_discover,
	.clock		= NULL,
	.dig_iface	= NULL,
	.meter		= &nrv10_meter_spec
};

