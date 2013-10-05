#include "./bebob.h"

#define MAUDIO_LOADER_CUE1	0x01000000
#define MAUDIO_LOADER_CUE2	0x00001101
#define MAUDIO_LOADER_CUE3	0x00000000

#define ANA_IN		"Analog In"
#define ANA_OUT		"Analog Out"
#define DIG_IN		"Digital_in"
#define DIG_OUT		"Digital Out"
#define STRM_IN		"Stream In"
#define AUX_IN		"Aux In"
#define AUX_OUT		"Aux Out"
#define HP_OUT		"HP Out"
#define MIX_OUT		"Mixer Out"

/* If we make any transaction to load firmware, the operation may failed. */
static int run_a_transaction(struct fw_unit *unit, int tcode,
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
static int firmware_load(struct snd_bebob *bebob)
{
	__be32 cues[3];

	cues[0] = cpu_to_be32(MAUDIO_LOADER_CUE1);
	cues[1] = cpu_to_be32(MAUDIO_LOADER_CUE2);
	cues[2] = cpu_to_be32(MAUDIO_LOADER_CUE3);

	return run_a_transaction(bebob->unit, TCODE_WRITE_BLOCK_REQUEST,
				 BEBOB_ADDR_REG_REQ, cues, sizeof(cues));
}

static int fw1814_discover(struct snd_bebob *bebob) {
	return 0;
}
static char *fw1814_clock_labels[] = {
	"Internal with Digital Mute", "Digital",
	"Word Clock", "Internal with Digital unmute"};
static int fw1814_clock_get(struct snd_bebob *bebob)
{
	int id;
	return id;
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
/* TODO: unknown because streaming is not success yet... */
static char *fw1814_meter_labels[] = {ANA_IN};
static int fw1814_meter_get(struct snd_bebob *bebob) {
	int id;
	return id;
}

static char *fw410_clock_labels[] = {"Internal", "Optical", "Coaxial"};
static int fw410_clock_get(struct snd_bebob *bebob)
{
	int id;
	return id;
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
	MIX_OUT, MIX_OUT, MIX_OUT, MIX_OUT, MIX_OUT,
	HP_OUT
};
static int fw410_meter_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}

static char *audiophile_clock_labels[] = {"Internal", "Digital"};
static int audiophile_clock_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}
static int audiophile_clock_set(struct snd_bebob *bebob, int id)
{
	return id;
}
static char *audiophile_meter_labels[] = {
	ANA_IN, DIG_IN,
	ANA_OUT, ANA_OUT, DIG_OUT,
	AUX_IN, AUX_OUT,
};
static int audiophile_meter_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}

static char *solo_clock_labels[] = {"Internal", "Digital"};
static int solo_clock_get(struct snd_bebob *bebob)
{
	int id;
	return id;
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
static int solo_meter_get(struct snd_bebob *bebob)
{
	int id;
	return id;
}

static char *ozonic_meter_labels[] = {
	ANA_IN, ANA_IN,
	STRM_IN, STRM_IN,
	ANA_OUT, ANA_OUT
};
static int ozonic_meter_get(struct snd_bebob *bebob)
{
	int id;
	return id;
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
	.num	= sizeof(fw1814_clock_labels),
	.labels	= fw1814_clock_labels,
	.get	= &fw1814_clock_get,
	.set	= &fw1814_clock_set
};
static struct snd_bebob_dig_iface_spec fw1814_dig_iface_spec = {
	.num	= sizeof(fw1814_dig_iface_labels),
	.labels	= fw1814_dig_iface_labels,
	.get	= &fw1814_dig_iface_get,
	.set	= &fw1814_dig_iface_set
};
static struct snd_bebob_meter_spec fw1814_meter_spec = {
	.num	= sizeof(fw1814_meter_labels),
	.labels	= fw1814_meter_labels,
	.bytes	= 2,
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
	.num	= sizeof(fw410_clock_labels),
	.labels	= fw410_clock_labels,
	.get	= &fw410_clock_get,
	.set	= &fw410_clock_set
};
static struct snd_bebob_dig_iface_spec fw410_dig_iface_spec = {
	.num	= sizeof(fw1814_dig_iface_labels),
	.labels	= fw410_dig_iface_labels,
	.get	= &fw410_dig_iface_get,
	.set	= &fw410_dig_iface_set
};
static struct snd_bebob_meter_spec fw410_meter_spec = {
	.num	= sizeof(fw410_meter_labels),
	.labels	= fw410_meter_labels,
	.bytes	= 4,
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
	.num	= sizeof(audiophile_clock_labels),
	.labels	= audiophile_clock_labels,
	.get	= &audiophile_clock_get,
	.set	= &audiophile_clock_set
};
static struct snd_bebob_meter_spec audiophile_meter_spec = {
	.num	= sizeof(audiophile_meter_labels),
	.labels	= audiophile_meter_labels,
	.bytes	= 4,
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
	.num	= sizeof(solo_clock_labels),
	.labels	= solo_clock_labels,
	.get	= &solo_clock_get,
	.set	= &solo_clock_set
};
static struct snd_bebob_meter_spec solo_meter_spec = {
	.num	= sizeof(solo_meter_labels),
	.labels	= solo_meter_labels,
	.bytes	= 4,
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
	.num	= sizeof(ozonic_meter_labels),
	.labels	= ozonic_meter_labels,
	.bytes	= 4,
	.get	= &ozonic_meter_get
};
struct snd_bebob_spec maudio_ozonic = {
	.load		= NULL,
	.discover	= &snd_bebob_discover,
	.clock		= NULL,
	.dig_iface	= NULL,
	.meter		= &ozonic_meter_spec
};
