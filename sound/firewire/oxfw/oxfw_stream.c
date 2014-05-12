/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

#define CALLBACK_TIMEOUT	200

/*
 * According to their datasheet:
 *  OXFW970: 32.0/44.1/48.0/96.0 Khz, 8 audio channels I/O
 *  OXFW971: 32.0/44.1/48.0/88.2/96.0/192.0 kHz, 16 audio channels I/O, MIDI I/O
 */
static const unsigned int oxfw_rate_table[] = {
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 192000,
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
	[5] = 0x07,
};

int snd_oxfw_stream_get_rate(struct snd_oxfw *oxfw, unsigned int *rate)
{
	unsigned int tx_rate, rx_rate;
	int err;

	err = avc_general_get_sig_fmt(oxfw->unit, &rx_rate,
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0)
		goto end;
	*rate = rx_rate;

	if (oxfw->has_output) {
		err = avc_general_get_sig_fmt(oxfw->unit, &tx_rate,
						AVC_GENERAL_PLUG_DIR_OUT, 0);
		if ((err < 0) || (rx_rate == tx_rate))
			goto end;

		/* synchronize transmit stream rate to receive stream rate */
		err = avc_general_set_sig_fmt(oxfw->unit, rx_rate,
					AVC_GENERAL_PLUG_DIR_OUT, 0);
	}
end:
	return err;
}

int snd_oxfw_stream_set_rate(struct snd_oxfw *oxfw, unsigned int rate)
{
	int err;

	err = avc_general_set_sig_fmt(oxfw->unit, rate,
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if (err < 0)
		goto end;

	if (oxfw->has_output)
		err = avc_general_set_sig_fmt(oxfw->unit, rate,
					      AVC_GENERAL_PLUG_DIR_OUT, 0);
end:
	return err;
}

static void stop_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream)
{
	amdtp_stream_pcm_abort(stream);
	amdtp_stream_stop(stream);

	if (stream == &oxfw->tx_stream)
		cmp_connection_break(&oxfw->out_conn);
	else
		cmp_connection_break(&oxfw->in_conn);
}

static int start_stream(struct snd_oxfw *oxfw, struct amdtp_stream *stream,
			unsigned int rate, unsigned int pcm_channels)
{
	struct snd_oxfw_stream_formation *formations;
	unsigned int i, midi_ports;
	struct cmp_connection *conn;
	int err;

	if (stream == &oxfw->rx_stream) {
		formations = oxfw->rx_stream_formations;
		conn = &oxfw->in_conn;
	} else {
		formations = oxfw->tx_stream_formations;
		conn = &oxfw->out_conn;
	}

	/* Get stream formation */
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (formations[i].rate != rate)
			continue;
		if ((pcm_channels == 0) ||
		    (formations[i].pcm == pcm_channels))
			break;
	}
	if (i == SND_OXFW_STREAM_FORMAT_ENTRIES) {
		err = -EINVAL;
		goto end;
	}

	pcm_channels = formations[i].pcm;
	midi_ports = formations[i].midi;

	/* The stream should have one pcm channels at least */
	if (pcm_channels == 0) {
		err = -EINVAL;
		goto end;
	}
	amdtp_stream_set_parameters(stream, rate, pcm_channels, midi_ports);

	/* Establish connection */
	err = cmp_connection_establish(conn,
				       amdtp_stream_get_max_payload(stream));
	if (err < 0)
		goto end;

	/* Start stream */
	err = amdtp_stream_start(stream,
				 conn->resources.channel,
				 conn->speed);
	if (err < 0) {
		cmp_connection_break(conn);
		goto end;
	}

	/* Wait first callback */
	err = amdtp_stream_wait_callback(stream, CALLBACK_TIMEOUT);
	if (err < 0)
		stop_stream(oxfw, stream);
end:
	return err;
}

static int check_connection_used_by_others(struct snd_oxfw *oxfw,
					   struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	bool used;
	int err;

	if (stream == &oxfw->tx_stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	err = cmp_connection_check_used(conn, &used);
	if ((err >= 0) && used && !amdtp_stream_running(stream)) {
		dev_err(&oxfw->unit->device,
			"Connection established by others: %cPCR[%d]\n",
			(conn->direction == CMP_OUTPUT) ? 'o' : 'i',
			conn->pcr_index);
		err = -EBUSY;
	}

	return err;
}

int snd_oxfw_stream_init_simplex(struct snd_oxfw *oxfw,
				 struct amdtp_stream *stream)
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

	mutex_lock(&oxfw->mutex);

	err = cmp_connection_init(conn, oxfw->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, oxfw->unit, s_dir, CIP_NONBLOCKING);
	if (err < 0) {
		amdtp_stream_destroy(stream);
		cmp_connection_destroy(conn);
		goto end;
	}

	/* OXFW starts to transmit packets with non-zero dbc. */
	if (stream == &oxfw->tx_stream)
		oxfw->tx_stream.flags |= CIP_SKIP_INIT_DBC_CHECK;
end:
	mutex_unlock(&oxfw->mutex);
	return err;
}

int snd_oxfw_stream_start_simplex(struct snd_oxfw *oxfw,
				  struct amdtp_stream *stream,
				  unsigned int rate, unsigned int pcm_channels)
{
	struct amdtp_stream *opposite;
	unsigned int curr_rate;
	atomic_t *substreams, *opposite_substreams;
	int err = 0;

	if (stream == &oxfw->tx_stream) {
		substreams = &oxfw->capture_substreams;
		opposite = &oxfw->rx_stream;
		opposite_substreams = &oxfw->playback_substreams;
	} else {
		substreams = &oxfw->playback_substreams;
		opposite_substreams = &oxfw->capture_substreams;

		if (oxfw->has_output)
			opposite = &oxfw->rx_stream;
		else
			opposite = NULL;
	}

	if (atomic_read(substreams) == 0)
		goto end;

	mutex_lock(&oxfw->mutex);

	/*
	 * Considering JACK/FFADO streaming:
	 * TODO: This can be removed hwdep functionality becomes popular.
	 */
	err = check_connection_used_by_others(oxfw, stream);
	if (err < 0)
		goto end;

	/* packet queueing error */
	if (amdtp_streaming_error(stream))
		stop_stream(oxfw, stream);

	/* stop streams if rate is different */
	err = snd_oxfw_stream_get_rate(oxfw, &curr_rate);
	if (err < 0) {
		dev_err(&oxfw->unit->device,
			"fail to get sampling rate: %d\n", err);
		goto end;
	}
	if (rate == 0)
		rate = curr_rate;
	if (curr_rate != rate) {
		/* Stop streams safely. */
		if (opposite != NULL) {
			err = check_connection_used_by_others(oxfw, opposite);
			if (err < 0)
				goto end;
			stop_stream(oxfw, opposite);
		}
		stop_stream(oxfw, stream);

		err = snd_oxfw_stream_set_rate(oxfw, rate);
		if (err < 0) {
			dev_err(&oxfw->unit->device,
				"fail to set sampling rate: %d\n", err);
			goto end;
		}

		/* Start opposite stream if needed. */
		if (opposite && !amdtp_stream_running(opposite) &&
		    (atomic_read(opposite_substreams) > 0)) {
			err = start_stream(oxfw, opposite, rate, 0);
			if (err < 0) {
				dev_err(&oxfw->unit->device,
					"fail to restart stream: %d\n", err);
				goto end;
			}
		}
	}

	if (atomic_read(substreams) && !amdtp_stream_running(stream)) {
		err = start_stream(oxfw, stream, rate, pcm_channels);
		if (err < 0)
			dev_err(&oxfw->unit->device,
				"fail to start stream: %d\n", err);
	}
end:
	mutex_unlock(&oxfw->mutex);
	return err;
}

void snd_oxfw_stream_stop_simplex(struct snd_oxfw *oxfw,
				  struct amdtp_stream *stream)
{
	if (((stream == &oxfw->tx_stream) &&
	     (atomic_read(&oxfw->capture_substreams) > 0)) ||
	    ((stream == &oxfw->rx_stream) &&
	     (atomic_read(&oxfw->playback_substreams) > 0)))
		return;

	mutex_lock(&oxfw->mutex);
	stop_stream(oxfw, stream);
	mutex_unlock(&oxfw->mutex);
}

void snd_oxfw_stream_destroy_simplex(struct snd_oxfw *oxfw,
				     struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (stream == &oxfw->tx_stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	mutex_lock(&oxfw->mutex);
	stop_stream(oxfw, stream);
	amdtp_stream_destroy(stream);
	cmp_connection_destroy(conn);
	mutex_unlock(&oxfw->mutex);
}

void snd_oxfw_stream_update_simplex(struct snd_oxfw *oxfw,
				    struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (stream == &oxfw->tx_stream)
		conn = &oxfw->out_conn;
	else
		conn = &oxfw->in_conn;

	if (cmp_connection_update(conn) < 0) {
		mutex_lock(&oxfw->mutex);
		stop_stream(oxfw, stream);
		mutex_unlock(&oxfw->mutex);
	} else {
		amdtp_stream_update(stream);
	}
}

/*
 * See Table 6.16 - AM824 Stream Format
 *     Figure 6.19 - format_information field for AM824 Compound
 * in AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA)
 * Also 'Clause 12 AM824 sequence adaption layers' in IEC 61883-6:2005
 */
static int parse_stream_formation(u8 *buf, unsigned int len,
				  struct snd_oxfw_stream_formation *formation)
{
	unsigned int i, e, channels, format;

	if (len < 3)
		return -EINVAL;

	/*
	 * this module can support a hierarchy combination that:
	 *  Root:	Audio and Music (0x90)
	 *  Level 1:	AM824 Compound  (0x40)
	 */
	if ((buf[0] != 0x90) || (buf[1] != 0x40))
		return -ENOSYS;

	/* check the sampling rate */
	for (i = 0; i < ARRAY_SIZE(avc_stream_rate_table); i++) {
		if (buf[2] == avc_stream_rate_table[i])
			break;
	}
	if (i == ARRAY_SIZE(avc_stream_rate_table))
		return -ENOSYS;

	memset(formation, 0, sizeof(struct snd_oxfw_stream_formation));
	formation->rate = oxfw_rate_table[i];

	for (e = 0; e < buf[4]; e++) {
		channels = buf[5 + e * 2];
		format = buf[6 + e * 2];

		switch (format) {
		/* IEC 60958-3, currently handle as MBLA */
		case 0x00:
		/* Multi Bit Linear Audio (Raw) */
		case 0x06:
			formation->pcm += channels;
			break;
		/* MIDI Conformant */
		case 0x0d:
			formation->midi += channels;
			break;
		/* IEC 61937-3 to 7 */
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		/* Multi Bit Linear Audio */
		case 0x07:	/* DVD-Audio */
		case 0x0c:	/* High Precision */
		/* One Bit Audio */
		case 0x08:	/* (Plain) Raw */
		case 0x09:	/* (Plain) SACD */
		case 0x0a:	/* (Encoded) Raw */
		case 0x0b:	/* (Encoded) SACD */
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
			return -ENOSYS;	/* not supported */
		}
	}

	if (formation->pcm  > AMDTP_MAX_CHANNELS_FOR_PCM ||
	    formation->midi > AMDTP_MAX_CHANNELS_FOR_MIDI)
		return -ENOSYS;

	return 0;
}

static int
assume_stream_formations(struct snd_oxfw *oxfw, enum avc_general_plug_dir dir,
			 unsigned int pid, u8 *buf, unsigned int *len,
			 struct snd_oxfw_stream_formation *formations)
{
	unsigned int rate, pcm_channels, midi_channels, i, eid;
	int err;

	/* get formation at current sampling rate */
	err = avc_stream_get_format_single(oxfw->unit, dir, pid, buf, len);
	if (err < 0) {
		dev_err(&oxfw->unit->device,
		"fail to get current stream format for isoc %s plug %d:%d\n",
			(dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" : "out",
			pid, err);
		goto end;
	}

	/* parse and set stream formation */
	eid = 0;
	err = parse_stream_formation(buf, *len, &formations[eid]);
	if (err < 0)
		goto end;

	rate = formations[eid].rate;
	pcm_channels = formations[eid].pcm;
	midi_channels = formations[eid].midi;

	/* apply the formation for each available sampling rate */
	for (i = 0; i < ARRAY_SIZE(oxfw_rate_table); i++) {
		if (rate == oxfw_rate_table[i])
			continue;

		err = avc_general_inquiry_sig_fmt(oxfw->unit,
						  oxfw_rate_table[i],
						  dir, pid);
		if (err < 0)
			continue;

		eid++;
		formations[eid].rate = oxfw_rate_table[i];
		formations[eid].pcm = pcm_channels;
		formations[eid].midi = midi_channels;
	}

	err = 0;
end:
	return err;
}

static int fill_stream_formations(struct snd_oxfw *oxfw,
				  enum avc_general_plug_dir dir,
				  unsigned short pid)
{
	u8 *buf;
	struct snd_oxfw_stream_formation *formations, tmp;
	unsigned int i, len, eid = 0;
	int err;

	buf = kmalloc(AVC_GENERIC_FRAME_MAXIMUM_BYTES, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (dir == AVC_GENERAL_PLUG_DIR_OUT)
		formations = oxfw->tx_stream_formations;
	else
		formations = oxfw->rx_stream_formations;

	/* get first entry */
	len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
	err = avc_stream_get_format_list(oxfw->unit, dir, 0, buf, &len, 0);
	if (err == -ENOSYS) {
		/* LIST subfunction is not implemented */
		len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
		err = assume_stream_formations(oxfw, dir, pid, buf, &len,
					       formations);
		goto end;
	} else if (err < 0) {
		dev_err(&oxfw->unit->device,
			"fail to get stream format %d for isoc %s plug %d:%d\n",
			eid, (dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" : "out",
			pid, err);
		goto end;
	}

	/* LIST subfunction is implemented */
	while (eid < SND_OXFW_STREAM_FORMAT_ENTRIES) {
		/* parse and set stream formation */
		err = parse_stream_formation(buf, len, &formations[eid]);
		if (err < 0)
			break;

		/* get next entry */
		len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
		err = avc_stream_get_format_list(oxfw->unit, dir, 0,
						 buf, &len, ++eid);
		/* No entries remained. */
		if (err == -EINVAL) {
			err = 0;
			break;
		} else if (err < 0) {
			dev_err(&oxfw->unit->device,
			"fail to get stream format %d for isoc %s plug %d:%d\n",
				eid, (dir == AVC_GENERAL_PLUG_DIR_IN) ? "in" :
									"out",
				pid, err);
			break;
		}
	}

	if (eid >= SND_OXFW_STREAM_FORMAT_ENTRIES)
		goto end;

	/* Griffin FireWave have another entry in current formation. */
	len = AVC_GENERIC_FRAME_MAXIMUM_BYTES;
	err = avc_stream_get_format_single(oxfw->unit, dir, 0, buf, &len);
	if (err < 0)
		goto end;
	err = parse_stream_formation(buf, len, &tmp);
	if (err < 0)
		goto end;
	/* Store this if no duplicates. */
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (memcmp(&formations[i], &tmp, sizeof(tmp)) == 0)
			break;
	}
	if (i == SND_OXFW_STREAM_FORMAT_ENTRIES)
		formations[eid] = tmp;
end:
	kfree(buf);
	return err;
}

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw)
{
	u8 plugs[AVC_PLUG_INFO_BUF_BYTES];
	unsigned int i;
	int err;

	/* the number of plugs for isoc in/out, ext in/out  */
	err = avc_general_get_plug_info(oxfw->unit, 0x1f, 0x07, 0x00, plugs);
	if (err < 0) {
		dev_err(&oxfw->unit->device,
		"fail to get info for isoc/external in/out plugs: %d\n",
			err);
		goto end;
	} else if ((plugs[0] == 0) && (plugs[1] == 0)) {
		err = -ENOSYS;
		goto end;
	}

	/* use oPCR[0] if exists */
	if (plugs[1] > 0) {
		err = fill_stream_formations(oxfw, AVC_GENERAL_PLUG_DIR_OUT, 0);
		if (err < 0)
			goto end;
		oxfw->has_output = true;
	}

	/* use iPCR[0] if exists */
	if (plugs[0] > 0) {
		err = fill_stream_formations(oxfw, AVC_GENERAL_PLUG_DIR_IN, 0);
		if (err < 0)
			goto end;
	}

	/* if its stream has MIDI conformant data channel, add one MIDI port */
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		if (oxfw->tx_stream_formations[i].midi > 0)
			oxfw->midi_input_ports = 1;
		if (oxfw->rx_stream_formations[i].midi > 0)
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
