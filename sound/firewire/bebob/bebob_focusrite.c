/*
 * bebob_focusrite.c - a part of driver for BeBoB based devices
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

#define ANA_IN	"Analog In"
#define DIG_IN	"Digital In"
#define ANA_OUT	"Analog Out"
#define DIG_OUT	"Digital Out"
#define STM_IN	"Stream In"

#define SAFFIRE_ADDRESS_BASE			0x000100000000

#define SAFFIRE_OFFSET_CLOCK_SOURCE		0x0000000000f8
#define SAFFIREPRO_OFFSET_CLOCK_SOURCE		0x000000000174

/* whether sync to external device or not */
#define SAFFIRE_OFFSET_CLOCK_SYNC_EXT		0x00000000013c
#define SAFFIRE_LE_OFFSET_CLOCK_SYNC_EXT	0x000000000432
#define SAFFIREPRO_OFFSET_CLOCK_SYNC_EXT	0x000000000164

#define SAFFIRE_CLOCK_SOURCE_INTERNAL		0
#define SAFFIRE_CLOCK_SOURCE_SPDIF		1

/* '1' is absent, why... */
#define SAFFIREPRO_CLOCK_SOURCE_INTERNAL	0
#define SAFFIREPRO_CLOCK_SOURCE_SPDIF		2
#define SAFFIREPRO_CLOCK_SOURCE_ADAT1		3
#define SAFFIREPRO_CLOCK_SOURCE_ADAT2		4
#define SAFFIREPRO_CLOCK_SOURCE_WORDCLOCK	5

/* S/PDIF, ADAT1, ADAT2 is enabled or not. three quadlets */
#define SAFFIREPRO_ENABLE_DIG_IFACES		0x0000000001a4

/* saffirepro has its own parameter for sampling frequency */
#define SAFFIREPRO_RATE_NOREBOOT		0x0000000001cc
/* index is the value for this register */
const static unsigned int rates[] = {
	[0] = 0,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 176400,
	[6] = 192000
};

/* saffire(no label)/saffire LE has metering */
#define SAFFIRE_OFFSET_METER			0x000000000100
#define SAFFIRE_LE_OFFSET_METER			0x000000000168

static inline int
saffire_read_block(struct snd_bebob *bebob, u64 offset,
		   u32 *buf, unsigned int size)
{
	unsigned int i;
	int err;
	__be32 *tmp = (__be32 *)buf;

	err =  snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
				  SAFFIRE_ADDRESS_BASE + offset,
				  tmp, size, 0);
	if (err < 0)
		goto end;

	for (i = 0; i < size / sizeof(u32); i++)
		buf[i] = be32_to_cpu(tmp[i]);
end:
	return err;
}

static inline int
saffire_read_quad(struct snd_bebob *bebob, u64 offset, u32 *value)
{
	int err;
	__be32 tmp;

	err = snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
				 SAFFIRE_ADDRESS_BASE + offset,
				 &tmp, sizeof(u32), 0);
	if (err < 0)
		goto end;

	*value = be32_to_cpu(tmp);
end:
	return err;
}

static inline int
saffire_write_quad(struct snd_bebob *bebob, u64 offset, u32 value)
{
	value = cpu_to_be32(value);

	return snd_fw_transaction(bebob->unit, TCODE_WRITE_QUADLET_REQUEST,
				  SAFFIRE_ADDRESS_BASE + offset,
				  &value, sizeof(u32), 0);
}

static char *saffirepro_26_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF", "ADAT1", "ADAT2", "Word Clock"
};

static char *saffirepro_10_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF", "Word Clock"
};
static int
saffirepro_both_clk_freq_get(struct snd_bebob *bebob, unsigned int *rate)
{
	u32 id;
	int err;

	err = saffire_read_quad(bebob, SAFFIREPRO_RATE_NOREBOOT, &id);
	if (err < 0)
		goto end;
	if (id >= ARRAY_SIZE(rates)) {
		err = -EIO;
		goto end;
	}

	*rate = rates[id];
end:
	return err;
}
static int
saffirepro_both_clk_freq_set(struct snd_bebob *bebob, unsigned int rate)
{
	u32 id;
	bool flag;

	flag = false;
	for (id = 0; id < ARRAY_SIZE(rates); id++) {
		if (rates[id] == rate)
			flag = true;
	}
	if (!flag)
		return -EINVAL;

	return saffire_write_quad(bebob, SAFFIREPRO_RATE_NOREBOOT, id);
}
static int
saffirepro_both_clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;
	u32 value;

	err = saffire_read_quad(bebob, SAFFIREPRO_OFFSET_CLOCK_SOURCE, &value);
	if (err < 0)
		goto end;

	if (bebob->spec->clock->labels == saffirepro_10_clk_src_labels) {
		if (value == SAFFIREPRO_CLOCK_SOURCE_WORDCLOCK)
			*id = 2;
		else if (value == SAFFIREPRO_CLOCK_SOURCE_SPDIF)
			*id = 1;
	} else if (value > 1)
		*id = value - 1;
end:
	return err;
}
static int
saffirepro_both_clk_src_set(struct snd_bebob *bebob, unsigned int id)
{
	int err;
	u32 values[3];

	if (bebob->spec->clock->labels == saffirepro_10_clk_src_labels) {
		if (id == 2)
			id = 5;
		else if (id == 1)
			id = 2;
	} else if (id > 0)
		id++;

	/* if requesting digital input, check whether it's enabled or not */
	/* TODO: consider to add switch for these digital inputs */
	if ((id > 1) && (id < 5)) {
		err = saffire_read_block(bebob, SAFFIREPRO_ENABLE_DIG_IFACES,
					 values, sizeof(values));
		if (err < 0)
			goto end;
		if (values[id - 2] == 0)
			goto end;
	}

	err = saffire_write_quad(bebob, SAFFIREPRO_OFFSET_CLOCK_SOURCE, id);
end:
	return err;
}
static int
saffirepro_both_clk_synced(struct snd_bebob *bebob, bool *synced)
{
	unsigned int clock;
	u32 value;
	int err;

	err = saffirepro_both_clk_src_get(bebob, &clock);
	if (err < 0)
		goto end;
	/* internal */
	if (clock == 0) {
		*synced = true;
		goto end;
	}

	err = saffire_read_quad(bebob,
				SAFFIREPRO_OFFSET_CLOCK_SYNC_EXT, &value);
	if (err < 0)
		goto end;

	*synced = (value & 0x01);
end:
	return err;
}

struct snd_bebob_spec saffire_le_spec;
static char *saffire_both_clk_src_labels[] = {
	SND_BEBOB_CLOCK_INTERNAL, "S/PDIF"
};
static int
saffire_both_clk_src_get(struct snd_bebob *bebob, unsigned int *id)
{
	int err;
	u32 value;

	err = saffire_read_quad(bebob, SAFFIRE_OFFSET_CLOCK_SOURCE, &value);
	if (err < 0)
		goto end;

	*id = 0xff & value;
end:
	return err;
};
static int
saffire_both_clk_src_set(struct snd_bebob *bebob, unsigned int id)
{
	return saffire_write_quad(bebob, SAFFIRE_OFFSET_CLOCK_SOURCE, id);
};
static int
saffire_both_clk_synced(struct snd_bebob *bebob, bool *synced)
{
	int err;
	u64 offset;
	u32 value;

	err = saffire_both_clk_src_get(bebob, &value);
	if (err < 0)
		goto end;
	/* internal */
	if (value == 0) {
		*synced = true;
		goto end;
	}

	if (bebob->spec == &saffire_le_spec)
		offset = SAFFIRE_LE_OFFSET_CLOCK_SYNC_EXT;
	else
		offset = SAFFIRE_OFFSET_CLOCK_SYNC_EXT;

	err = saffire_read_quad(bebob, offset, &value);
	if (err < 0)
		goto end;

	*synced = (0x01 & value);
end:
	return err;
}
static char *saffire_le_meter_labels[] = {
	ANA_IN, ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, ANA_OUT, ANA_OUT,
	STM_IN, STM_IN
};
#define SWAP(a, b) \
	tmp = a; \
	a = b; \
	b = tmp
static int
saffire_le_meter_get(struct snd_bebob *bebob, u32 *buf, unsigned int size)
{
	int err;
	u32 tmp;

	if (size < sizeof(saffire_le_meter_labels) * sizeof(u32))
		return -EIO;

	err = saffire_read_block(bebob, SAFFIRE_LE_OFFSET_METER, buf, size);
	if (err < 0)
		goto end;

	SWAP(buf[1], buf[3]);
	SWAP(buf[2], buf[3]);
	SWAP(buf[3], buf[4]);

	SWAP(buf[7], buf[10]);
	SWAP(buf[8], buf[10]);
	SWAP(buf[9], buf[11]);
	SWAP(buf[11], buf[12]);

	SWAP(buf[15], buf[16]);

end:
	return err;
}
static char *saffire_meter_labels[] = {
	ANA_IN, ANA_IN,
	STM_IN, STM_IN, STM_IN, STM_IN, STM_IN,
};
static int
saffire_meter_get(struct snd_bebob *bebob, u32 *buf, unsigned int size)
{
	return saffire_read_block(bebob, SAFFIRE_OFFSET_METER, buf, size);
}

/* Saffire Pro 26 I/O  */
static struct snd_bebob_clock_spec saffirepro_26_clk_spec = {
	.num		= ARRAY_SIZE(saffirepro_26_clk_src_labels),
	.labels		= saffirepro_26_clk_src_labels,
	.get_src	= &saffirepro_both_clk_src_get,
	.set_src	= &saffirepro_both_clk_src_set,
	.get_freq	= &saffirepro_both_clk_freq_get,
	.set_freq	= &saffirepro_both_clk_freq_set,
	.synced		= &saffirepro_both_clk_synced
};
struct snd_bebob_spec saffirepro_26_spec = {
	.load	= NULL,
	.clock	= &saffirepro_26_clk_spec,
	.meter	= NULL
};

/* Saffire Pro 10 I/O */
static struct snd_bebob_clock_spec saffirepro_10_clk_spec = {
	.num		= ARRAY_SIZE(saffirepro_10_clk_src_labels),
	.labels		= saffirepro_10_clk_src_labels,
	.get_src	= &saffirepro_both_clk_src_get,
	.set_src	= &saffirepro_both_clk_src_set,
	.get_freq	= &saffirepro_both_clk_freq_get,
	.set_freq	= &saffirepro_both_clk_freq_set,
	.synced		= &saffirepro_both_clk_synced
};
struct snd_bebob_spec saffirepro_10_spec = {
	.load	= NULL,
	.clock	= &saffirepro_10_clk_spec,
	.meter	= NULL
};

struct snd_bebob_clock_spec saffire_both_clk_spec= {
	.num		= ARRAY_SIZE(saffire_both_clk_src_labels),
	.labels		= saffire_both_clk_src_labels,
	.get_src	= &saffire_both_clk_src_get,
	.set_src	= &saffire_both_clk_src_set,
	.get_freq	= &snd_bebob_stream_get_rate,
	.set_freq	= &snd_bebob_stream_set_rate,
	.synced		= &saffire_both_clk_synced
};

/* Saffire LE */
struct snd_bebob_meter_spec saffire_le_meter_spec = {
	.num	= ARRAY_SIZE(saffire_le_meter_labels),
	.labels	= saffire_le_meter_labels,
	.get	= &saffire_le_meter_get,
};
struct snd_bebob_spec saffire_le_spec = {
	.load	= NULL,
	.clock	= &saffire_both_clk_spec,
	.meter	= &saffire_le_meter_spec
};
/* Saffire */
struct snd_bebob_meter_spec saffire_meter_spec = {
	.num	= ARRAY_SIZE(saffire_meter_labels),
	.labels	= saffire_meter_labels,
	.get	= &saffire_meter_get,
};
struct snd_bebob_spec saffire_spec ={
	.load	= NULL,
	.clock	= &saffire_both_clk_spec,
	.meter	= &saffire_meter_spec
};
