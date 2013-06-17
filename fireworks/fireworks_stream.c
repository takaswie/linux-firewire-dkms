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

	err = amdtp_stream_init(stream, efw->unit, s_dir, CIP_NONBLOCKING);
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
	int err;

	/* TODO: clock source change? */
	err = snd_efw_command_get_clock_source(efw, &clock_source);
	if (err < 0)
		goto end;
	if (clock_source != SND_EFW_CLOCK_SOURCE_SYTMATCH) {
		master = &efw->receive_stream;
		slave = &efw->transmit_stream;
		sync_mode = AMDTP_STREAM_SYNC_DEVICE_MASTER;
	} else {
		master = &efw->transmit_stream;
		slave = &efw->receive_stream;
		sync_mode = AMDTP_STREAM_SYNC_DRIVER_MASTER;
	}

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

	if (efw->transmit_stream.sync_mode == AMDTP_STREAM_SYNC_DEVICE_MASTER) {
		master = &efw->transmit_stream;
		slave = &efw->receive_stream;
	} else {
		master = &efw->receive_stream;
		slave = &efw->transmit_stream;
	}

	snd_efw_stream_stop(efw, slave);
	snd_efw_stream_stop(efw, master);
}

void snd_efw_sync_streams_destroy(struct snd_efw *efw)
{
	snd_efw_sync_streams_stop(efw);
	snd_efw_stream_destroy(efw, &efw->receive_stream);
	snd_efw_stream_destroy(efw, &efw->transmit_stream);
}
