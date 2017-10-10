/*
 * mln-proc.c - a part of driver for Yamaha MLN3 board module.
 *
 * Copyright (c) 2017-2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "mln.h"

static void dump_info(struct snd_info_entry *entry,
		      struct snd_info_buffer *buffer)
{
	struct snd_mln *mln = entry->private_data;

	mln->protocol->dump_info(mln, buffer);
}

static void add_node(struct snd_mln *mln, struct snd_info_entry *root,
		     const char *name,
		     void (*op)(struct snd_info_entry *e,
			        struct snd_info_buffer *b))
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(mln->card, name, root);
	if (entry == NULL)
		return;

	snd_info_set_text_ops(entry, mln, op);
	if (snd_info_register(entry) < 0)
		snd_info_free_entry(entry);
}

void snd_mln_proc_init(struct snd_mln *mln)
{
	struct snd_info_entry *root;

	/*
	 * All nodes are automatically removed at snd_card_disconnect(),
	 * by following to link list.
	 */
	root = snd_info_create_card_entry(mln->card, "firewire",
					  mln->card->proc_root);
	if (root == NULL)
		return;
	root->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(root) < 0) {
		snd_info_free_entry(root);
		return;
	}

	add_node(mln, root, "registers", dump_info);
}
