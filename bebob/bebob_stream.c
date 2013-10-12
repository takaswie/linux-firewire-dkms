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

/* 128 is an arbitrary number but it's enough */
#define FORMATION_MAXIMUM_LENGTH 128

unsigned int snd_bebob_rate_table[SND_BEBOB_STREAM_FORMATION_ENTRIES] = {
	[0] = 22050,
	[1] = 24000,
	[2] = 32000,
	[3] = 44100,
	[4] = 48000,
	[5] = 88200,
	[6] = 96000,
	[7] = 176400,
	[8] = 192000,
};

static int
get_formation_index(int sampling_rate)
{
        int i;

        for (i = 0; i < sizeof(snd_bebob_rate_table); i += 1) {
                if (snd_bebob_rate_table[i] == sampling_rate)
                        return i;
	}
	return -1;
}

int snd_bebob_stream_map(struct snd_bebob *bebob,
			 struct amdtp_stream *stream)
{
	unsigned int cl, ch, clusters, channels, pos, pcm, midi;
	u8 *buf, type;
	int dir, err;

	/* maybe 256 is enough... */
	buf = kzalloc(256, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto end;
	}

	if (stream == &bebob->tx_stream)
		dir = 0;
	else
		dir = 1;

	err = avc_bridgeco_get_plug_channel_position(bebob->unit, dir, 0, buf);
	if (err < 0)
		goto end;

	clusters = *buf;
	buf++;
	pcm = 0;
	midi = 0;
	for (cl = 0; cl < clusters; cl++) {
		err = avc_bridgeco_get_plug_cluster_type(bebob->unit, dir, 0,
							 cl, &type);
		if (err < 0)
			goto end;

		channels = *buf;
		buf++;
		for (ch = 0; ch < channels; ch++) {
			pos = *buf - 1;
			if (type != 0x0a)
				stream->pcm_positions[pcm++] = pos;
			else
				stream->midi_positions[midi++] = pos;
			buf += 2;
		}
	}

end:
	return err;
}

static int
stream_init(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &bebob->tx_stream) {
		conn= &bebob->out_conn;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_IN_STREAM;
	} else {
		conn= &bebob->in_conn;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_OUT_STREAM;
	}

	err = cmp_connection_init(conn, bebob->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, bebob->unit, s_dir, CIP_BLOCKING);
	if (err < 0) {
		cmp_connection_destroy(conn);
		goto end;
	}

end:
	return err;
}

static int
stream_start(struct snd_bebob *bebob, struct amdtp_stream *stream,
	     unsigned int sampling_rate)
{
	struct snd_bebob_stream_formation *formations;
	struct cmp_connection *conn;
	unsigned int index, pcm_channels, midi_channels;
	int err = 0;

	/* already running */
	if (amdtp_stream_running(stream))
		goto end;

	if (stream == &bebob->tx_stream) {
		formations = bebob->tx_stream_formations;
		conn= &bebob->out_conn;
	} else {
		formations = bebob->rx_stream_formations;
		conn= &bebob->in_conn;
	}

	index = get_formation_index(sampling_rate);
	pcm_channels = formations[index].pcm;
	midi_channels = formations[index].midi;

	amdtp_stream_set_params(stream, sampling_rate,
				pcm_channels, midi_channels);

	/* channel mapping */
	if (bebob->spec->map != NULL) {
		err = bebob->spec->map(bebob, stream);
		if (err < 0)
			goto end;
	}

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
		cmp_connection_break(conn);

end:
	return err;
}

static void
stream_stop(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	if (amdtp_stream_running(stream))
		amdtp_stream_stop(stream);

	if (stream == &bebob->tx_stream)
		cmp_connection_break(&bebob->out_conn);
	else
		cmp_connection_break(&bebob->in_conn);

	return;
}

static void
stream_update(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (stream == &bebob->tx_stream)
		conn = &bebob->out_conn;
	else
		conn = &bebob->in_conn;

	if (cmp_connection_update(conn) < 0) {
		amdtp_stream_pcm_abort(stream);
		mutex_lock(&bebob->mutex);
		stream_stop(bebob, stream);
		mutex_unlock(&bebob->mutex);
		return;
	}
	amdtp_stream_update(stream);
}

static void
stream_destroy(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	stream_stop(bebob, stream);

	if (stream == &bebob->tx_stream)
		cmp_connection_destroy(&bebob->out_conn);
	else
		cmp_connection_destroy(&bebob->in_conn);
}

static int
get_roles(struct snd_bebob *bebob, enum cip_flags *sync_mode,
	  struct amdtp_stream **master, struct amdtp_stream **slave)
{
	/* currently this module doesn't support SYT-Match mode */
	*sync_mode = CIP_SYNC_TO_DEVICE;
	*master = &bebob->tx_stream;
	*slave = &bebob->rx_stream;

	return 0;
}

int snd_bebob_stream_init_duplex(struct snd_bebob *bebob)
{
	int err;

	err = stream_init(bebob, &bebob->tx_stream);
	if (err < 0)
		goto end;
	err = stream_init(bebob, &bebob->rx_stream);
end:
	return err;
}

int snd_bebob_stream_start_duplex(struct snd_bebob *bebob,
				  struct amdtp_stream *request,
				  unsigned int sampling_rate)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err, in_rate, out_rate;
	bool slave_flag;

	err = get_roles(bebob, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if ((request == slave) || amdtp_stream_running(slave))
		slave_flag = true;
	else
		slave_flag = false;

	/* change sampling rate if possible */
	err = avc_generic_get_sampling_rate(bebob->unit, &out_rate, 0, 0);
	if (err < 0)
		goto end;
	err = avc_generic_get_sampling_rate(bebob->unit, &in_rate, 1, 0);
	if (err < 0)
		goto end;
	if (in_rate != out_rate)
		goto end;

	if (sampling_rate == 0)
		sampling_rate = in_rate;
	if (sampling_rate != in_rate) {
		/* master is just for MIDI stream */
		if (amdtp_stream_running(master) &&
		    !amdtp_stream_pcm_running(master))
			stream_stop(bebob, master);

		/* slave is just for MIDI stream */
		if (amdtp_stream_running(slave) &&
		    !amdtp_stream_pcm_running(slave))
			stream_stop(bebob, slave);

		/* move to strem_start? */
		err = avc_generic_set_sampling_rate(bebob->unit,
						    sampling_rate, 1, 0);
		if (err < 0)
			goto end;
		err = avc_generic_set_sampling_rate(bebob->unit,
						    sampling_rate, 0, 0);
		if (err < 0)
			goto end;
		/* TODO: notify */
	}

	/* master should be always running */
	if (!amdtp_stream_running(master)) {
		amdtp_stream_set_sync(sync_mode, master, slave);
		err = stream_start(bebob, master, sampling_rate);
		if (err < 0)
			goto end;

		err = amdtp_stream_wait_run(master);
		if (err < 0)
			goto end;
	}

	/* start slave if needed */
	if (slave_flag && !amdtp_stream_running(slave)) {
		err = stream_start(bebob, slave, sampling_rate);
		if (err < 0)
			goto end;
		err = amdtp_stream_wait_run(slave);
	}

end:
	return err;
}

int snd_bebob_stream_stop_duplex(struct snd_bebob *bebob)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err;

	err = get_roles(bebob, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if (amdtp_stream_pcm_running(slave) ||
	    amdtp_stream_midi_running(slave))
		goto end;

	stream_stop(bebob, slave);

	if (!amdtp_stream_pcm_running(master) &&
	    !amdtp_stream_midi_running(master))
		stream_stop(bebob, master);
end:
	return err;
}

void snd_bebob_stream_update_duplex(struct snd_bebob *bebob)
{
	struct amdtp_stream *master, *slave;

	if (bebob->tx_stream.flags & CIP_SYNC_TO_DEVICE) {
		master = &bebob->tx_stream;
		slave = &bebob->rx_stream;
	} else {
		master = &bebob->rx_stream;
		slave = &bebob->tx_stream;
	}

	stream_update(bebob, master);
	stream_update(bebob, slave);
}

void snd_bebob_stream_destroy_duplex(struct snd_bebob *bebob)
{
	if (amdtp_stream_pcm_running(&bebob->tx_stream))
		amdtp_stream_pcm_abort(&bebob->tx_stream);
	if (amdtp_stream_pcm_running(&bebob->rx_stream))
		amdtp_stream_pcm_abort(&bebob->rx_stream);

	stream_destroy(bebob, &bebob->tx_stream);
	stream_destroy(bebob, &bebob->rx_stream);
}

int snd_bebob_get_formation_index(int sampling_rate)
{
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		if (snd_bebob_rate_table[i] == sampling_rate)
			return i;
	}
	return -1;
}

static void
set_stream_formation(u8 *buf, int len,
		     struct snd_bebob_stream_formation *formation)
{
	int channels, format;
	int e;

	for (e = 0; e < buf[4]; e += 1) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* PCM for IEC 60958-3 */
		case 0x00:
		/* PCM for IEC 61883-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* PCM for Multi bit linear audio */
		case 0x06:
		case 0x07:
			formation->pcm += channels;
			break;
		/* MIDI comformant (MMA/AMEI RP-027) */
		case 0x0d:
			formation->midi += channels;
			break;
		default:
			break;
		}
	}

	return;
}

static int
fill_stream_formations(struct snd_bebob *bebob,
		       int direction, unsigned short plugid)
{
	int freq_table[] = {
		[0x00] = 0,	/*  22050 */
		[0x01] = 1,	/*  24000 */
		[0x02] = 2,	/*  32000 */
		[0x03] = 3,	/*  44100 */
		[0x04] = 4,	/*  48000 */
		[0x05] = 6,	/*  96000 */
		[0x06] = 7,	/* 176400 */
		[0x07] = 8,	/* 192000 */
		[0x0a] = 5	/*  88200 */
	};

	u8 *buf;
	struct snd_bebob_stream_formation *formations;
	int index;
	int len;
	int e, i;
	int err;

	buf = kmalloc(FORMATION_MAXIMUM_LENGTH, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (direction > 0)
		formations = bebob->rx_stream_formations;
	else
		formations = bebob->tx_stream_formations;

	for (e = 0, index = 0; e < 9; e += 1) {
		len = FORMATION_MAXIMUM_LENGTH;
		/* get entry */
		memset(buf, 0, len);
		err = avc_bridgeco_get_plug_stream_formation_entry(bebob->unit, direction, plugid, e, buf, &len);
		if (err < 0)
			goto end;
		/* reach the end of entries */
		else if (buf[0] != 0x0c)
			break;
		/*
		 * this module can support a hierarchy combination that:
		 *  Root:	Audio and Music (0x90)
		 *  Level 1:	AM824 Compound  (0x40)
		 */
		else if ((buf[11] != 0x90) || (buf[12] != 0x40))
			break;
		/* check formation length */
		else if (len < 1)
			break;

		/* formation information includes own value for sampling rate. */
		if ((buf[13] > 0x07) && (buf[13] != 0x0a))
			break;
		for (i = 0; i < sizeof(freq_table); i += 1) {
			if (i == buf[13])
				index = freq_table[i];
		}

		/* parse and set stream formation */
		set_stream_formation(buf + 11, len - 11, &formations[index]);
	};

	err = 0;

end:
	kfree(buf);
	return err;
}

/* In this function, 2 means input and output */
int snd_bebob_stream_discover(struct snd_bebob *bebob)
{
	/* the number of plugs for input and output */
	unsigned short bus_plugs[2];
	unsigned short ext_plugs[2];
	int type, i;
	int err;

	err = avc_generic_get_plug_info(bebob->unit, bus_plugs, ext_plugs);
	if (err < 0)
		goto end;

	/*
	 * This module supports one PCR input plug and one PCR output plug
	 * then ignores the others.
	 */
	for (i = 0; i < 2; i += 1) {
		if (bus_plugs[i]  == 0) {
			err = -EIO;
			goto end;
		}

		err = avc_bridgeco_get_plug_type(bebob->unit, i, 0, &type);
		if (err < 0)
			goto end;
		else if (type != 0x00) {
			err = -EIO;
			goto end;
		}
	}

	/* store formations */
	for (i = 0; i < 2; i += 1) {
		err = fill_stream_formations(bebob, i, 0);
		if (err < 0)
			goto end;
	}

	/* TODO: count MIDI external plugs */
	bebob->midi_input_ports = 1;
	bebob->midi_output_ports = 1;

	err = 0;

end:
	return err;
}
