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

int
snd_efw_stream_init(struct snd_efw_t *efw, struct snd_efw_stream_t *stream)
{
	enum cmp_direction c_direction;
	enum amdtp_stream_direction s_direction;
	int err;

	if (stream == &efw->receive_stream) {
		c_direction = CMP_OUTPUT;
		s_direction = AMDTP_STREAM_RECEIVE;
	} else {
		c_direction = CMP_INPUT;
		s_direction = AMDTP_STREAM_TRANSMIT;
	}

	err = cmp_connection_init(&stream->conn, efw->unit, c_direction, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(&stream->strm, efw->unit, s_direction, CIP_NONBLOCKING);
	if (err < 0) {
		cmp_connection_destroy(&stream->conn);
		goto end;
	}

	stream->pcm = false;
	stream->midi = false;

end:
	return err;
}

int
snd_efw_stream_start(struct snd_efw_stream_t *stream)
{
	int err;

	/* already running */
	if (!IS_ERR(stream->strm.context)) {
		err = 0;
		goto end;
	}

	/*
	 * establish connection via CMP
	 */
	err = cmp_connection_establish(&stream->conn,
		amdtp_stream_get_max_payload(&stream->strm));
	if (err < 0)
		goto end;

	/* start amdtp stream */
	err = amdtp_stream_start(&stream->strm,
		stream->conn.resources.channel,
		stream->conn.speed);
	if (err < 0)
		cmp_connection_break(&stream->conn);

end:
	return err;
}

void
snd_efw_stream_stop(struct snd_efw_stream_t *stream)
{
	if (!!IS_ERR(stream->strm.context))
		goto end;

	amdtp_stream_stop(&stream->strm);
	cmp_connection_break(&stream->conn);
end:
	return;
}

void
snd_efw_stream_destroy(struct snd_efw_stream_t *stream)
{
	snd_efw_stream_stop(stream);
	cmp_connection_destroy(&stream->conn);
	return;
}
