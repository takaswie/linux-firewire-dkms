/*
 * bebob_stream.c - driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
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

static int amdtp_sfc_table[] = {
	[CIP_SFC_32000]	 = 32000,
	[CIP_SFC_44100]	 = 44100,
	[CIP_SFC_48000]	 = 48000,
	[CIP_SFC_88200]	 = 88200,
	[CIP_SFC_96000]	 = 96000,
	[CIP_SFC_176400] = 176400,
	[CIP_SFC_192000] = 192000};

int avc_generic_set_sampling_rate(struct fw_unit *unit, int rate,
				  int direction, unsigned short plug)
{
	int sfc;
	u8 *buf;
	int err;

	for (sfc = 0; sfc < ARRAY_SIZE(amdtp_sfc_table); sfc += 1)
		if (amdtp_sfc_table[sfc] == rate)
			break;

	buf = kmalloc(8, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}

	buf[0] = 0x00;		/* AV/C CONTROL */
	buf[1] = 0xff;		/* unit */
	if (direction > 0)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & plug;	/* plug */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0x00 | sfc;	/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x09) /* ACCEPTED */) {
		dev_err(&unit->device, "failed to set sampe rate\n");
		err = -EIO;
		goto end;
	}

	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_generic_get_sampling_rate(struct fw_unit *unit, int *rate,
				  int direction, unsigned short plug)
{
	int sfc, evt;
	u8 *buf;
	int err;

	buf = kmalloc(8, GFP_KERNEL);
	if (!buf) {
		return -ENOMEM;
	}

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* unit */
	if (direction > 0)
		buf[2] = 0x19;	/* INPUT PLUG SIGNAL FORMAT */
	else
		buf[2] = 0x18;	/* OUTPUT PLUG SIGNAL FORMAT */
	buf[3] = 0xff & plug;	/* plug */
	buf[4] = 0x90;		/* EOH_1, Form_1, FMT means audio and music */
	buf[5] = 0xff;		/* FDF-hi */
	buf[6] = 0xff;		/* FDF-mid */
	buf[7] = 0xff;		/* FDF-low */

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x0c) /* IMPLEMENTED/STABLE */) {
		dev_err(&unit->device, "failed to get sampe rate\n");
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

int avc_generic_get_plug_info(struct fw_unit *unit,
			unsigned short bus_plugs[2],
			unsigned short ext_plugs[2])
{
	u8 *buf;
	int err;

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	memset(buf, 0xff, 8);
	buf[0] = 0x01;
	buf[1] = 0xff;
	buf[2] = 0x02;
	buf[3] = 0x00;

	err = fcp_avc_transaction(unit, buf, 8, buf, 8, 0);
	if (err < 0)
		goto end;
	else if ((err < 6) | (buf[0] != 0x0c /* IMPLEMENTED/STABLE */)) {
		err = -EIO;
		goto end;
	}

	bus_plugs[0] = buf[4];
	bus_plugs[1] = buf[5];
	ext_plugs[0] = buf[6];
	ext_plugs[1] = buf[7];

	err = 0;

end:
	return err;
}

int avc_bridgeco_get_plug_type(struct fw_unit *unit, int direction,
				    unsigned short plugid, int *type)
{
	u8 *buf;
	int err;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* unit */
	buf[2] = 0x02;		/* opcode is PLUG INFO */
	buf[3] = 0xC0;		/* sub function is extended for bridgeco */
	if (direction > 0)	/* plug direction [0x00/0x01] */
		buf[4] = 0x00;	/* input plug */
	else
		buf[4] = 0x01;	/* output plug */
	buf[5]  = 0x00;		/* address mode [0x00/0x01/0x02] */
	buf[6]  = 0x00;		/* plug type [0x00/0x01/0x02]*/
	buf[7]  = 0xff & plugid;	/* plug id */
	buf[8]  = 0xff;		/* reserved */
	buf[9]  = 0x00;		/* info type [0x00-0x07] */
	buf[10] = 0x00;		/* plug type in response */
	buf[11] = 0x00;		/* padding for quadlets */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	else if ((err < 6) | (buf[0] != 0x0c /* IMPLEMENTED/STABLE */)) {
		err = -EIO;
		goto end;
	}

	*type = buf[10];
	err = 0;

end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_channels(struct fw_unit *unit, int direction,
				   unsigned short plugid, int *channels)
{
	u8 *buf;
	int err;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;		/* AV/C STATUS */
	buf[1] = 0xff;		/* unit */
	buf[2] = 0x02;		/* opcode is PLUG INFO */
	buf[3] = 0xC0;		/* sub function is extended for bridgeco */
	if (direction > 0)	/* plug direction [0x00/0x01] */
		buf[4] = 0x00;	/* input plug */
	else
		buf[4] = 0x01;	/* output plug */
	buf[5] = 0x00;		/* address mode [0x00/0x01/0x02] */
	buf[6] = 0x00;		/* plug type [0x00/0x01/0x02]*/
	buf[7] = 0xff & plugid;	/* plug id */
	buf[8] = 0xff;		/* reserved */
	buf[9] = 0x02;		/* info type [0x00-0x07] */
	buf[10] = 0x00;		/* number of channels in response */
	buf[11] = 0x00;		/* padding for quadlets */

	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x0c /* IMPLEMENTED/STABLE */)) {
		err = -EIO;
		goto end;
	}

	*channels = buf[10];
	err = 0;

end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_channel_position(struct fw_unit *unit, int direction,
					unsigned short plugid, u8 *position)
{
	u8 *buf;
	int err;

	/*
	 * I cannot assume the length of return value of this command.
	 * So here I keep the maximum length of FCP.
	 */
	buf = kmalloc(256, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	memset(buf, 0, 256);
	buf[0] = 0x01;			/* AV/C STATUS */
	buf[1] = 0xff;			/* unit */
	buf[2] = 0x02;			/* opcode is PLUG INFO */
	buf[3] = 0xC0;			/* sub function is extended for bridgeco */
	if (direction > 0)	/* plug direction [0x00/0x01] */
		buf[4] = 0x00;	/* input plug */
	else
		buf[4] = 0x01;	/* output plug */
	buf[5] = 0x00;			/* address mode [0x00/0x01/0x02] */
	buf[6] = 0x00;			/* plug type [0x00/0x01/0x02]*/
	buf[7] = 0xff & plugid;		/* plug id */
	buf[8] = 0xff;			/* reserved */
	buf[9] = 0x03;			/* channel position */

	err = fcp_avc_transaction(unit, buf, 12, buf, 256, 0);
	if (err < 0)
		goto end;
	if ((err < 6) | (buf[0] != 0x0c /* IMPLEMENTED/STABLE */)) {
		err = -EIO;
		goto end;
	}

	/* TODO: length should be checked. */
	memcpy(position, buf + 10, err - 10);
	err = 0;

end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_cluster_info(struct fw_unit *unit, int direction,
				       int plugid, int cluster_id, u8 *format)
{
	u8 *buf;
	int err;

	/* cluster info includes characters for name but we don't need it */
	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	buf[0] = 0x01;
	buf[1] = 0xff;
	buf[2] = 0x02;
	buf[3] = 0xc0;
	if (direction > 0)	/* plug direction [0x00/0x01] */
		buf[4] = 0x00;	/* input plug */
	else
		buf[4] = 0x01;	/* output plug */
	buf[5] = 0x00;
	buf[6] = 0x00;
	buf[7] = 0xff & plugid;
	buf[8] = 0xff;
	buf[9] = 0x07;
	buf[10] = 0xff & (cluster_id + 1);
	buf[11] = 0x00;

	/* transaction */
	err = fcp_avc_transaction(unit, buf, 12, buf, 12, 0);
	if (err < 0) {
		goto end;
	} else if (err < 12) {
		err = -EIO;
		goto end;
	} else if (buf[0] == 0x0a) {
		*format = -1;
		goto end;
	} else if (buf[0] != 0x0c) {
		err = -EIO;
		goto end;
	}

	*format = buf[11];
	err = 0;

end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_stream_formation_entry(struct fw_unit *unit,
				int direction, unsigned short plugid,
				int entryid, u8 *buf, int *len)
{
	int err;

	/* check buffer parameters */
	if ((buf == NULL) || (*len < 13)) {
		err = -EINVAL;
		goto end;
	}

	/* fill buffer as command */
	buf[0] = 0x01;			/* AV/C STATUS */
	buf[1] = 0xff;			/* unit */
	buf[2] = 0x2f;			/* opcode is STREAM FORMAT SUPPORT */
	buf[3] = 0xc1;			/* COMMAND LIST, BridgeCo extension */
	if (direction > 0)	/* plug direction [0x00/0x01] */
		buf[4] = 0x00;	/* input plug */
	else
		buf[4] = 0x01;	/* output plug */
	buf[5] = 0x00;			/* address mode is 'Unit' */
	buf[6] = 0x00;			/* plug type is 'PCR' */
	buf[7] = 0xff & plugid;		/* plug ID */
	buf[8] = 0xff;			/* reserved */
	buf[9] = 0xff;			/* no meaning, just fill */
	buf[10] = 0xff & entryid;	/* entry ID */
	buf[11] = 0x00;			/* padding */

	/* transaction */
	err = fcp_avc_transaction(unit, buf, 12, buf, *len, 0);
	if ((err < 0) || (buf[0] != 0x0c))
		goto end;
	else if (err < 6) {
		err = -EIO;
		goto end;
	}

	*len = err;
	err = 0;

end:
	return err;
}
