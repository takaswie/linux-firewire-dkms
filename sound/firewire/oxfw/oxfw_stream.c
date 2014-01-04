/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./oxfw.h"

/*
 * According to their datasheet:
 *  OXFW970 32.0/44.1/48.0/96.0 Khz, 4 audio channels I/O
 *  OXFW971: 32.0/44.1/48.0/88.2/96.0 kHz, 16 audio channels I/O, MIDI I/O
 *
 * OXFW970 seems not to implement 'LIST' subfunction for 'EXTENDED STREAM
 * FORMAT INFORMATION' defined in 'AV/C Stream Format Information
 * Specification 1.1 (Apr 2005, 1394TA)'. So this module uses an assumption
 * that OXFW970 doesn't change its formation of channels in AMDTP stream.
 *
 * They transmit packet following to 'CIP header with  SYT field' defined in
 * IEC 61883-1. But the sequence of value in SYT field is not compliant. So
 * this module doesn't use the value of SYT field in in-packets. Then this
 * module performs as a 'master of synchronization'. In this way, this module
 * hopes the device to pick up the value of SYT value in out-packet which
 * this module transmits. But the device seems not to use it for in-packet
 * which the device transmits. Concluding, it doesn't matter whether this
 * module perform as a master or slave.
 */
const unsigned int snd_oxfw_rate_table[SND_OXFW_RATE_TABLE_ENTRIES] = {
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

int snd_oxfw_stream_get_rate(struct snd_oxfw *oxfw, unsigned int *curr_rate)
{
	unsigned int tx_rate, rx_rate;
	int err;

	err = snd_oxfw_get_rate(oxfw, &tx_rate, AVC_GENERAL_PLUG_DIR_OUT);
	if (err < 0)
		goto end;

	err = snd_oxfw_get_rate(oxfw, &rx_rate, AVC_GENERAL_PLUG_DIR_IN);
	if (err < 0)
		goto end;

	*curr_rate = rx_rate;
	if (rx_rate == tx_rate)
		goto end;

	/* synchronize receive stream rate to transmit stream rate */
	err = snd_oxfw_set_rate(oxfw, rx_rate, AVC_GENERAL_PLUG_DIR_IN);
end:
	return err;
}

int snd_oxfw_stream_set_rate(struct snd_oxfw *oxfw, unsigned int rate)
{
	int err;

	err = snd_oxfw_set_rate(oxfw, rate, AVC_GENERAL_PLUG_DIR_OUT);
	if (err < 0)
		goto end;

	err = snd_oxfw_set_rate(oxfw, rate, AVC_GENERAL_PLUG_DIR_IN);
end:
	return err;
}

static int
check_connection_used_by_others(struct snd_oxfw *oxfw,
				struct amdtp_stream *s, bool *used)
{
	struct cmp_connection *conn;
	int err;

	if (s == &oxfw->tx_stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	err = cmp_connection_check_used(conn, used);
	if (err >= 0)
		*used = (*used && !amdtp_stream_running(s));

	return err;
}

static int
init_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
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
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, oxfw->unit, s_dir, CIP_NONBLOCKING);
	if (err < 0)
		cmp_connection_destroy(conn);
end:
	return err;
}


static void
stop_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	if (amdtp_stream_running(stream))
		amdtp_stream_stop(stream);

	if (stream == &oxfw->tx_stream)
		cmp_connection_break(&oxfw->out_conn);
	else
		cmp_connection_break(&oxfw->in_conn);

	return;
}

static int
start_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream,
	     unsigned int sampling_rate)
{
	struct cmp_connection *conn;
	unsigned int i, pcm_channels, midi_ports;
	int err;

	/* already running */
	if (amdtp_stream_running(stream)) {
		err = 0;
		goto end;
	}

	for (i = 0; i < sizeof(snd_oxfw_rate_table); i++) {
		if (snd_oxfw_rate_table[i] == sampling_rate)
			break;
	}
	if (i == sizeof(snd_oxfw_rate_table)) {
		err = -EINVAL;
		goto end;
	}

	/* set stream formation */
	if (stream == &oxfw->tx_stream) {
		conn = &oxfw->out_conn;
		pcm_channels = oxfw->tx_stream_formations[i].pcm;
		midi_ports = oxfw->tx_stream_formations[i].midi * 8;
	} else {
		conn = &oxfw->in_conn;
		pcm_channels = oxfw->rx_stream_formations[i].pcm;
		midi_ports = oxfw->rx_stream_formations[i].midi * 8;
	}
	amdtp_stream_set_parameters(stream, sampling_rate,
				    pcm_channels, midi_ports);

	/*  establish connection via CMP */
	err = cmp_connection_establish(conn,
				amdtp_stream_get_max_payload(stream));
	if (err < 0)
		goto end;

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
	if (err < 0)
		stop_stream(oxfw, stream);

	/* wait first callback */
	if (!amdtp_stream_wait_callback(stream)) {
		stop_stream(oxfw, stream);
		err = -ETIMEDOUT;
	}
end:
	return err;
}

static void
update_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (&oxfw->tx_stream == stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	if (cmp_connection_update(conn) < 0) {
		amdtp_stream_pcm_abort(stream);
		mutex_lock(&oxfw->mutex);
		stop_stream(oxfw, stream);
		mutex_unlock(&oxfw->mutex);
		return;
	}
	amdtp_stream_update(stream);
}

static void
destroy_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	stop_stream(oxfw, stream);

	if (stream == &oxfw->tx_stream)
		cmp_connection_destroy(&oxfw->out_conn);
	else
		cmp_connection_destroy(&oxfw->in_conn);
}

static int
get_roles(struct snd_oxfw *oxfw, enum cip_flags *sync_mode,
	  struct amdtp_stream **master, struct amdtp_stream **slave)
{
	/* It doesn't matter. So this module perform as a sync master */
	*sync_mode = 0x00;
	*master = &oxfw->rx_stream;
	*slave = &oxfw->tx_stream;

	return 0;
}

int snd_oxfw_stream_init_duplex(struct snd_oxfw *oxfw)
{
	int err;

	err = init_stream(oxfw, &oxfw->tx_stream);
	if (err < 0)
		goto end;

	err = init_stream(oxfw, &oxfw->rx_stream);
end:
	return err;
}

int snd_oxfw_stream_start_duplex(struct snd_oxfw *oxfw,
				  struct amdtp_stream *request,
				  unsigned int rate)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	unsigned int curr_rate;
	bool slave_flag, used;
	int err;

	mutex_lock(&oxfw->mutex);

	err = get_roles(oxfw, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if ((request == slave) || amdtp_stream_running(slave))
		slave_flag = true;
	else
		slave_flag = false;

	/*
	 * Considering JACK/FFADO streaming:
	 * TODO: This can be removed hwdep functionality becomes popular.
	 */
	err = check_connection_used_by_others(oxfw, master, &used);
	if (err < 0)
		goto end;
	if (used) {
		dev_err(&oxfw->unit->device,
			"connections established by others: %d\n",
			used);
		err = -EBUSY;
		goto end;
	}

	/* get current rate */
	err = snd_oxfw_stream_get_rate(oxfw, &curr_rate);
	if (err < 0)
		goto end;
	if (rate == 0)
		rate = curr_rate;

	/* change sampling rate if needed */
	if (rate != curr_rate) {
		/* slave is just for MIDI stream */
		if (amdtp_stream_running(slave) &&
		    !amdtp_stream_pcm_running(slave))
			amdtp_stream_stop(slave);

		/* master is just for MIDI stream */
		if (amdtp_stream_running(master) &&
		    !amdtp_stream_pcm_running(master))
			amdtp_stream_stop(master);

		err = snd_oxfw_stream_set_rate(oxfw, rate);
		if (err < 0)
			goto end;
	}

	/* master should be always running */
	if (!amdtp_stream_running(master)) {
		err = start_stream(oxfw, master, rate);
		if (err < 0) {
			dev_err(&oxfw->unit->device,
				"fail to run AMDTP master stream:%d\n", err);
			goto end;
		}
	}

	/* start slave if needed */
	if (slave_flag && !amdtp_stream_running(slave)) {
		err = start_stream(oxfw, slave, rate);
		if (err < 0)
			dev_err(&oxfw->unit->device,
				"fail to run AMDTP slave stream:%d\n", err);
	}
end:
	mutex_unlock(&oxfw->mutex);
	return err;
}

int snd_oxfw_stream_stop_duplex(struct snd_oxfw *oxfw)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err;

	mutex_lock(&oxfw->mutex);

	err = get_roles(oxfw, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if (amdtp_stream_pcm_running(slave) ||
	    amdtp_stream_midi_running(slave))
		goto end;

	stop_stream(oxfw, slave);

	if (amdtp_stream_pcm_running(master) ||
	    amdtp_stream_midi_running(master))
		goto end;

	stop_stream(oxfw, master);
end:
	mutex_unlock(&oxfw->mutex);
	return err;
}

void snd_oxfw_stream_update_duplex(struct snd_oxfw *oxfw)
{
	mutex_lock(&oxfw->mutex);

	update_stream(oxfw, &oxfw->rx_stream);
	update_stream(oxfw, &oxfw->tx_stream);

	mutex_unlock(&oxfw->mutex);
}

void snd_oxfw_stream_destroy_duplex(struct snd_oxfw *oxfw)
{
	mutex_lock(&oxfw->mutex);

	if (amdtp_stream_pcm_running(&oxfw->rx_stream))
		amdtp_stream_pcm_abort(&oxfw->rx_stream);
	if (amdtp_stream_pcm_running(&oxfw->tx_stream))
		amdtp_stream_pcm_abort(&oxfw->tx_stream);

	destroy_stream(oxfw, &oxfw->rx_stream);
	destroy_stream(oxfw, &oxfw->tx_stream);

	mutex_unlock(&oxfw->mutex);
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
	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
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

	if (dir == AVC_GENERAL_PLUG_DIR_IN)
		formations = oxfw->rx_stream_formations;
	else
		formations = oxfw->tx_stream_formations;

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
	} while (eid < SND_OXFW_RATE_TABLE_ENTRIES);
end:
	kfree(buf);
	return err;
}

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw)
{
	u8 plugs[AVC_PLUG_INFO_BUF_COUNT];
	unsigned int i;
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

	/* if its stream has MIDI conformant data channel, add one MIDI port */
	for (i = 0; i < SND_OXFW_RATE_TABLE_ENTRIES; i++) {
		if (oxfw->tx_stream_formations[i].midi > 0)
			oxfw->midi_input_ports = 1;
		else if (oxfw->rx_stream_formations[i].midi > 0)
			oxfw->midi_output_ports = 1;
	}
end:
	return err;
}

void snd_oxfw_stream_lock_changed(struct snd_oxfw *oxfw)
{
	oxfw->dev_lock_changed = true;
	wake_up(&oxfw->hwdep_wait);
}

int snd_oxfw_stream_lock_try(struct snd_oxfw *oxfw)
{
	int err;

	spin_lock_irq(&oxfw->lock);

	/* user land lock this */
	if (oxfw->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (oxfw->dev_lock_count++ == 0)
		snd_oxfw_stream_lock_changed(oxfw);
	err = 0;
end:
	spin_unlock_irq(&oxfw->lock);
	return err;
}

void snd_oxfw_stream_lock_release(struct snd_oxfw *oxfw)
{
	spin_lock_irq(&oxfw->lock);

	if (WARN_ON(oxfw->dev_lock_count <= 0))
		goto end;
	if (--oxfw->dev_lock_count == 0)
		snd_oxfw_stream_lock_changed(oxfw);
end:
	spin_unlock_irq(&oxfw->lock);
}
