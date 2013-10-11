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

/* TODO: to use chache because there are some devices which don't respond */
static int mapping_channels(struct snd_bebob *bebob, struct amdtp_stream *s)
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

	if (s == &bebob->tx_stream)
		dir = 1;
	else
		dir = 0;

	err = avc_bridgeco_get_plug_channel_position(bebob->unit, dir, 0, buf);
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
			buf++;
			if (type != 0x0a)
				s->pcm_positions[pcm++] = pos;
			else
				s->midi_positions[midi++] = pos;
			buf++;
		}
	}

end:
	return err;
}

int snd_bebob_stream_init(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	struct cmp_connection *conn;
	enum cmp_direction c_dir;
	enum amdtp_stream_direction s_dir;
	int err;

	if (stream == &bebob->tx_stream) {
		conn= &bebob->out_conn;
		c_dir = CMP_OUTPUT;
		s_dir = AMDTP_IN_STREAM;
	} else {
		conn= &bebob->in_conn;
		c_dir = CMP_INPUT;
		s_dir = AMDTP_OUT_STREAM;
	}

	err = cmp_connection_init(conn, bebob->unit, c_dir, 0);
	if (err < 0)
		goto end;

	err = amdtp_stream_init(stream, bebob->unit, s_dir, CIP_NONBLOCKING);
	if (err < 0) {
		cmp_connection_destroy(conn);
		goto end;

	}

end:
	return err;
}

int snd_bebob_stream_start(struct snd_bebob *bebob, struct amdtp_stream *stream,
			   unsigned int sampling_rate)
{
	struct snd_bebob_stream_formation *formations;
	struct cmp_connection *conn;
	unsigned int index, pcm_channels, midi_channels;
	int err = 0;

	/* already running */
	if (!IS_ERR(stream->context))
		goto end;

	if (stream == &bebob->tx_stream) {
		formations = bebob->tx_stream_formations;
		conn= &bebob->out_conn;
	} else {
		formations = bebob->rx_stream_formations;
		conn= &bebob->in_conn;
	}

	index = snd_bebob_get_formation_index(sampling_rate);
	pcm_channels = formations[index].pcm;
	midi_channels = formations[index].midi;

	amdtp_stream_set_params(stream, sampling_rate,
				pcm_channels, midi_channels);

	err = mapping_channels(bebob, stream);
	if (err < 0)
		goto end;

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

void snd_bebob_stream_stop(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	if (!!IS_ERR(stream->context))
		goto end;

	amdtp_stream_stop(stream);

	if (stream == &bebob->tx_stream)
		cmp_connection_break(&bebob->out_conn);
	else
		cmp_connection_break(&bebob->in_conn);
end:
	return;
}

void snd_bebob_stream_destroy(struct snd_bebob *bebob, struct amdtp_stream *stream)
{
	snd_bebob_stream_stop(bebob, stream);

	if (stream == &bebob->tx_stream)
		cmp_connection_destroy(&bebob->out_conn);
	else
		cmp_connection_destroy(&bebob->in_conn);

	return;
}
