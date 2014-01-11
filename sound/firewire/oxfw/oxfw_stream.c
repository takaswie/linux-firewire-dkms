/*
 * oxfw_stream.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Takashi Sakamoto <o-takashi@sakamocchi.jp>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "oxfw.h"

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
