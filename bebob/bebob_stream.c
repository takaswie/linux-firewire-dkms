/*
 * bebob_stream.c - a part of driver for BeBoB based devices
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

/*
 * NOTE;
 * For BeBoB streams, Both of input and output CMP connection is important.
 *
 * [Communication with Windows driver] According to logs of IEEE1394 packets,
 * all models which use BeBoB chipset seem to make both of connections when
 * booting.
 *
 * [Actual behavior] In some devices, one CMP connection starts to
 * transmit/receive a corresponding stream. But in the others, both of CMP
 * connection needs to start transmitting stream. A example of the latter is
 * 'M-Audio Firewire 410'.
 */

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
get_formation_index(int rate)
{
        int i;

        for (i = 0; i < sizeof(snd_bebob_rate_table); i += 1) {
                if (snd_bebob_rate_table[i] == rate)
                        return i;
	}
	return -1;
}

int
snd_bebob_stream_get_rate(struct snd_bebob *bebob, int *curr_rate)
{
	int err, tx_rate, rx_rate;

	err = avc_generic_get_sig_fmt(bebob->unit, &tx_rate, 0, 0);
	if (err < 0)
		goto end;

	err = avc_generic_get_sig_fmt(bebob->unit, &rx_rate, 1, 0);
	if (err < 0)
		goto end;

	*curr_rate = rx_rate;
	if (rx_rate == tx_rate)
		goto end;

	/* synchronize receive stream rate to transmit stream rate */
	err = avc_generic_set_sig_fmt(bebob->unit, rx_rate, 0, 0);
end:
	return err;
}

int
snd_bebob_stream_set_rate(struct snd_bebob *bebob, int rate)
{
	int err;

	/* move to strem_start? */
	err = avc_generic_set_sig_fmt(bebob->unit, rate, 0, 0);
	if (err < 0)
		goto end;
	err = avc_generic_set_sig_fmt(bebob->unit, rate, 1, 0);
	if (err < 0)
		goto end;

	/* TODO: notify */
end:
	return err;
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

	err = avc_bridgeco_get_plug_channel_position(bebob->unit,
						     dir, 0, buf);
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
init_both_connections(struct snd_bebob *bebob)
{
	int err;

	err = cmp_connection_init(&bebob->in_conn,
				  bebob->unit, CMP_INPUT, 0);
	if (err < 0)
		goto end;

	err = cmp_connection_init(&bebob->out_conn,
				  bebob->unit, CMP_OUTPUT, 0);
	if (err < 0)
		cmp_connection_destroy(&bebob->in_conn);
end:
	return err;
}

static int
make_both_connections(struct snd_bebob *bebob, int rate)
{
	int index, pcm_channels, midi_channels, err;

	/* confirm params for both streams */
	index = get_formation_index(rate);
	pcm_channels = bebob->tx_stream_formations[index].pcm;
	midi_channels = bebob->tx_stream_formations[index].midi;
	amdtp_stream_set_params(&bebob->tx_stream,
				rate, pcm_channels, midi_channels);
	pcm_channels = bebob->rx_stream_formations[index].pcm;
	midi_channels = bebob->rx_stream_formations[index].midi;
	amdtp_stream_set_params(&bebob->rx_stream,
				rate, pcm_channels, midi_channels);

	/* establish connections for both streams */
	err = cmp_connection_establish(&bebob->out_conn,
				amdtp_stream_get_max_payload(&bebob->tx_stream));
	if (err < 0)
		goto end;
	err = cmp_connection_establish(&bebob->in_conn,
				amdtp_stream_get_max_payload(&bebob->rx_stream));
	if (err < 0) {
		cmp_connection_break(&bebob->out_conn);
		goto end;
	}

end:
	return err;
}

static void
break_both_connections(struct snd_bebob *bebob)
{
	cmp_connection_break(&bebob->in_conn);
	cmp_connection_break(&bebob->out_conn);
	return;
}

static void
destroy_both_connections(struct snd_bebob *bebob)
{
	break_both_connections(bebob);

	cmp_connection_destroy(&bebob->in_conn);
	cmp_connection_destroy(&bebob->out_conn);
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

static int
start_stream(struct snd_bebob *bebob, struct amdtp_stream *stream,
	     unsigned int rate)
{
	struct cmp_connection *conn;
	int err = 0;

	/* already running */
	if (amdtp_stream_running(stream))
		goto end;

	if (stream == &bebob->rx_stream)
		conn = &bebob->in_conn;
	else
		conn = &bebob->out_conn;

	/* channel mapping */
	if (bebob->spec->map != NULL) {
		err = bebob->spec->map(bebob, stream);
		if (err < 0)
			goto end;
	}

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
	if (err < 0)
		goto end;
end:
	return err;
}

int snd_bebob_stream_init_duplex(struct snd_bebob *bebob)
{
	int err;

	err = init_both_connections(bebob);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(&bebob->tx_stream, bebob->unit,
				AMDTP_IN_STREAM, CIP_BLOCKING);
	if (err < 0) {
		destroy_both_connections(bebob);
		goto end;
	}

	err = amdtp_stream_init(&bebob->rx_stream, bebob->unit,
				AMDTP_OUT_STREAM, CIP_BLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(&bebob->tx_stream);
		destroy_both_connections(bebob);
	}
end:
	return err;
}

int snd_bebob_stream_start_duplex(struct snd_bebob *bebob,
				  struct amdtp_stream *request,
				  unsigned int rate)
{
	struct snd_bebob_freq_spec *freq = bebob->spec->freq;
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err, curr_rate;
	bool slave_flag;

	mutex_lock(&bebob->mutex);

	err = get_roles(bebob, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if ((request == slave) || amdtp_stream_running(slave))
		slave_flag = true;
	else
		slave_flag = false;

	/* get current rate */
	err = freq->get(bebob, &curr_rate);
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
		    !amdtp_stream_pcm_running(master)) {
			amdtp_stream_stop(master);
			break_both_connections(bebob);
		}
	}

	/* master should be always running */
	if (!amdtp_stream_running(master)) {
		amdtp_stream_set_sync(sync_mode, master, slave);

		/*
		 * NOTE:
		 * If establishing connections at first, Yamaha GO46 (and maybe
		 * TerraTek X24) don't generate sound.
		 */
		err = freq->set(bebob, rate);
		if (err < 0)
			goto end;

		err = make_both_connections(bebob, rate);
		if (err < 0)
			goto end;

		err = start_stream(bebob, master, rate);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to run AMDTP master stream:%d\n", err);
			break_both_connections(bebob);
			goto end;
		}

		/*
		 * NOTE:
		 * The firmware customized by M-Audio uses this cue to start
		 * transmit stream. This is not in specification.
		 */
		if (bebob->maudio_special_quirk) {
			err = freq->set(bebob, rate);
			if (err < 0) {
				amdtp_stream_stop(master);
				break_both_connections(bebob);
				goto end;
			}
		}

		/* wait first callback */
		if (!amdtp_stream_wait_callback(master)) {
			amdtp_stream_stop(master);
			break_both_connections(bebob);
			err = -ETIMEDOUT;
			goto end;
		}
	}

	/* start slave if needed */
	if (slave_flag && !amdtp_stream_running(slave)) {
		err = start_stream(bebob, slave, rate);
		if (err < 0) {
			dev_err(&bebob->unit->device,
				"fail to run AMDTP slave stream:%d\n", err);
			amdtp_stream_stop(master);
			break_both_connections(bebob);
			goto end;
		}

		/* wait first callback */
		if (!amdtp_stream_wait_callback(slave)) {
			amdtp_stream_stop(slave);
			amdtp_stream_stop(master);
			break_both_connections(bebob);
			err = -ETIMEDOUT;
			goto end;
		}
	}
end:
	mutex_unlock(&bebob->mutex);
	return err;
}

int snd_bebob_stream_stop_duplex(struct snd_bebob *bebob)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err;

	mutex_lock(&bebob->mutex);

	err = get_roles(bebob, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if (amdtp_stream_pcm_running(slave) ||
	    amdtp_stream_midi_running(slave))
		goto end;

	amdtp_stream_stop(slave);

	if (amdtp_stream_pcm_running(master) ||
	    amdtp_stream_midi_running(master))
		goto end;

	amdtp_stream_stop(master);
	break_both_connections(bebob);
end:
	mutex_unlock(&bebob->mutex);
	return err;
}

void snd_bebob_stream_update_duplex(struct snd_bebob *bebob)
{
	if ((cmp_connection_update(&bebob->in_conn) > 0) ||
	    (cmp_connection_update(&bebob->out_conn) > 0)) {
		mutex_lock(&bebob->mutex);
		amdtp_stream_pcm_abort(&bebob->rx_stream);
		amdtp_stream_pcm_abort(&bebob->tx_stream);
		break_both_connections(bebob);
		mutex_unlock(&bebob->mutex);
	}

	amdtp_stream_update(&bebob->rx_stream);
	amdtp_stream_update(&bebob->tx_stream);
}

void snd_bebob_stream_destroy_duplex(struct snd_bebob *bebob)
{
	mutex_lock(&bebob->mutex);

	if (amdtp_stream_pcm_running(&bebob->rx_stream))
		amdtp_stream_pcm_abort(&bebob->rx_stream);
	if (amdtp_stream_pcm_running(&bebob->tx_stream))
		amdtp_stream_pcm_abort(&bebob->tx_stream);

	amdtp_stream_stop(&bebob->rx_stream);
	amdtp_stream_stop(&bebob->tx_stream);
	destroy_both_connections(bebob);

	mutex_unlock(&bebob->mutex);
}

int snd_bebob_get_formation_index(int rate)
{
	int i;

	for (i = 0; i < SND_BEBOB_STREAM_FORMATION_ENTRIES; i += 1) {
		if (snd_bebob_rate_table[i] == rate)
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
