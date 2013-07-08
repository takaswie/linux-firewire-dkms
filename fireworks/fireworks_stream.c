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

int snd_efw_stream_init(struct snd_efw *efw, struct amdtp_stream *stream)
{
	struct cmp_connection *connection;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &efw->receive_stream) {
		connection = &efw->output_connection;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_STREAM_RECEIVE;
	} else {
		connection = &efw->input_connection;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_STREAM_TRANSMIT;
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

int snd_efw_stream_start(struct snd_efw *efw, struct amdtp_stream *stream)
{
	struct cmp_connection *connection;
	int err = 0;

	/* already running */
	if (amdtp_stream_running(stream))
		goto end;

	if (stream == &efw->receive_stream)
		connection = &efw->output_connection;
	else
		connection = &efw->input_connection;

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

void snd_efw_stream_stop(struct snd_efw *efw, struct amdtp_stream *stream)
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

void snd_efw_stream_update(struct snd_efw *efw, struct amdtp_stream *stream)
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

void snd_efw_stream_destroy(struct snd_efw *efw, struct amdtp_stream *stream)
{
	snd_efw_stream_stop(efw, stream);

	if (stream == &efw->receive_stream)
		cmp_connection_destroy(&efw->output_connection);
	else
		cmp_connection_destroy(&efw->input_connection);

	return;
}

int snd_efw_sync_streams_init(struct snd_efw *efw)
{
	int err;

	err = snd_efw_stream_init(efw, &efw->receive_stream);
	if (err < 0)
		goto end;

	err = snd_efw_stream_init(efw, &efw->transmit_stream);
	if (err < 0)
		snd_efw_stream_destroy(efw, &efw->receive_stream);
end:
	return err;
}

int snd_efw_sync_streams_start(struct snd_efw *efw)
{
	enum snd_efw_clock_source clock_source;
	struct amdtp_stream *master, *slave;
	enum amdtp_stream_sync_mode sync_mode;
	unsigned int master_channels, slave_channels;
	int sampling_rate, mode, err;

	/*
	 * TODO: sampling rate and clock source can be retrieved by the same
	 * EFC command.
	 */
	err = snd_efw_command_get_sampling_rate(efw, &sampling_rate);
	if (err < 0)
		goto end;
	mode = snd_efw_get_multiplier_mode(sampling_rate);

	/* TODO: how to deal with clock source change? */
	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;

	if (clock_source == SND_EFW_CLOCK_SOURCE_SYTMATCH) {
		master = &efw->transmit_stream;
		master_channels = efw->pcm_playback_channels[mode];
		slave = &efw->receive_stream;
		slave_channels = efw->pcm_capture_channels[mode];
		sync_mode = AMDTP_STREAM_SYNC_TO_DRIVER;
	} else {
		master = &efw->receive_stream;
		master_channels = efw->pcm_capture_channels[mode];
		slave = &efw->transmit_stream;
		slave_channels = efw->pcm_playback_channels[mode];
		sync_mode = AMDTP_STREAM_SYNC_TO_DEVICE;
	}

	amdtp_stream_set_rate(master, sampling_rate);
	amdtp_stream_set_pcm(master, master_channels);

	amdtp_stream_set_rate(slave, sampling_rate);
	amdtp_stream_set_pcm(slave, slave_channels);

	amdtp_stream_set_sync_mode(sync_mode, master, slave);

	err = snd_efw_stream_start(efw, master);
	if (err < 0)
		goto end;

	if (!amdtp_stream_wait_run(master)) {
		err = -EIO;
		goto end;
	}

	err = snd_efw_stream_start(efw, slave);
	if (err < 0)
		snd_efw_stream_destroy(efw, master);
end:
	return err;
}

void snd_efw_sync_streams_stop(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;

	if (efw->transmit_stream.sync_mode == AMDTP_STREAM_SYNC_TO_DRIVER) {
		master = &efw->transmit_stream;
		slave = &efw->receive_stream;
	} else {
		master = &efw->receive_stream;
		slave = &efw->transmit_stream;
	}

	/* stop master at first because master has a reference to slave */
	snd_efw_stream_stop(efw, master);
	snd_efw_stream_stop(efw, slave);
}

void snd_efw_sync_streams_update(struct snd_efw *efw)
{
	struct amdtp_stream *master, *slave;
	bool flag = false;

	if (efw->receive_stream.sync_mode == AMDTP_STREAM_SYNC_TO_DRIVER) {
		master = &efw->transmit_stream;
		slave = &efw->receive_stream;
	} else {
		master = &efw->receive_stream;
		slave = &efw->transmit_stream;
	}

	if (master->sync_slave == slave) {
		flag = true;
		master->sync_slave = ERR_PTR(-1);
	}
	snd_efw_stream_update(efw, master);

	snd_efw_stream_update(efw, slave);
	if (flag)
		master->sync_slave = slave;
}

void snd_efw_sync_streams_destroy(struct snd_efw *efw)
{
	if (amdtp_stream_pcm_running(&efw->receive_stream))
		amdtp_stream_pcm_abort(&efw->receive_stream);
	if (amdtp_stream_pcm_running(&efw->transmit_stream))
		amdtp_stream_pcm_abort(&efw->transmit_stream);

	snd_efw_sync_streams_stop(efw);

	snd_efw_stream_destroy(efw, &efw->receive_stream);
	snd_efw_stream_destroy(efw, &efw->transmit_stream);
}
