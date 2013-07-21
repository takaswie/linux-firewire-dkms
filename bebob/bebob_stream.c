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

int snd_bebob_strem_get_formation_index(int sampling_rate)
{
        int table[] = {
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
        int i;

        for (i = 0; i < sizeof(table); i += 1) {
                if (table[i] == sampling_rate)
                        return i;
	}
	return -1;
}


int snd_bebob_stream_init(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	struct cmp_connection *connection;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &bebob->receive_stream) {
		connection = &bebob->output_connection;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_STREAM_IN;
	} else {
		connection = &bebob->input_connection;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_STREAM_OUT;
	}

	err = cmp_connection_init(connection, bebob->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, bebob->unit, s_dir, CIP_NONBLOCKING);
	if (err < 0) {
		cmp_connection_destroy(connection);
		goto end;
	}

end:
	return err;
}

int snd_bebob_stream_start(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	struct cmp_connection *connection;
	int err = 0;

	/* already running */
	if (!IS_ERR(stream->context))
		goto end;

	if (stream == &bebob->receive_stream)
		connection = &bebob->output_connection;
	else
		connection = &bebob->input_connection;

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

void snd_bebob_stream_stop(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	if (!!IS_ERR(stream->context))
		goto end;

	amdtp_stream_stop(stream);

	if (stream == &bebob->receive_stream)
		cmp_connection_break(&bebob->output_connection);
	else
		cmp_connection_break(&bebob->input_connection);
end:
	return;
}

void snd_bebob_stream_destroy(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	snd_bebob_stream_stop(bebob, stream);

	if (stream == &bebob->receive_stream)
		cmp_connection_destroy(&bebob->output_connection);
	else
		cmp_connection_destroy(&bebob->input_connection);

	return;
}
