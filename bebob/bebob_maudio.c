#include "./bebob.h"

/* M-Audio don't use debugger information */

#define MAUDIO_LOADER_CUE1	0x01000000
#define MAUDIO_LOADER_CUE2	0x00001101
#define MAUDIO_LOADER_CUE3	0x00000000

/*
 * for some M-Audio devices, this module just send cue to load
 * firmware. After loading, the device generates bus reset and
 * newly detected.
 */
int snd_bebob_maudio_detect(struct fw_unit *unit)
{
	char name[24];
	__be32 cues[3];
	int err = 0;

	/* maybe Juju bug. fw_csr_string return maximum 16 characters */
	int limit = 15;

	err = fw_csr_string(unit->directory, CSR_MODEL, name, sizeof(name));
	if (err < 0)
		goto end;

	/*
	 * No need to send cue to load firmware.
	 * Here 1 means already loaded.
	 */
	if ((strncmp(name, "FW Audiophile Bootloader", limit) != 0) ||
	    (strncmp(name, "FW 410 Bootloader", limit) != 0) ||
	    (strncmp(name, "FW 1814 Bootloader", limit) != 0)) {
		err = 1;
		goto end;
	}

snd_printk(KERN_INFO"M-Audio loading\n");

	cues[0] = cpu_to_be32(MAUDIO_LOADER_CUE1);
	cues[1] = cpu_to_be32(MAUDIO_LOADER_CUE2);
	cues[2] = cpu_to_be32(MAUDIO_LOADER_CUE3);

	/* Returns zero on success, or a negative error code. */
	err = snd_fw_transaction(unit, TCODE_WRITE_BLOCK_REQUEST,
			BEBOB_ADDR_REG_REQ, cues, 12);

end:
	return err;
}

