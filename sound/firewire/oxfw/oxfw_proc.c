/*
 * oxfw_proc.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
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

	snd_iprintf(buffer, "Output Stream from device:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = oxfw->tx_stream_formations;
	for (i = 0; i < SND_OXFW_STREAM_TABLE_ENTRIES; i++) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n", snd_oxfw_rate_table[i],
			formation[i].pcm, formation[i].midi);
	}

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

	if (snd_oxfw_stream_get_rate(oxfw, &rate) >= 0)
		snd_iprintf(buffer, "Sampling rate: %d\n", rate);
}

static void
add_node(struct snd_oxfw *oxfw, struct snd_info_entry *root, const char *name,
	 void (*op)(struct snd_info_entry *e, struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(oxfw->card, name, root);
	if (entry == NULL)
		return;

	snd_info_set_text_ops(entry, oxfw, op);
	if (snd_info_register(entry) < 0)
		snd_info_free_entry(entry);
}

void snd_oxfw_proc_init(struct snd_oxfw *oxfw)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed following to link structure
	 * at snd_card_disconnect().
	 */
	root = snd_info_create_card_entry(oxfw->card, "firewire",
					  oxfw->card->proc_root);
	if (root == NULL)
		return;
	root->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(root) < 0) {
		snd_info_free_entry(root);
		return;
	}

	add_node(oxfw, root, "clock", proc_read_clock);
	add_node(oxfw, root, "formation", proc_read_formation);
}
