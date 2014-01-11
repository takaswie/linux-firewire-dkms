/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Takashi Sakamoto <o-takashi@sakamocchi.jp>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

/*
 * According to their datasheet:
 *  OXFW970: 32.0/44.1/48.0/96.0 Khz, 8 audio channels I/O
 *  OXFW971: 32.0/44.1/48.0/88.2/96.0/192.0 kHz, 16 audio channels I/O, MIDI I/O
 */
const unsigned int snd_oxfw_rate_table[SND_OXFW_STREAM_TABLE_ENTRIES] = {
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 176400,
	[6] = 192000,
};

/*
 * See Table 5.7 – Sampling frequency for Multi-bit Audio
 * at AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA)
 */
static const unsigned int avc_stream_rate_table[] = {
	[0] = 0x02,
	[1] = 0x03,
	[2] = 0x04,
	[3] = 0x0a,
	[4] = 0x05,
	[5] = 0x06,
	[6] = 0x07,
};

int snd_oxfw_stream_get_rate(struct snd_oxfw *oxfw, unsigned int *rate)
{
	unsigned int tx_rate, rx_rate;
	int err;

	err = snd_oxfw_command_get_rate(oxfw, AVC_GENERAL_PLUG_DIR_OUT,
					&tx_rate);
	if (err < 0)
		goto end;

	err = snd_oxfw_command_get_rate(oxfw, AVC_GENERAL_PLUG_DIR_IN,
					&rx_rate);
	if (err < 0)
		goto end;

	*rate = rx_rate;
	if (rx_rate == tx_rate)
		goto end;

	/* synchronize receive stream rate to transmit stream rate */
	err = snd_oxfw_command_set_rate(oxfw, AVC_GENERAL_PLUG_DIR_IN,
					rx_rate);
end:
	return err;
}

int snd_oxfw_stream_set_rate(struct snd_oxfw *oxfw, unsigned int rate)
{
	int err;

	err = snd_oxfw_command_set_rate(oxfw, AVC_GENERAL_PLUG_DIR_OUT, rate);
	if (err < 0)
		goto end;

	err = snd_oxfw_command_set_rate(oxfw, AVC_GENERAL_PLUG_DIR_IN, rate);
end:
	return err;
}

static int stream_init(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &oxfw->tx_stream) {
		conn = &oxfw->out_conn;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_IN_STREAM;
	} else {
		conn = &oxfw->in_conn;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_OUT_STREAM;
	}

	err = cmp_connection_init(conn, oxfw->unit, c_dir, 0);
	if (err < 0) {
		fw_unit_put(oxfw->unit);
		goto end;
	}

	err = amdtp_stream_init(stream, oxfw->unit, s_dir, CIP_NONBLOCKING);
	if (err < 0)
		cmp_connection_destroy(conn);
end:
	return err;
}

int snd_oxfw_stream_start(struct snd_oxfw *oxfw,
			   struct amdtp_stream *stream,
			   unsigned int rate)
{
	unsigned int i, curr_rate, pcm_channels, midi_ports;
	struct cmp_connection *conn;
	enum avc_general_plug_dir dir;
	bool used;
	int err;

	/* already start or not */
	if (amdtp_stream_running(stream)) {
		err = 0;
		goto end;
	}

	if (stream == &oxfw->tx_stream) {
		conn = &oxfw->out_conn;
		dir = AVC_GENERAL_PLUG_DIR_OUT;
	} else {
		conn = &oxfw->in_conn;
		dir = AVC_GENERAL_PLUG_DIR_IN;
	}

	/* check other's connection */
	err = cmp_connection_check_used(conn, &used);
	if (err < 0)
		goto end;
	else if (used) {
		err = -EBUSY;
		goto end;
	};

	/* arrange sampling rate */
	err = snd_oxfw_stream_get_rate(oxfw, &curr_rate);
	if (err < 0)
		goto end;
	if (curr_rate != rate) {
		err = snd_oxfw_stream_set_rate(oxfw, rate);
		if (err < 0)
			goto end;
	}

	/* set stream formation */
	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		if (snd_oxfw_rate_table[i] == rate)
			break;
	}
	if (i == SND_OXFW_STREAM_TABLE_ENTRIES) {
		err = -EINVAL;
		goto end;
	}
	if (stream == &oxfw->tx_stream) {
		pcm_channels = oxfw->tx_stream_formations[i].pcm;
		midi_ports = oxfw->tx_stream_formations[i].midi;
	} else {
		pcm_channels = oxfw->rx_stream_formations[i].pcm;
		midi_ports = oxfw->rx_stream_formations[i].midi;
	}
	if (pcm_channels == 0) {
		err = -EINVAL;
		goto end;
	}
	amdtp_stream_set_parameters(stream, rate, pcm_channels, midi_ports);

	/* establish connection */
	err = cmp_connection_establish(conn,
			amdtp_stream_get_max_payload(stream));
	if (err < 0)
		goto end;

	/* start stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
	if (err < 0)
		cmp_connection_break(conn);
end:
	return err;
}

void snd_oxfw_stream_stop(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	if (amdtp_stream_running(stream))
		amdtp_stream_stop(stream);

	if (stream == &oxfw->tx_stream)
		cmp_connection_break(&oxfw->out_conn);
	else
		cmp_connection_break(&oxfw->in_conn);
}

void snd_oxfw_stream_destroy(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	amdtp_stream_pcm_abort(stream);
	snd_oxfw_stream_stop(oxfw, stream);
}

void snd_oxfw_stream_update(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (stream == &oxfw->tx_stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	if (cmp_connection_update(conn) < 0) {
		amdtp_stream_pcm_abort(stream);
		snd_oxfw_stream_stop(oxfw, stream);
	} else {
		amdtp_stream_update(stream);
	}
}

int firewave_stream_discover(struct snd_oxfw *oxfw)
{
	oxfw->rx_stream_formations[2].pcm = 6;
	oxfw->rx_stream_formations[3].pcm = 6;
	oxfw->rx_stream_formations[4].pcm = 2;
	oxfw->rx_stream_formations[6].pcm = 2;

	return 0;
}

int lacie_speakers_stream_discover(struct snd_oxfw *oxfw)
{
	oxfw->rx_stream_formations[2].pcm = 2;
	oxfw->rx_stream_formations[3].pcm = 2;
	oxfw->rx_stream_formations[4].pcm = 2;
	oxfw->rx_stream_formations[5].pcm = 2;
	oxfw->rx_stream_formations[6].pcm = 2;

	return 0;
}

/*
 * See Table 6.16 - AM824 Stream Format
 *     Figure 6.19 - format_information field for AM824 Compound
 * at AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA)
 */
static int
parse_stream_formation(u8 *buf, unsigned int len,
		       struct snd_oxfw_stream_formation *formation,
		       unsigned int *index)
{
	unsigned int e, channels, format;

	/*
	 * this module can support a hierarchy combination that:
	 *  Root:	Audio and Music (0x90)
	 *  Level 1:	AM824 Compound  (0x40)
	 */
	if ((buf[0] != 0x90) || (buf[1] != 0x40))
		return -ENOSYS;

	/* check the sampling rate */
	for (*index = 0; *index < sizeof(avc_stream_rate_table); *index += 1) {
		if (buf[2] == avc_stream_rate_table[*index])
			break;
	}
	if (*index == sizeof(avc_stream_rate_table))
		return -ENOSYS;

	for (e = 0; e < buf[4]; e++) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* IEC 60958-3 */
		case 0x00:
		/* Multi Bit Linear Audio (Raw) */
		case 0x06:
			formation[*index].pcm += channels;
			break;
		/* MIDI comformant */
		case 0x0d:
			formation[*index].midi += channels;
			break;
		/* Multi Bit Linear Audio (DVD-audio) */
		case 0x07:
		/* IEC 61937-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* One Bit Audio */
		case 0x08:	/* (Plain) Raw */
		case 0x09:	/* (Plain) SACD */
		case 0x0a:	/* (Encoded) Raw */
		case 0x0b:	/* (ENcoded) SACD */
		/* High precision Multi-bit Linear Audio */
		case 0x0c:
		/* SMPTE Time-Code conformant */
		case 0x0e:
		/* Sample Count */
		case 0x0f:
		/* Anciliary Data */
		case 0x10:
		/* Synchronization Stream (Stereo Raw audio) */
		case 0x40:
		/* Don't care */
		case 0xff:
		default:
			break;	/* not supported */
		}
	}

	return 0;
}

static int
assume_stream_formations(struct snd_oxfw *oxfw, enum avc_general_plug_dir dir,
			 unsigned int pid, u8 *buf, unsigned int *len,
			 struct snd_oxfw_stream_formation *formations)
{
	unsigned int i, pcm_channels, midi_channels;
	int err;

	/* get formation at current sampling rate */
	err = avc_stream_get_format_single(oxfw->unit, dir, pid, buf, len);
	if ((err < 0) || (err == 0x80) /* NOT IMPLEMENTED */)
		goto end;

	/* parse and set stream formation */
	err = parse_stream_formation(buf, *len, formations, &i);
	if (err < 0)
		goto end;

	pcm_channels = formations[i].pcm;
	midi_channels = formations[i].midi;

	/* apply the formation for each available sampling rate */
	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		err = avc_general_inquiry_sig_fmt(oxfw->unit,
						  snd_oxfw_rate_table[i],
						  dir, pid);
		if ((err < 0) || (err == 0x08) /* NOT IMPLEMENTED */)
			continue;

		formations[i].pcm = pcm_channels;
		formations[i].midi = midi_channels;
	}
end:
	return err;
}

static int
fill_stream_formations(struct snd_oxfw *oxfw, enum avc_general_plug_dir dir,
		       unsigned short pid)
{
	u8 *buf;
	struct snd_oxfw_stream_formation *formations;
	unsigned int i, len, eid;
	int err;

	buf = kmalloc(AVC_GENERIC_FRAME_MAXIMUM_BYTES, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (dir == AVC_GENERAL_PLUG_DIR_OUT)
		formations = oxfw->tx_stream_formations;
	else
		formations = oxfw->rx_stream_formations;

	/* initialize parameters here because of checking implementation */
	eid = 0;
	len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
	memset(buf, 0, len);

	/* get first entry */
	err = avc_stream_get_format_list(oxfw->unit, dir, 0, buf, &len, eid);
	if ((err < 0) || (len < 3)) {
		/* LIST subfunction is not implemented */
		err = assume_stream_formations(oxfw, dir, pid, buf, &len,
					       formations);
		goto end;
	}

	/* LIST subfunction is implemented */
	do {
		/* parse and set stream formation */
		err = parse_stream_formation(buf, len, formations, &i);
		if (err < 0)
			continue;

		/* get next entry */
		len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
		memset(buf, 0, len);
		err = avc_stream_get_format_list(oxfw->unit, dir, 0,
						 buf, &len, ++eid);
		if ((err < 0) || (len < 3))
			break;
	} while (eid < SND_OXFW_STREAM_TABLE_ENTRIES);
end:
	kfree(buf);
	return err;
}

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw)
{
	u8 plugs[AVC_PLUG_INFO_BUF_COUNT];
	int err;

	/* the number of plugs for isoc in/out, ext in/out  */
	err = avc_general_get_plug_info(oxfw->unit, 0x1f, 0x07, 0x00, plugs);
	if (err < 0)
		goto end;
	if ((plugs[0] == 0) || (plugs[0] == 0)) {
		err = -EIO;
		goto end;
	}

	/* use oPCR[0] */
	err = fill_stream_formations(oxfw, AVC_GENERAL_PLUG_DIR_OUT, 0);
	if (err < 0)
		goto end;

	/* use iPCR[0] */
	err = fill_stream_formations(oxfw, AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0)
		goto end;
end:
	return err;
}

int snd_oxfw_streams_init(struct snd_oxfw *oxfw)
{
	int err;

	err = stream_init(oxfw, &oxfw->rx_stream);
	if (err < 0)
		goto end;

	/* 44.1kHz is the most popular */
	if (oxfw->tx_stream_formations[1].pcm > 0)
		err = stream_init(oxfw, &oxfw->tx_stream);
end:
	return err;
}
