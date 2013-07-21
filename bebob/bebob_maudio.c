#include "./bebob.h"

/* M-Audio don't use debugger information */

#define MAUDIO_LOADER_CUE1	0x01000000
#define MAUDIO_LOADER_CUE2	0x00001101
#define MAUDIO_LOADER_CUE3	0x00000000

#define JUJU_LIMIT 11

/*
 * for some M-Audio devices, this module just send cue to load
 * firmware. After loading, the device generates bus reset and
 * newly detected.
 */
static int boot_loading(struct snd_bebob *bebob)
{
	int err;
	__be32 cues[3];

	snd_printk(KERN_INFO"loading M-Audio Device...\n");

	cues[0] = cpu_to_be32(MAUDIO_LOADER_CUE1);
	cues[1] = cpu_to_be32(MAUDIO_LOADER_CUE2);
	cues[2] = cpu_to_be32(MAUDIO_LOADER_CUE3);

	/* Returns zero on success, or a negative error code. */
	err = snd_fw_transaction(bebob->unit, TCODE_WRITE_BLOCK_REQUEST,
				 BEBOB_ADDR_REG_REQ, cues, 12);

	/* positive return value need to be handled correctly later */
	if (err == 0)
		err = 1;

	return err;
}

/*
 * audiophile uses the same model id for pre-loaded-device and
 * post-loaded-device. This module uses the model name to identify.
 */
static int audiophile_probing(struct snd_bebob *bebob)
{
	char name[24];
	int err;

	err = fw_csr_string(bebob->unit->directory, CSR_MODEL, name, sizeof(name));
	if (err < 0)
		goto end;

	/* Audiophole should be loaded */
	if (strncmp(name, "FW Audiophile Bootloader", JUJU_LIMIT) == 0) {
		err = boot_loading(bebob);
		goto end;
	}

	err = 0;

end:
	return err;
}

/* specific operations for Firewire 1814 */
static int fw1814_probing(struct snd_bebob *bebob)
{
	int err;

/*
 * TODO:
 * The number of channels in a stream is differ depending on
 * Digital Format. But I hope the function to change it into user land...

Channels
	Digital	I/O	|Analog		|MIDI I/O
	ADAT	SPDIF	|Input	Out	|
 44.1	8	2	|8	4	|1
 48.0	8	2	|8	4	|1
 88.2	4 S/MUX	2	|8	4	|1
 96.0	4 S/MUX	2	|8	4	|1
177.4	0	0	|2	4	|1
192.0	0	0	|2	4	|1
*/
	/* get the current digital format for input */
	/* then
	if (adat) {
		memcpy(bebob->tx_stream_formations[3], 16+1);
		memcpy(bebob->tx_stream_formations[4], 16+1);
		memcpy(bebob->tx_stream_formations[5], 12+1);
		memcpy(bebob->tx_stream_formations[6], 12+1);
		memcpy(bebob->tx_stream_formations[7], 4+1);
		memcpy(bebob->tx_stream_formations[8], 4+1);
	} else {
		memcpy(bebob->tx_stream_formations[3], 10+1);
		memcpy(bebob->tx_stream_formations[4], 10+1);
		memcpy(bebob->tx_stream_formations[5], 10+1);
		memcpy(bebob->tx_stream_formations[6], 10+1);
		memcpy(bebob->tx_stream_formations[7], 4+1);
		memcpy(bebob->tx_stream_formations[8], 4+1);
	}
	*/

	/* get the current digital format for output */
	/* then
	if (adat) {
		memcpy(bebob->rx_stream_formations[3], 16+1);
		memcpy(bebob->rx_stream_formations[4], 16+1);
		memcpy(bebob->rx_stream_formations[5], 12+1);
		memcpy(bebob->rx_stream_formations[6], 12+1);
		memcpy(bebob->rx_stream_formations[7], 4+1);
		memcpy(bebob->rx_stream_formations[8], 4+1);
	} else {
		memcpy(bebob->rx_stream_formations[3], 10+1);
		memcpy(bebob->rx_stream_formations[4], 10+1);
		memcpy(bebob->rx_stream_formations[5], 10+1);
		memcpy(bebob->rx_stream_formations[6], 10+1);
		memcpy(bebob->rx_stream_formations[7], 4+1);
		memcpy(bebob->rx_stream_formations[8], 4+1);
	}
	 */

	err = 0;

	return err;
}

static int fw1814_clocking(struct snd_bebob *bebob)
{
	int err;

	err = 0;

	/*
	 * TODO:
	 * fw1814 uses 4 options for this. 3 of them is generic
	 * but the rest is specific.
	 */

	return err;
}

/* specific operations for Firewire 410 */
static int fw410_streaming(struct snd_bebob *bebob)
{
	int err;

	err = 0;

	/*
	 * TODO:
	 * fw410 don't use the generic AVC commands to change sampling rate.
	 * It uses bridgeco specific way with stream formations.
	 */

	return err;
}

struct snd_bebob_ops maudio_bootloader_ops = {
	.probing	= &boot_loading,
	.streaming	= NULL,
	.clocking	= NULL
};

struct snd_bebob_ops maudio_fw1814_ops = {
	.probing	= &fw1814_probing,
	.streaming	= NULL,
	.clocking	= &fw1814_clocking
};

struct snd_bebob_ops maudio_fw410_ops = {
	.probing	= NULL,
	.streaming	= &fw410_streaming,
	.clocking	= NULL
};

struct snd_bebob_ops maudio_audiophile_ops = {
	.probing	= &audiophile_probing,
	.streaming	= NULL,
	.clocking	= NULL
};
