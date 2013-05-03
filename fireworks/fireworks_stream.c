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

int snd_efw_stream_init(struct snd_efw_t *efw, struct amdtp_stream *stream)
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

int snd_efw_stream_start(struct snd_efw_t *efw, struct amdtp_stream *stream)
{
	struct cmp_connection *connection;
	int err = 0;

	/* already running */
	if (!IS_ERR(stream->context))
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

void snd_efw_stream_stop(struct snd_efw_t *efw, struct amdtp_stream *stream)
{
	if (!!IS_ERR(stream->context))
		goto end;

	amdtp_stream_stop(stream);

	if (stream == &efw->receive_stream)
		cmp_connection_break(&efw->output_connection);
	else
		cmp_connection_break(&efw->input_connection);
end:
	return;
}

void snd_efw_stream_destroy(struct snd_efw_t *efw, struct amdtp_stream *stream)
{
	snd_efw_stream_stop(efw, stream);

	if (stream == &efw->receive_stream)
		cmp_connection_destroy(&efw->output_connection);
	else
		cmp_connection_destroy(&efw->input_connection);

	return;
}
