/*
 * oxfw_command.c - driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./oxfw.h"

int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len,
			  unsigned int eid)
{
	unsigned int subfunc;
	int err;

	/* check given buffer */
	if ((buf == NULL) || (*len < 12)) {
		err = -EINVAL;
		goto end;
	}

	if (eid == 0xff)
		subfunc = 0xc0;	/* SINGLE */
	else
		subfunc = 0xc1;	/* LIST */

	buf[0] = 0x01;		/* STATUS */
	buf[1] = 0xff;		/* UNIT */
	buf[2] = 0xbf;		/* EXTENDED STREAM FORMAT INFORMATION */
	buf[3] = subfunc;	/* SINGLE or LIST */
	buf[4] = dir;		/* Plug Direction */
	buf[5] = 0x00;		/* Unit */
	buf[6] = 0x00;		/* PCR (Isochronous Plug) */
	buf[7] = 0xff & pid;	/* Plug ID */
	buf[8] = 0xff;		/* Padding */
	buf[9] = 0xff;		/* support status in response */
	buf[10] = 0xff & eid;	/* entry ID */
	buf[11] = 0xff;		/* padding */

	/* do transaction and check buf[1-7] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, *len,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7));
	if (err < 0) {
		goto end;
	/* reach the end of entries */
	} else if (buf[0] == 0x0a) {
		err = 0;
		*len = 0;
		goto end;
	} else if (buf[0] != 0x0c) {
		err = -EINVAL;
		goto end;
	/* the content starts at 11th bytes */
	} else if (err < 9) {
		err = -EIO;
		goto end;
	} else if ((subfunc == 0xc1) && (buf[10] != eid)) {
		err = -EIO;
		goto end;
	}

	/* strip */
	memmove(buf, buf + 10, err - 10);
	*len = err - 10;
	err = 0;
end:
	return err;
}

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
	buf[6] = 0xff;		/* FDF-mid. AM824, SYT hi (not used)*/
	buf[7] = 0xff;		/* FDF-low. AM824, SYT lo (not used) */

	/* do transaction and check buf[1-5] are the same against command */
	err = fcp_avc_transaction(unit, buf, 8, buf, 8,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5));
	if (err < 0)
		goto end;

	/* check length */
	if (err != 8) {
		dev_err(&unit->device, "failed to inquiry sample rate\n");
		err = -EIO;
		goto end;
	}

	/* return response code */
	err = buf[0];
end:
	kfree(buf);
	return err;
}

int snd_oxfw_get_rate(struct snd_oxfw *oxfw, unsigned int *rate,
		      enum avc_general_plug_dir dir)
{
	int err;

	err = avc_general_get_sig_fmt(oxfw->unit, rate, dir, 0);
	if (err < 0)
		goto end;

	/* IMPLEMENTED/STABLE is OK */
	if (err != 0x0c) {
		dev_err(&oxfw->unit->device,
			"failed to get sampling rate\n");
		err = -EIO;
	}
end:
	return err;
}

int snd_oxfw_set_rate(struct snd_oxfw *oxfw, unsigned int rate,
		       enum avc_general_plug_dir dir)
{
	int err;

	err = avc_general_set_sig_fmt(oxfw->unit, rate, dir, 0);
	if (err < 0)
		goto end;

	/* ACCEPTED or INTERIM is OK */
	if ((err != 0x0f) && (err != 0x09)) {
		dev_err(&oxfw->unit->device,
			"failed to set sampling rate\n");
		err = -EIO;
	}
end:
	return err;
}
