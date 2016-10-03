/*
 * oxfw_command.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

int avc_general_inquiry_sig_fmt(struct fw_unit *unit, unsigned int rate,
				enum avc_general_plug_dir dir,
				unsigned short pid)
{
	unsigned int sfc;
	u8 *buf;
	int err;

	for (sfc = 0; sfc < CIP_SFC_COUNT; sfc++) {
		if (amdtp_rate_table[sfc] == rate)
			break;
	}
	if (sfc == CIP_SFC_COUNT)
		return -EINVAL;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x02;		/* SPECIFIC INQUIRY */
	buf[1] = 0xff;		/* UNIT */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT. AM824 */
	buf[5] = 0x07 & sfc;	/* FDF-hi. AM824, frequency */
	buf[6] = 0xff;		/* FDF-mid. AM824, SYT hi (not used) */
	buf[7] = 0xff;		/* FDF-low. AM824, SYT lo (not used) */

	/* do transaction and check buf[1-5] are the same against command */
	err = fcp_avc_transaction(unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5));
	if ((err > 0) && (err < 8))
		err = -EIO;
	else if (buf[0] == 0x08)	/* NOT IMPLEMENTED */
		err = -ENOSYS;
	if (err < 0)
		goto end;

	err = 0;
end:
	kfree(buf);
	return err;
}
