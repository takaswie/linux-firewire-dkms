/*
 * bebob_command.c - driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./bebob.h"

int avc_audio_set_selector(struct fw_unit *unit, unsigned int subunit_id,
			   unsigned int fb_id, unsigned int num)
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

	/* do transaction and check buf[1-8] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, 12,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7) | BIT(8));
	if (err > 0 && err < 9)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (err > 0)
		err = 0;

	kfree(buf);
	return err;
}

int avc_audio_get_selector(struct fw_unit *unit, unsigned int subunit_id,
			   unsigned int fb_id, unsigned int *num)
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
	buf[7]  = 0xff;		/* input function block plug number */
	buf[8]  = 0x01;		/* control selector is SELECTOR_CONTROL */

	/* do transaction and check buf[1-6,8] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, 12,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(8));
	if (err > 0 && err < 9)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	*num = buf[7];
	err = 0;
end:
	kfree(buf);
	return err;
}

static inline void
avc_bridgeco_fill_command_base(u8 *buf, unsigned int ctype, unsigned int opcode,
			       unsigned int subfunction,
			       u8 addr[AVC_BRIDGECO_ADDR_BYTES])
{
	buf[0] = 0x7 & ctype;
	buf[1] = addr[0];
	buf[2] = 0xff & opcode;
	buf[3] = 0xff & subfunction;
	buf[4] = addr[1];
	buf[5] = addr[2];
	buf[6] = addr[3];
	buf[7] = addr[4];
	buf[8] = addr[5];
}

int avc_bridgeco_get_plug_type(struct fw_unit *unit,
			       u8 addr[AVC_BRIDGECO_ADDR_BYTES],
			       enum avc_bridgeco_plug_type *type)
{
	u8 *buf;
	int err;

	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* status for plug info with bridgeco extension */
	avc_bridgeco_fill_command_base(buf, 0x01, 0x02, 0xc0, addr);
	buf[9]  = 0x00;		/* info type is 'plug type' */
	buf[10] = 0xff;		/* plug type in response */

	/* do transaction and check buf[1-7,9] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, 12,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7) | BIT(9));
	if (err > 0 && err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	*type = buf[10];
	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_ch_pos(struct fw_unit *unit,
				 u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				 u8 *buf, unsigned int len)
{
	int err;

	/* check given buffer */
	if ((buf == NULL) || (len < 256)) {
		err = -EINVAL;
		goto end;
	}

	/* status for plug info with bridgeco extension */
	avc_bridgeco_fill_command_base(buf, 0x01, 0x02, 0xc0, addr);

	/* info type is 'channel position' */
	buf[9] = 0x03;

	/* do transaction and check buf[1-7,9] are the same */
	err = fcp_avc_transaction(unit, buf, 12, buf, 256,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) |
				  BIT(5) | BIT(6) | BIT(7) | BIT(9));
	if (err > 0 && err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	/* strip command header */
	memmove(buf, buf + 10, err - 10);
	err = 0;
end:
	return err;
}

int avc_bridgeco_get_plug_section_type(struct fw_unit *unit,
				       u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				       unsigned int section_id, u8 *type)
{
	u8 *buf;
	int err;

	/* section info includes charactors but this module don't need it */
	buf = kzalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* status for plug info with bridgeco extension */
	avc_bridgeco_fill_command_base(buf, 0x01, 0x02, 0xc0, addr);

	buf[9] = 0x07;		/* info type is 'section info' */
	buf[10] = 0xff & (section_id + 1);	/* section id */
	buf[11] = 0x00;		/* type in response */

	/* do transaction and check buf[1-7,9,10] are the same */
	err = fcp_avc_transaction(unit, buf, 12, buf, 12,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7) | BIT(9) | BIT(10));
	if (err > 0 && err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	*type = buf[11];
	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_input(struct fw_unit *unit,
				u8 addr[AVC_BRIDGECO_ADDR_BYTES], u8 input[7])
{
	int err;
	u8 *buf;

	buf = kzalloc(18, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* status for plug info with bridgeco extension */
	avc_bridgeco_fill_command_base(buf, 0x01, 0x02, 0xc0, addr);

	/* info type is 'Plug Input Specific Data' */
	buf[9] = 0x05;

	/* do transaction and check buf[1-7] are the same */
	err = fcp_avc_transaction(unit, buf, 16, buf, 16,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7));
	if (err > 0 && err < 8)
		err = -EIO;
	else if (buf[0] == 0x08) /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a) /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b) /* IN TRANSITION */
		err = -EAGAIN;
	if (err < 0)
		goto end;

	memcpy(input, buf + 10, 5);
	err = 0;
end:
	kfree(buf);
	return err;
}

int avc_bridgeco_get_plug_strm_fmt(struct fw_unit *unit,
				   u8 addr[AVC_BRIDGECO_ADDR_BYTES],
				   unsigned int entryid, u8 *buf,
				   unsigned int *len)
{
	int err;

	/* check given buffer */
	if ((buf == NULL) || (*len < 12)) {
		err = -EINVAL;
		goto end;
	}

	/* status for plug info with bridgeco extension */
	avc_bridgeco_fill_command_base(buf, 0x01, 0x2f, 0xc1, addr);

	buf[9] = 0xff;			/* stream status in response */
	buf[10] = 0xff & entryid;	/* entry ID */

	/* do transaction and check buf[1-7,10] are the same against command */
	err = fcp_avc_transaction(unit, buf, 12, buf, *len,
				  BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) |
				  BIT(6) | BIT(7) | BIT(10));
	if ((err > 0) && (err < 12))
		err = -EIO;
	else if (buf[0] == 0x08)        /* NOT IMPLEMENTED */
		err = -ENOSYS;
	else if (buf[0] == 0x0a)        /* REJECTED */
		err = -EINVAL;
	else if (buf[0] == 0x0b)        /* IN TRANSITION */
		err = -EAGAIN;
	else if (buf[10] != entryid)
		err = -EIO;
	if (err < 0)
		goto end;

	/* strip command header */
	memmove(buf, buf + 11, err - 11);
	*len = err - 11;
	err = 0;
end:
	return err;
}
