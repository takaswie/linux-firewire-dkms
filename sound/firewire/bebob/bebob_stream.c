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
 * all models seem to make both of connections when booting.
 *
 * [Actual behavior] In some devices, one CMP connection starts to
 * transmit/receive a corresponding stream. But in the others, both of CMP
 * connection needs to start transmitting stream. An example of the latter is
 * 'M-Audio Firewire 410'.
 */

/* 128 is an arbitrary length but it seems to be enough */
#define FORMAT_MAXIMUM_LENGTH 128

const unsigned int snd_bebob_rate_table[SND_BEBOB_STRM_FMT_ENTRIES] = {
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

static unsigned int
get_formation_index(unsigned int rate)
{
	unsigned int i;

	for (i = 0; i < sizeof(snd_bebob_rate_table); i++) {
		if (snd_bebob_rate_table[i] == rate)
			return i;
	}
	return -1;
}

int
snd_bebob_stream_get_rate(struct snd_bebob *bebob, unsigned int *curr_rate)
{
	unsigned int tx_rate, rx_rate;
	int err;

	err = snd_bebob_get_rate(bebob, &tx_rate, AVC_GENERAL_PLUG_DIR_OUT);
	if (err < 0)
		goto end;

	err = snd_bebob_get_rate(bebob, &rx_rate, AVC_GENERAL_PLUG_DIR_IN);
	if (err < 0)
		goto end;

	*curr_rate = rx_rate;
	if (rx_rate == tx_rate)
		goto end;

	/* synchronize receive stream rate to transmit stream rate */
	err = snd_bebob_set_rate(bebob, rx_rate, AVC_GENERAL_PLUG_DIR_IN);
end:
	return err;
}

int
snd_bebob_stream_set_rate(struct snd_bebob *bebob, unsigned int rate)
{
	int err;

	/* TODO: move to strem_start? */
	err = snd_bebob_set_rate(bebob, rate, AVC_GENERAL_PLUG_DIR_OUT);
	if (err < 0)
		goto end;

	err = snd_bebob_set_rate(bebob, rate, AVC_GENERAL_PLUG_DIR_IN);
end:
	return err;
}

int
snd_bebob_stream_check_internal_clock(struct snd_bebob *bebob, bool *internal)
{
	struct snd_bebob_clock_spec *clk = bebob->spec->clock;
	u8 addr[6], input[7];
	unsigned int id;
	int err = 0;

	*internal = false;

	/* 1.The device has its own operation to switch source of clock */
	if (clk->get_src) {
		err = clk->get_src(bebob, &id);
		if (err < 0)
			goto end;
		if (strncmp(clk->labels[id], SND_BEBOB_CLOCK_INTERNAL,
			    strlen(SND_BEBOB_CLOCK_INTERNAL)) == 0)
			*internal = true;
		goto end;
	}

	/*
	 * 2.The device don't support for switching source of clock
	 *   then assumed to use internal clock always
	 */
	if (bebob->sync_input_plug < 0) {
		*internal = true;
		goto end;
	}

	/*
	 * 3.The device supports to switch source of clock by an usual way.
	 *   Let's check input for 'Music Sub Unit Sync Input' plug.
	 */
	avc_bridgeco_fill_subunit_addr(addr, 0x60, SND_BEBOB_PLUG_DIR_IN,
				       bebob->sync_input_plug);
	err = avc_bridgeco_get_plug_input(bebob->unit, addr, input);
	if (err < 0)
		goto end;

	/*
	 * If source of clock is internal CSR, Music Sub Unit Sync Input is
	 * a destination of Music Sub Unit Sync Output.
	 */
	*internal = ((input[0] == SND_BEBOB_PLUG_DIR_OUT) &&
		     (input[1] == SND_BEBOB_PLUG_MODE_SUBUNIT) &&
		     (input[2] == 0x0c) &&
		     (input[3] == 0x00));
end:
	return err;
}


int snd_bebob_stream_map(struct snd_bebob *bebob,
			 struct amdtp_stream *stream)
{
	unsigned int cl, ch, clusters, channels, pos, pcm, midi;
	u8 *buf, ctype;
	u8 addr[6];
	enum snd_bebob_plug_dir dir;
	int err;

	/*
	 * The length of return value of this command cannot be assumed. Here
	 * keep the maximum length of AV/C command which defined specification.
	 */
	buf = kzalloc(256, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto end;
	}

	if (stream == &bebob->tx_stream)
		dir = SND_BEBOB_PLUG_DIR_OUT;
	else
		dir = SND_BEBOB_PLUG_DIR_IN;

	avc_bridgeco_fill_unit_addr(addr, dir, SND_BEBOB_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_ch_pos(bebob->unit, addr, buf, 256);
	if (err < 0)
		goto end;

	clusters = *buf;
	buf++;
	pcm = 0;
	midi = 0;
	for (cl = 0; cl < clusters; cl++) {
		avc_bridgeco_fill_unit_addr(addr, dir,
					    SND_BEBOB_PLUG_UNIT_ISOC, 0);
		err = avc_bridgeco_get_plug_cluster_type(bebob->unit, addr,
							 cl, &ctype);
		if (err < 0)
			goto end;

		channels = *buf;
		buf++;
		for (ch = 0; ch < channels; ch++) {
			pos = *buf - 1;
			if (ctype != 0x0a)
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
check_connection_used_by_others(struct snd_bebob *bebob,
				struct amdtp_stream *s, bool *used)
{
	struct cmp_connection *conn;
	int err;

	if (s == &bebob->tx_stream)
		conn = &bebob->out_conn;
	else
		conn = &bebob->in_conn;

	err = cmp_connection_check_used(conn, used);
	if (err >= 0)
		*used = (*used && !amdtp_stream_running(s));

	return err;
}

static int
make_both_connections(struct snd_bebob *bebob, unsigned int rate)
{
	int index, pcm_channels, midi_channels, err;

	/* confirm params for both streams */
	index = get_formation_index(rate);
	pcm_channels = bebob->tx_stream_formations[index].pcm;
	midi_channels = bebob->tx_stream_formations[index].midi;
	amdtp_stream_set_parameters(&bebob->tx_stream,
				    rate, pcm_channels, midi_channels * 8);
	pcm_channels = bebob->rx_stream_formations[index].pcm;
	midi_channels = bebob->rx_stream_formations[index].midi;
	amdtp_stream_set_parameters(&bebob->rx_stream,
				    rate, pcm_channels, midi_channels * 8);

	/* establish connections for both streams */
	err = cmp_connection_establish(&bebob->out_conn,
			amdtp_stream_get_max_payload(&bebob->tx_stream));
	if (err < 0)
		goto end;
	err = cmp_connection_establish(&bebob->in_conn,
			amdtp_stream_get_max_payload(&bebob->rx_stream));
	if (err < 0)
		cmp_connection_break(&bebob->out_conn);
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
	if (!bebob->maudio_special_quirk) {
		err = snd_bebob_stream_map(bebob, stream);
		if (err < 0)
			goto end;
	}

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
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
	struct snd_bebob_clock_spec *spec = bebob->spec->clock;
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	unsigned int curr_rate;
	bool slave_flag, used;
	int err;

	mutex_lock(&bebob->mutex);

	err = get_roles(bebob, &sync_mode, &master, &slave);
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
	err = check_connection_used_by_others(bebob, master, &used);
	if (err < 0)
		goto end;
	if (used) {
		dev_err(&bebob->unit->device,
			"connections established by others: %d\n",
			used);
		err = -EBUSY;
		goto end;
	}

	/* get current rate */
	err = spec->get_freq(bebob, &curr_rate);
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
		err = spec->set_freq(bebob, rate);
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
			err = spec->set_freq(bebob, rate);
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

static void
set_stream_formation(u8 *buf, unsigned int len,
		     struct snd_bebob_stream_formation *formation)
{
	unsigned int e, channels, format;

	for (e = 0; e < buf[4]; e++) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* PCM for IEC 60958-3 */
		case 0x00:
		/* PCM for IEC 61937-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* PCM for Multi bit linear audio */
		case 0x06:	/* raw */
		case 0x07:	/* DVD-Audio */
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
fill_stream_formations(struct snd_bebob *bebob, enum snd_bebob_plug_dir dir,
		       unsigned short pid)
{
	static const unsigned int freq_table[] = {
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
	unsigned int i, index, len, eid;
	u8 addr[6];
	int err;

	buf = kmalloc(FORMAT_MAXIMUM_LENGTH, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (dir == SND_BEBOB_PLUG_DIR_IN)
		formations = bebob->rx_stream_formations;
	else
		formations = bebob->tx_stream_formations;

	for (eid = 0, index = 0; eid < SND_BEBOB_STRM_FMT_ENTRIES; eid++) {
		len = FORMAT_MAXIMUM_LENGTH;

		memset(buf, 0, len);
		avc_bridgeco_fill_unit_addr(addr, dir,
					    SND_BEBOB_PLUG_UNIT_ISOC, pid);
		err = avc_bridgeco_get_plug_strm_fmt(bebob->unit, addr,
						     eid, buf, &len);
		if (err < 0)
			goto end;
		else if (len < 3)
			break;
		/*
		 * this module can support a hierarchy combination that:
		 *  Root:	Audio and Music (0x90)
		 *  Level 1:	AM824 Compound  (0x40)
		 */
		else if ((buf[0] != 0x90) || (buf[1] != 0x40))
			break;

		/* check sampling rate */
		index = -1;
		for (i = 0; i < sizeof(freq_table); i++) {
			if (i == buf[2])
				index = freq_table[i];
		}
		if (index < 0)
			break;

		/* parse and set stream formation */
		set_stream_formation(buf, len, &formations[index]);
	}
end:
	kfree(buf);
	return err;
}

static int
seek_msu_sync_input_plug(struct snd_bebob *bebob)
{
	u8 plugs[AVC_PLUG_INFO_BUF_COUNT], addr[6];
	unsigned int i, type;
	int err;

	/* get information about Music Sub Unit */
	err = avc_general_get_plug_info(bebob->unit, 0x0c, 0x00, 0x00, plugs);
	if (err < 0)
		goto end;

	/* seek destination plugs for 'MSU sync input' */
	bebob->sync_input_plug = -1;
	for (i = 0; i < plugs[0]; i++) {
		avc_bridgeco_fill_subunit_addr(addr, 0x60,
					       SND_BEBOB_PLUG_DIR_IN, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0)
			goto end;

		if (type == SND_BEBOB_PLUG_TYPE_SYNC)
			bebob->sync_input_plug = i;
	}
end:
	return err;
}

/* In this function, 2 means input and output */
int snd_bebob_stream_discover(struct snd_bebob *bebob)
{
	u8 plugs[AVC_PLUG_INFO_BUF_COUNT], addr[6];
	enum snd_bebob_plug_type type;
	unsigned int i;
	int err;

	/* the number of plugs for isoc in/out, ext in/out  */
	err = avc_general_get_plug_info(bebob->unit, 0x1f, 0x07, 0x00, plugs);
	if (err < 0)
		goto end;

	/*
	 * This module supports one ISOC input plug and one ISOC output plug
	 * then ignores the others.
	 */
	if (plugs[0] == 0) {
		err = -EIO;
		goto end;
	}
	avc_bridgeco_fill_unit_addr(addr, SND_BEBOB_PLUG_DIR_IN,
				    SND_BEBOB_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
	if (err < 0)
		goto end;
	else if (type != SND_BEBOB_PLUG_TYPE_ISOC) {
		err = -EIO;
		goto end;
	}

	if (plugs[1] == 0) {
		err = -EIO;
		goto end;
	}
	avc_bridgeco_fill_unit_addr(addr, SND_BEBOB_PLUG_DIR_OUT,
				    SND_BEBOB_PLUG_UNIT_ISOC, 0);
	err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
	if (err < 0)
		goto end;
	else if (type != SND_BEBOB_PLUG_TYPE_ISOC) {
		err = -EIO;
		goto end;
	}

	/* store formations */
	for (i = 0; i < 2; i++) {
		err = fill_stream_formations(bebob, i, 0);
		if (err < 0)
			goto end;
	}

	/* count external input plugs for MIDI */
	bebob->midi_input_ports = 0;
	for (i = 0; i < plugs[2]; i++) {
		avc_bridgeco_fill_unit_addr(addr, SND_BEBOB_PLUG_DIR_IN,
					    SND_BEBOB_PLUG_UNIT_EXT, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0)
			goto end;
		else if (type == SND_BEBOB_PLUG_TYPE_MIDI)
			bebob->midi_input_ports++;
	}

	/* count external output plugs for MIDI */
	bebob->midi_output_ports = 0;
	for (i = 0; i < plugs[3]; i++) {
		avc_bridgeco_fill_unit_addr(addr, SND_BEBOB_PLUG_DIR_OUT,
					    SND_BEBOB_PLUG_UNIT_EXT, i);
		err = avc_bridgeco_get_plug_type(bebob->unit, addr, &type);
		if (err < 0)
			goto end;
		else if (type == SND_BEBOB_PLUG_TYPE_MIDI)
			bebob->midi_output_ports++;
	}

	/* for check source of clock later */
	if (!bebob->spec->clock->get_src)
		err = seek_msu_sync_input_plug(bebob);
end:
	return err;
}

void snd_bebob_stream_lock_changed(struct snd_bebob *bebob)
{
	bebob->dev_lock_changed = true;
	wake_up(&bebob->hwdep_wait);
}

int snd_bebob_stream_lock_try(struct snd_bebob *bebob)
{
	int err;

	spin_lock_irq(&bebob->lock);

	/* user land lock this */
	if (bebob->dev_lock_count < 0) {
		err = -EBUSY;
		goto end;
	}

	/* this is the first time */
	if (bebob->dev_lock_count++ == 0)
		snd_bebob_stream_lock_changed(bebob);
	err = 0;
end:
	spin_unlock_irq(&bebob->lock);
	return err;
}

void snd_bebob_stream_lock_release(struct snd_bebob *bebob)
{
	spin_lock_irq(&bebob->lock);

	if (WARN_ON(bebob->dev_lock_count <= 0))
		goto end;
	if (--bebob->dev_lock_count == 0)
		snd_bebob_stream_lock_changed(bebob);
end:
	spin_unlock_irq(&bebob->lock);
}
