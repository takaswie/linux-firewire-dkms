/*
 * oxfw_proc.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./oxfw.h"

static void
proc_read_formation(struct snd_info_entry *entry,
		    struct snd_info_buffer *buffer)
{
	struct snd_oxfw *oxfw = entry->private_data;
	struct snd_oxfw_stream_formation *formation;
	unsigned int i;

	snd_iprintf(buffer, "Input Stream to device:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = oxfw->rx_stream_formations;
	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n", snd_oxfw_rate_table[i],
			formation[i].pcm, formation[i].midi);
	}
}

static void
proc_read_clock(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_oxfw *oxfw = entry->private_data;
	unsigned int rate;
	int err;

	err = avc_general_get_sig_fmt(oxfw->unit, &rate,
				      AVC_GENERAL_PLUG_DIR_IN, 0);
	if ((err < 0) && (err == 0x09))
		snd_iprintf(buffer, "Sampling rate: %d\n", rate);
}

void snd_oxfw_proc_init(struct snd_oxfw *oxfw)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(oxfw->card, "#formation", &entry))
		snd_info_set_text_ops(entry, oxfw, proc_read_formation);

	if (!snd_card_proc_new(oxfw->card, "#clock", &entry))
		snd_info_set_text_ops(entry, oxfw, proc_read_clock);

	return;
}
