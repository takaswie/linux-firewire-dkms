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
	struct cmp_connection *connection;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &efw->receive_stream) {
		connection = &efw->output_connection;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_STREAM_IN;
	} else {
		connection = &efw->input_connection;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_STREAM_OUT;
	}

	err = cmp_connection_init(connection, efw->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, efw->unit, s_dir, CIP_BLOCKING);
	if (err < 0) {
		cmp_connection_destroy(connection);
		goto end;
	}

end:
	return err;
}

static int snd_efw_stream_start(struct snd_efw *efw,
				struct amdtp_stream *stream, int sampling_rate)
{
	struct cmp_connection *connection;
	unsigned int pcm_channels;
	int mode, err = 0;

	/* already running */
	if (amdtp_stream_running(stream))
		goto end;

	mode = snd_efw_get_multiplier_mode(sampling_rate);
	if (stream == &efw->receive_stream) {
		connection = &efw->output_connection;
		pcm_channels = efw->pcm_capture_channels[mode];
	} else {
		connection = &efw->input_connection;
		pcm_channels = efw->pcm_playback_channels[mode];
	}

	amdtp_stream_set_rate(stream, sampling_rate);
	amdtp_stream_set_pcm(stream, pcm_channels);
	amdtp_stream_set_midi(stream, 1);

	/*  establish connection via CMP */
	err = cmp_connection_establish(connection,
				amdtp_stream_get_max_payload(stream));
	if (err < 0)
		goto end;

	/* start amdtp stream */
	err = amdtp_stream_start(stream,
				 connection->resources.channel,
				 connection->speed);
	if (err < 0)
		cmp_connection_break(connection);

end:
	return err;
}

static void snd_efw_stream_stop(struct snd_efw *efw,
				struct amdtp_stream *stream)
{
	if (!amdtp_stream_running(stream))
		goto end;

	amdtp_stream_stop(stream);

	if (stream == &efw->receive_stream)
		cmp_connection_break(&efw->output_connection);
	else
		cmp_connection_break(&efw->input_connection);
end:
	return;
}

static void snd_efw_stream_update(struct snd_efw *efw,
				  struct amdtp_stream *stream)
{
	struct cmp_connection *conn;

	if (&efw->receive_stream == stream)
		conn = &efw->input_connection;
	else
		conn = &efw->output_connection;

	if (cmp_connection_update(conn) < 0) {
		amdtp_stream_pcm_abort(stream);
		mutex_lock(&efw->mutex);
		snd_efw_stream_stop(efw, stream);
		mutex_unlock(&efw->mutex);
	}
	amdtp_stream_update(stream);
}

static void snd_efw_stream_destroy(struct snd_efw *efw,
				   struct amdtp_stream *stream)
{
	snd_efw_stream_stop(efw, stream);

	if (stream == &efw->receive_stream)
		cmp_connection_destroy(&efw->output_connection);
	else
		cmp_connection_destroy(&efw->input_connection);

	return;
}

static int get_roles(struct snd_efw *efw,
		     enum amdtp_stream_sync_mode *sync_mode,
		     struct amdtp_stream **master, struct amdtp_stream **slave)
{
	enum snd_efw_clock_source clock_source;
	int err;

	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;

	if (clock_source != SND_EFW_CLOCK_SOURCE_SYTMATCH) {
		*master = &efw->receive_stream;
		*slave = &efw->transmit_stream;
		*sync_mode = AMDTP_STREAM_SYNC_TO_DEVICE;
	} else {
		*master = &efw->transmit_stream;
		*slave = &efw->receive_stream;
		*sync_mode = AMDTP_STREAM_SYNC_TO_DRIVER;
	}
end:
	return err;
}

int snd_efw_stream_init_duplex(struct snd_efw *efw)
{
	int err;

	err = snd_efw_stream_init(efw, &efw->receive_stream);
	if (err < 0)
		goto end;

	err = snd_efw_stream_init(efw, &efw->transmit_stream);

end:
	return err;
}

int snd_efw_stream_start_duplex(struct snd_efw *efw,
				struct amdtp_stream *request,
				int sampling_rate)
{
	struct amdtp_stream *master, *slave;
	enum amdtp_stream_sync_mode sync_mode;
	int err, current_rate;
	bool slave_flag;

	err = get_roles(efw, &sync_mode, &master, &slave);
	if (err < 0)
		return err;

	if ((request == slave) || amdtp_stream_running(slave))
		slave_flag = true;
	else
		slave_flag = false;

	/* change sampling rate if possible */
	err = snd_efw_command_get_sampling_rate(efw, &current_rate);
	if (err < 0)
		goto end;
	if (sampling_rate == 0)
		sampling_rate = current_rate;
	if (sampling_rate != current_rate) {
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
		amdtp_stream_set_sync_mode(sync_mode, master, slave);
		err = snd_efw_stream_start(efw, master, sampling_rate);
		if (err < 0)
			goto end;
	}

	/* start slave if needed */
	if (slave_flag && !amdtp_stream_running(slave))
		err = snd_efw_stream_start(efw, slave, sampling_rate);

end:
	return err;
}

int snd_efw_stream_stop_duplex(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;
	enum amdtp_stream_sync_mode sync_mode;
	int err;

	err = get_roles(efw, &sync_mode, &master, &slave);
	if (err < 0)
		goto end;

	if (amdtp_stream_pcm_running(slave) ||
	    snd_efw_midi_stream_running(efw, slave))
		goto end;

	snd_efw_stream_stop(efw, slave);

	if (!amdtp_stream_pcm_running(master) &&
	    !snd_efw_midi_stream_running(efw, master))
		snd_efw_stream_stop(efw, master);

end:
	return err;
}

void snd_efw_stream_update_duplex(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;

	if (efw->receive_stream.sync_mode == AMDTP_STREAM_SYNC_TO_DRIVER) {
		master = &efw->transmit_stream;
		slave = &efw->receive_stream;
	} else {
		master = &efw->receive_stream;
		slave = &efw->transmit_stream;
	}

	snd_efw_stream_update(efw, master);
	snd_efw_stream_update(efw, slave);
}

void snd_efw_stream_destroy_duplex(struct snd_efw *efw)
{
	if (amdtp_stream_pcm_running(&efw->receive_stream))
		amdtp_stream_pcm_abort(&efw->receive_stream);
	if (amdtp_stream_pcm_running(&efw->transmit_stream))
		amdtp_stream_pcm_abort(&efw->transmit_stream);

	snd_efw_stream_destroy(efw, &efw->receive_stream);
	snd_efw_stream_destroy(efw, &efw->transmit_stream);
}
