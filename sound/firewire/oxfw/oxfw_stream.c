/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Takashi Sakamoto <o-takashi@sakamocchi.jp>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

/*
 * According to their datasheet:
 *  OXFW970: 32.0/44.1/48.0/96.0 Khz, 8 audio channels I/O
 *  OXFW971: 32.0/44.1/48.0/88.2/96.0/192.0 kHz, 16 audio channels I/O, MIDI I/O
 */
const unsigned int snd_oxfw_rate_table[SND_OXFW_STREAM_TABLE_ENTRIES] = {
	[0] = 32000,
	[1] = 44100,
	[2] = 48000,
	[3] = 88200,
	[4] = 96000,
	[5] = 176400,
	[6] = 192000,
};

int snd_oxfw_stream_init(struct snd_oxfw *oxfw)
{
	int err;

	err = cmp_connection_init(&oxfw->in_conn, oxfw->unit,
				  CMP_INPUT, 0);
	if (err < 0) {
		fw_unit_put(oxfw->unit);
		goto end;
	}

	err = amdtp_stream_init(&oxfw->rx_stream, oxfw->unit,
				AMDTP_OUT_STREAM, CIP_NONBLOCKING);
	if (err < 0)
		cmp_connection_destroy(&oxfw->in_conn);
end:
	return err;
}

int snd_oxfw_stream_start(struct snd_oxfw *oxfw)
{
	int err = 0;

	if (amdtp_stream_running(&oxfw->rx_stream))
		goto end;

	err = cmp_connection_establish(&oxfw->in_conn,
		amdtp_stream_get_max_payload(&oxfw->rx_stream));
	if (err < 0)
		goto end;

	err = amdtp_stream_start(&oxfw->rx_stream,
				 oxfw->in_conn.resources.channel,
				 oxfw->in_conn.speed);
	if (err < 0)
		cmp_connection_break(&oxfw->in_conn);
end:
	return err;
}

void snd_oxfw_stream_stop(struct snd_oxfw *oxfw)
{
	if (amdtp_stream_running(&oxfw->rx_stream))
		amdtp_stream_stop(&oxfw->rx_stream);

	cmp_connection_break(&oxfw->in_conn);
}

void snd_oxfw_stream_destroy(struct snd_oxfw *oxfw)
{
	amdtp_stream_pcm_abort(&oxfw->rx_stream);
	snd_oxfw_stream_stop(oxfw);
}

void snd_oxfw_stream_update(struct snd_oxfw *oxfw)
{
	if (cmp_connection_update(&oxfw->in_conn) < 0) {
		amdtp_stream_pcm_abort(&oxfw->rx_stream);
		snd_oxfw_stream_stop(oxfw);
	} else {
		amdtp_stream_update(&oxfw->rx_stream);
	}
}

int firewave_stream_discover(struct snd_oxfw *oxfw)
{
	oxfw->rx_stream_formations[2].pcm = 6;
	oxfw->rx_stream_formations[3].pcm = 6;
	oxfw->rx_stream_formations[4].pcm = 2;
	oxfw->rx_stream_formations[6].pcm = 2;

	return 0;
}

int lacie_speakers_stream_discover(struct snd_oxfw *oxfw)
{
	oxfw->rx_stream_formations[2].pcm = 2;
	oxfw->rx_stream_formations[3].pcm = 2;
	oxfw->rx_stream_formations[4].pcm = 2;
	oxfw->rx_stream_formations[5].pcm = 2;
	oxfw->rx_stream_formations[6].pcm = 2;

	return 0;
}
