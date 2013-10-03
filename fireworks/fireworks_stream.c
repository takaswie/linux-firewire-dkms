/*
 * fireworks_stream.c - driver for Firewire devices from Echo Digital Audio
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
#include "./fireworks.h"

static int snd_efw_stream_init(struct snd_efw *efw, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &efw->tx_stream) {
		conn = &efw->out_conn;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_IN_STREAM;
	} else {
		conn= &efw->in_conn;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_OUT_STREAM;
	}

	err = cmp_connection_init(conn, efw->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, efw->unit, s_dir, CIP_BLOCKING);
	if (err < 0) {
		cmp_connection_destroy(conn);
		goto end;
	}

end:
	return err;
}

static int snd_efw_stream_start(struct snd_efw *efw,
				struct amdtp_stream *stream, int sampling_rate)
{
	struct cmp_connection *conn;
	unsigned int pcm_channels, midi_channels;
	int mode, err = 0;

	/* already running */
	if (amdtp_stream_running(stream))
		goto end;

	mode = snd_efw_get_multiplier_mode(sampling_rate);
	if (stream == &efw->tx_stream) {
		conn = &efw->out_conn;
		pcm_channels = efw->pcm_capture_channels[mode];
		midi_channels = DIV_ROUND_UP(efw->midi_output_ports, 8);
	} else {
		conn = &efw->in_conn;
		pcm_channels = efw->pcm_playback_channels[mode];
		midi_channels = DIV_ROUND_UP(efw->midi_input_ports, 8);
	}

	amdtp_stream_set_params(stream, sampling_rate, pcm_channels, midi_channels);

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

static void snd_efw_stream_stop(struct snd_efw *efw,
				struct amdtp_stream *stream)
{
	if (!amdtp_stream_running(stream))
		goto end;

	amdtp_stream_stop(stream);

	if (stream == &efw->tx_stream)
		cmp_connection_break(&efw->out_conn);
	else
		cmp_connection_break(&efw->in_conn);
end:
	return;
}

static void snd_efw_stream_update(struct snd_efw *efw,
				  struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (&efw->tx_stream == stream)
		conn = &efw->out_conn;
	else
		conn = &efw->in_conn;

	if (cmp_connection_update(conn) < 0) {
		amdtp_stream_pcm_abort(stream);
		mutex_lock(&efw->mutex);
		snd_efw_stream_stop(efw, stream);
		mutex_unlock(&efw->mutex);
		return;
	}
	amdtp_stream_update(stream);
}

static void snd_efw_stream_destroy(struct snd_efw *efw,
				   struct amdtp_stream *stream)
{
	snd_efw_stream_stop(efw, stream);

	if (stream == &efw->tx_stream)
		cmp_connection_destroy(&efw->out_conn);
	else
		cmp_connection_destroy(&efw->in_conn);

	return;
}

static int get_roles(struct snd_efw *efw,
		     enum cip_flags *sync_mode,
		     struct amdtp_stream **master, struct amdtp_stream **slave)
{
	enum snd_efw_clock_source clock_source;
	int err;

	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;

	if (clock_source != SND_EFW_CLOCK_SOURCE_SYTMATCH) {
		*master = &efw->tx_stream;
		*slave = &efw->rx_stream;
		*sync_mode = CIP_SYNC_TO_DEVICE;
	} else {
		*master = &efw->rx_stream;
		*slave = &efw->tx_stream;
		*sync_mode = ~CIP_SYNC_TO_DEVICE;
	}
end:
	return err;
}

int snd_efw_stream_init_duplex(struct snd_efw *efw)
{
	int err;

	err = snd_efw_stream_init(efw, &efw->tx_stream);
	if (err < 0)
		goto end;

	err = snd_efw_stream_init(efw, &efw->rx_stream);
end:
	return err;
}

int snd_efw_stream_start_duplex(struct snd_efw *efw,
				struct amdtp_stream *request,
				int sampling_rate)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err, curr_rate;
	bool slave_flag;

	err = get_roles(efw, &sync_mode, &master, &slave);
	if (err < 0)
		return err;

	if ((request == slave) || amdtp_stream_running(slave))
		slave_flag = true;
	else
		slave_flag = false;

	/* change sampling rate if possible */
	err = snd_efw_command_get_sampling_rate(efw, &curr_rate);
	if (err < 0)
		goto end;
	if (sampling_rate == 0)
		sampling_rate = curr_rate;
	if (sampling_rate != curr_rate) {
		/* master is just for MIDI stream */
		if (amdtp_stream_running(master) &&
		    !amdtp_stream_pcm_running(master))
			snd_efw_stream_stop(efw, master);

		/* slave is just for MIDI stream */
		if (amdtp_stream_running(slave) &&
		    !amdtp_stream_pcm_running(slave))
			snd_efw_stream_stop(efw, slave);

		err = snd_efw_command_set_sampling_rate(efw, sampling_rate);
		if (err < 0)
			return err;
		snd_ctl_notify(efw->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       efw->control_id_sampling_rate);
	}

	/*  master should be always running */
	if (!amdtp_stream_running(master)) {
		amdtp_stream_set_sync(sync_mode, master, slave);
		err = snd_efw_stream_start(efw, master, sampling_rate);
		if (err < 0)
			goto end;

		err = amdtp_stream_wait_run(master);
		if (err < 0)
			goto end;
	}

	/* start slave if needed */
	if (slave_flag && !amdtp_stream_running(slave))
		err = snd_efw_stream_start(efw, slave, sampling_rate);
		if (err < 0)
			goto end;
		err = amdtp_stream_wait_run(slave);
end:
	return err;
}

int snd_efw_stream_stop_duplex(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;
	enum cip_flags sync_mode;
	int err;

	err = get_roles(efw, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if (amdtp_stream_pcm_running(slave) ||
	    amdtp_stream_midi_running(slave))
		goto end;

	snd_efw_stream_stop(efw, slave);

	if (!amdtp_stream_pcm_running(master) &&
	    !amdtp_stream_midi_running(master))
		snd_efw_stream_stop(efw, master);

end:
	return err;
}

void snd_efw_stream_update_duplex(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;

	if (efw->tx_stream.flags & CIP_SYNC_TO_DEVICE) {
		master = &efw->tx_stream;
		slave = &efw->rx_stream;
	} else {
		master = &efw->rx_stream;
		slave = &efw->tx_stream;
	}

	snd_efw_stream_update(efw, master);
	snd_efw_stream_update(efw, slave);
}

void snd_efw_stream_destroy_duplex(struct snd_efw *efw)
{
	if (amdtp_stream_pcm_running(&efw->tx_stream))
		amdtp_stream_pcm_abort(&efw->tx_stream);
	if (amdtp_stream_pcm_running(&efw->rx_stream))
		amdtp_stream_pcm_abort(&efw->rx_stream);

	snd_efw_stream_destroy(efw, &efw->tx_stream);
	snd_efw_stream_destroy(efw, &efw->rx_stream);
}
