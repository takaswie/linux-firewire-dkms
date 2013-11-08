/*
 * bebob_command.c - driver for BeBoB based devices
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

#define BEBOB_COMMAND_MAX_TRIAL	3
#define BEBOB_COMMAND_WAIT_MSEC	100

static const int amdtp_sfc_table[] = {
	[CIP_SFC_32000]	 = 32000,
	[CIP_SFC_44100]	 = 44100,
	[CIP_SFC_48000]	 = 48000,
	[CIP_SFC_88200]	 = 88200,
	[CIP_SFC_96000]	 = 96000,
	[CIP_SFC_176400] = 176400,
	[CIP_SFC_192000] = 192000};

int avc_audio_set_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int num)
{
	u8 *buf;
	int err;

	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0]  = 0x00;		/* AV/C CONTROL */
	buf[1]  = 0x08 | (0x07 & subunit_id);	/* AUDIO SUBUNIT ID */
	buf[2]  = 0xb8;		/* FUNCTION BLOCK  */
	buf[3]  = 0x80;		/* type is 'selector'*/
	buf[4]  = 0xff & fb_id;	/* function block id */
	buf[5]  = 0x10;		/* control attribute is CURRENT */
	buf[6]  = 0x02;		/* selector length is 2 */
	buf[7]  = 0xff & num;	/* input function block plug number */
	buf[8]  = 0x01;		/* control selector is SELECTOR_CONTROL */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	if ((err < 6) || (buf[0] != 0x09)) {
		dev_err(&unit->device,
			"failed to set selector %d: 0x%02X\n",
			fb_id, buf[0]);
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_audio_get_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int *num)
{
	u8 *buf;
	int err;

	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0]  = 0x01;		/* AV/C STATUS */
	buf[1]  = 0x08 | (0x07 & subunit_id);	/* AUDIO SUBUNIT ID */
	buf[2]  = 0xb8;		/* FUNCTION BLOCK */
	buf[3]  = 0x80;		/* type is 'selector'*/
	buf[4]  = 0xff & fb_id;	/* function block id */
	buf[5]  = 0x10;		/* control attribute is CURRENT */
	buf[6]  = 0x02;		/* selector length is 2 */
	buf[7]  = 0x00;		/* input function block plug number */
	buf[8]  = 0x01;		/* control selector is SELECTOR_CONTROL */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	if ((err < 6) || (buf[0] != 0x0c)) {
		dev_err(&unit->device,
			"failed to get selector %d: 0x%02X\n",
			fb_id, buf[0]);
		err = -EIO;
		goto end;
	}

	*num = buf[7];
	err = 0;
end:
	kfree(buf);
	return err;
}


int avc_general_set_sig_fmt(struct fw_unit *unit, int rate,
			    enum avc_general_plug_dir dir,
			    unsigned short pid)
{
	int sfc, err;
	u8 *buf;
	bool flag;

	flag = false;
	for (sfc = 0; sfc < ARRAY_SIZE(amdtp_sfc_table); sfc += 1) {
		if (amdtp_sfc_table[sfc] == rate) {
			flag = true;
			break;
		}
	}
	if (!flag)
		return -EINVAL;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x00;		/* AV/C CONTROL */
	buf[1] = 0xff;		/* UNIT */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0x00 | sfc;	/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	/* ACCEPTED or INTERIM is OK */
	if ((err < 6) || ((buf[0] != 0x0f) && (buf[0] != 0x09))) {
		dev_err(&unit->device, "failed to set sample rate\n");
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_general_get_sig_fmt(struct fw_unit *unit, int *rate,
			    enum avc_general_plug_dir dir,
			    unsigned short pid)
{
	int sfc, evt;
	u8 *buf;
	int err;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* Unit */
	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & pid;	/* plug id */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0xff;		/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	/* IMPLEMENTED/STABLE is OK */
	if ((err < 6) || (buf[0] != 0x0c)){
		dev_err(&unit->device, "failed to get sample rate\n");
		err = -EIO;
		goto end;
	}

	/* check EVT field */
	evt = (0x30 & buf[5]) >> 4;
	if (evt != 0) {	/* not AM824 */
		err = -EINVAL;
		goto end;
	}

	/* check sfc field */
	sfc = 0x07 & buf[5];
	if (sfc >= ARRAY_SIZE(amdtp_sfc_table)) {
		err = -EINVAL;
		goto end;
	}

	*rate = amdtp_sfc_table[sfc];
	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_general_get_plug_info(struct fw_unit *unit,
			unsigned short bus_plugs[2],
			unsigned short ext_plugs[2])
{
	u8 *buf;
	int err;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;	/* AV/C STATUS */
	buf[1] = 0xff;	/* UNIT */
	buf[2] = 0x02;	/* PLUG INFO */
	buf[3] = 0x00;

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	/* IMPLEMENTED/STABLE is OK */
	else if ((err < 6) || (buf[0] != 0x0c)) {
		err = -EIO;
		goto end;
	}

	bus_plugs[0] = buf[4];
	bus_plugs[1] = buf[5];
	ext_plugs[0] = buf[6];
	ext_plugs[1] = buf[7];

	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_ccm_get_sig_src(struct fw_unit *unit,
			int *src_stype, int *src_sid, int *src_pid,
			int dst_stype, int dst_sid, int dst_pid)
{
	int err;
	u8 *buf;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;	/* AV/C STATUS */
	buf[1] = 0xff;	/* UNIT */
	buf[2] = 0x1A;	/* SIGNAL SOURCE */
	buf[3] = 0x0f;
	buf[4] = 0xff;
	buf[5] = 0xfe;
	buf[6] = (0xf8 & (dst_stype << 3)) | dst_sid;
	buf[7] = 0xff & dst_pid;

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 0) || (buf[0] != 0x0c)) {
		dev_err(&unit->device,
			"failed to get signal status\n");
		err = -EIO;
		goto end;
	}

	*src_stype = buf[4] >> 3;
	*src_sid = buf[4] & 0x07;
	*src_pid = buf[5];
end:
	kfree(buf);
	return err;
}

int avc_ccm_set_sig_src(struct fw_unit *unit,
			int src_stype, int src_sid, int src_pid,
			int dst_stype, int dst_sid, int dst_pid)
{
	int err;
	u8 *buf;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x00;	/* AV/C CONTROL */
	buf[1] = 0xff;	/* UNIT */
	buf[2] = 0x1A;	/* SIGNAL SOURCE */
	buf[3] = 0x0f;
	buf[4] = (0xf8 & (src_stype << 3)) | src_sid;
	buf[5] = 0xff & src_pid;
	buf[6] = (0xf8 & (dst_stype << 3)) | dst_sid;
	buf[7] = 0xff & dst_pid;

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 0) || ((buf[0] != 0x09) && (buf[0] != 0x0f))) {
		dev_err(&unit->device,
			"failed to set signal status\n");
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_type(struct fw_unit *unit,
			       enum snd_bebob_plug_dir pdir,
			       enum snd_bebob_plug_unit punit,
			       unsigned short pid,
			       enum snd_bebob_plug_type *type)
{
	u8 *buf;
	int err;

	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* UNIT */
	buf[2] = 0x02;		/* PLUG INFO */
	buf[3] = 0xC0;		/* Extended Plug Info */
	buf[4] = pdir;		/* plug direction */
	buf[5] = 0x00;		/* address mode [0x00/0x01/0x02] */
	buf[6] = punit;		/* plug unit type */
	buf[7]  = 0xff & pid;	/* plug id */
	buf[8]  = 0xff;		/* reserved */
	buf[9]  = 0x00;		/* info type [0x00-0x07] */
	buf[10] = 0x00;		/* plug type in response */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	/* IMPLEMENTED/STABLE is OK */
	else if ((err < 6) || (buf[0] != 0x0c)) {
		err = -EIO;
		goto end;
	}

	*type = buf[10];
	err = 0;

end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_ch_pos(struct fw_unit *unit,
				 enum snd_bebob_plug_dir pdir,
				 unsigned short pid, u8 *buf, int len)
{
	int trial, err;

	/* check given buffer */
	if ((buf == NULL) || (len < 256)) {
		err = -EINVAL;
		goto end;
	}

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* Unit */
	buf[2] = 0x02;		/* PLUG INFO */
	buf[3] = 0xC0;		/* Extended Plug Info */
	buf[4] = pdir;		/* plug direction */
	buf[5] = 0x00;		/* address mode is 'Unit' */
	buf[6] = 0x00;		/* plug unit type is 'ISOC'*/
	buf[7] = 0xff & pid;	/* plug id */
	buf[8] = 0xff;		/* reserved */
	buf[9] = 0x03;		/* info type is 'channel position' */

	/*
	 * NOTE:
	 * M-Audio Firewire 410 returns 0x09 (ACCEPTED) just after changing
	 * signal format even if this command asks STATE. This is not in
	 * AV/C command specification.
	 */
	for (trial = 0; trial < BEBOB_COMMAND_MAX_TRIAL; trial++) {
		err = fcp_avc_transaction(unit, buf, 12, buf, 256, 0);
		if (err < 0)
			goto end;
		else if (err < 6) {
			err = -EIO;
			goto end;
		} else if (buf[0] == 0x0c)
			break;
		else if (trial < BEBOB_COMMAND_MAX_TRIAL)
			msleep(BEBOB_COMMAND_WAIT_MSEC);
		else {
			err = -EIO;
			goto end;
		}
	}

	/* strip command header */
	memmove(buf, buf + 10, err - 10);
	err = 0;
end:
	return err;
}

int avc_bridgeco_get_plug_cluster_type(struct fw_unit *unit,
				       enum snd_bebob_plug_dir pdir,
				       int pid, int cluster_id, u8 *type)
{
	u8 *buf;
	int err;

	/* cluster info includes characters but this module don't need it */
	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* UNIT */
	buf[2] = 0x02;		/* PLUG INFO */
	buf[3] = 0xc0;		/* Extended Plug Info */
	buf[4] = pdir;		/* plug direction */
	buf[5] = 0x00;		/* address mode is 'Unit' */
	buf[6] = 0x00;		/* plug unit type is 'ISOC' */
	buf[7] = 0xff & pid;	/* plug id */
	buf[8] = 0xff;		/* reserved */
	buf[9] = 0x07;		/* info type is 'cluster info' */
	buf[10] = 0xff & (cluster_id + 1);	/* cluster id */
	buf[11] = 0x00;		/* character length in response */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	else if ((err < 12) && (buf[0] != 0x0c)) {
		err = -EIO;
		goto end;
	}

	*type = buf[11];
	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_strm_fmt(struct fw_unit *unit,
				   enum snd_bebob_plug_dir pdir,
				   unsigned short pid,
				   int entryid, u8 *buf, int *len)
{
	int err;

	/* check given buffer */
	if ((buf == NULL) || (*len < 12)) {
		err = -EINVAL;
		goto end;
	}

	/* fill buffer as command */
	buf[0] = 0x01;			/* AV/C STATUS */
	buf[1] = 0xff;			/* unit */
	buf[2] = 0x2f;			/* opcode is STREAM FORMAT SUPPORT */
	buf[3] = 0xc1;			/* COMMAND LIST, BridgeCo extension */
	buf[4] = pdir;			/* plug direction */
	buf[5] = 0x00;			/* address mode is 'Unit' */
	buf[6] = 0x00;			/* plug unit type is 'ISOC' */
	buf[7] = 0xff & pid;		/* plug ID */
	buf[8] = 0xff;			/* reserved */
	buf[9] = 0xff;			/* stream status, 0xff in request */
	buf[10] = 0xff & entryid;	/* entry ID */

	err = fcp_avc_transaction(unit, buf, 12, buf, *len, 0);
	if (err < 0)
		goto end;
	/* reach the end of entries */
	else if (buf[0] == 0x0a) {
		err = 0;
		*len = 0;
		goto end;
	} else if (buf[0] != 0x0c) {
		err = -EINVAL;
		goto end;
	/* the header of this command is 11 bytes */
	} else if (err < 12) {
		err = -EIO;
		goto end;
	} else if (buf[10] != entryid) {
		err = -EIO;
		goto end;
	}

	/* strip command header */
	memmove(buf, buf + 11, err - 11);
	*len = err - 11;
	err = 0;
end:
	return err;
}