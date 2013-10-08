#include "./bebob.h"

#define MAUDIO_BOOTLOADER_CUE1	0x01000000
#define MAUDIO_BOOTLOADER_CUE2	0x00001101
#define MAUDIO_BOOTLOADER_CUE3	0x00000000

#define MAUDIO_SPECIFIC_ADDRESS	0xffc700000000

/* labels for metering */
#define METER_OFFSET	0x00600000
#define ANA_IN		"Analog In"
#define ANA_OUT		"Analog Out"
#define DIG_IN		"Digital_in"
#define DIG_OUT		"Digital Out"
#define STRM_IN		"Stream In"
#define AUX_OUT		"Aux Out"
#define HP_OUT		"HP Out"

/*
 * FW1814 don't use AVC for control. The driver cannot refer to current
 * parameters by asynchronous transaction. The driver is allowed to write
 * transaction so MUST remember the current values.
 */
#define	FW1814_CONTROL_OFFSET	0x00700000
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
 * FW1814 cannot give a way to read its control. So drivers should remember
 * the value for each parameters. Windows driver, after loading firmware, write
 * all parameters just after flushing.
 */
static int
flush_fw1814(struct snd_bebob *bebob)
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

static int fw1814_discover(struct snd_bebob *bebob) {
	return 0;
}
static char *fw1814_clock_labels[] = {
	"Internal with Digital Mute", "Digital",
	"Word Clock", "Internal with Digital unmute"};
static int fw1814_clock_get(struct snd_bebob *bebob, int *id)
{
	return *id;
}
static int fw1814_clock_set(struct snd_bebob *bebob, int id)
{
	return id;
}
static char *fw1814_dig_iface_labels[] = {
	"Optical", "Coaxial"
};
static int fw1814_dig_iface_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}
static int fw1814_dig_iface_set(struct snd_bebob *bebob, int id)
{
	return id;
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
	buf = kmalloc(80, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = get_meter(bebob, (void *)buf, 80);
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

static char *fw410_clock_labels[] = {"Internal", "Optical", "Coaxial"};
static int fw410_clock_get(struct snd_bebob *bebob, int *id)
{
	return *id;
}
static int fw410_clock_set(struct snd_bebob *bebob, int id)
{
	return id;
}
static char *fw410_dig_iface_labels[] = {
	"Optical", "Coaxial"
};
static int fw410_dig_iface_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}
static int fw410_dig_iface_set(struct snd_bebob *bebob, int id)
{
	return id;
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

static char *audiophile_clock_labels[] = {"Internal", "Digital"};
static int audiophile_clock_get(struct snd_bebob *bebob, int *id)
{
	return *id;
}
static int audiophile_clock_set(struct snd_bebob *bebob, int id)
{
	return id;
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

static char *solo_clock_labels[] = {"Internal", "Digital"};
static int solo_clock_get(struct snd_bebob *bebob, int *id)
{
	return *id;
}
static int solo_clock_set(struct snd_bebob *bebob, int id)
{
	return id;
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

/* BeBoB bootloader specification */
struct snd_bebob_spec maudio_bootloader_spec = {
	.load		= &firmware_load,
	.discover	= NULL,
	.clock		= NULL,
	.dig_iface	= NULL
};

/* Firewire 1814 specification */
static struct snd_bebob_clock_spec fw1814_clock_spec = {
	.num	= ARRAY_SIZE(fw1814_clock_labels),
	.labels	= fw1814_clock_labels,
	.get	= &fw1814_clock_get,
	.set	= &fw1814_clock_set
};
static struct snd_bebob_dig_iface_spec fw1814_dig_iface_spec = {
	.num	= ARRAY_SIZE(fw1814_dig_iface_labels),
	.labels	= fw1814_dig_iface_labels,
	.get	= &fw1814_dig_iface_get,
	.set	= &fw1814_dig_iface_set
};
static struct snd_bebob_meter_spec fw1814_meter_spec = {
	.num	= ARRAY_SIZE(fw1814_meter_labels),
	.labels	= fw1814_meter_labels,
	.get	= &fw1814_meter_get
};
struct snd_bebob_spec maudio_fw1814_spec = {
	.load		= NULL,
	.discover	= &fw1814_discover,
	.clock		= &fw1814_clock_spec,
	.dig_iface	= &fw1814_dig_iface_spec,
	.meter		= &fw1814_meter_spec
};

/* Firewire 410 specification */
static struct snd_bebob_clock_spec fw410_clock_spec = {
	.num	= ARRAY_SIZE(fw410_clock_labels),
	.labels	= fw410_clock_labels,
	.get	= &fw410_clock_get,
	.set	= &fw410_clock_set
};
static struct snd_bebob_dig_iface_spec fw410_dig_iface_spec = {
	.num	= ARRAY_SIZE(fw1814_dig_iface_labels),
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
