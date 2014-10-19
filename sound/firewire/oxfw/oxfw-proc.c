/*
 * oxfw_proc.c - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./oxfw.h"

static void proc_read_formation(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	struct snd_oxfw *oxfw = entry->private_data;
	u8 *format;
	struct snd_oxfw_stream_formation formation;
	unsigned int i, err;

	snd_iprintf(buffer, "Output Stream from device:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		format = oxfw->tx_stream_formats[i];
		if (format == NULL)
			continue;

		err = snd_oxfw_stream_parse_format(format, &formation);
		if (err < 0)
			continue;

		snd_iprintf(buffer, "\t%d\t%d\t%d\n",
			    formation.rate, formation.pcm, formation.midi);
	}

	snd_iprintf(buffer, "Input Stream to device:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	for (i = 0; i < SND_OXFW_STREAM_FORMAT_ENTRIES; i++) {
		format = oxfw->rx_stream_formats[i];
		if (format == NULL)
			continue;

		err = snd_oxfw_stream_parse_format(format, &formation);
		if (err < 0)
			continue;

		snd_iprintf(buffer, "\t%d\t%d\t%d\n",
			    formation.rate, formation.pcm, formation.midi);
	}
}

static void proc_read_clock(struct snd_info_entry *entry,
			    struct snd_info_buffer *buffer)
{
	struct snd_oxfw *oxfw = entry->private_data;
	unsigned int rate;

	if (snd_oxfw_stream_get_rate(oxfw, &rate) >= 0)
		snd_iprintf(buffer, "Sampling rate: %d\n", rate);
}

static void add_node(struct snd_oxfw *oxfw, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *e,
				struct snd_info_buffer *b))
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
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
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
