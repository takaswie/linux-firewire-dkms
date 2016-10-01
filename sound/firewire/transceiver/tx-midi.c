/*
 * tx-midi.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "tx.h"

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	/* Do nothing. */
	return 0;
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substream,
				  int up)
{
	struct fw_am_unit *am = substream->rmidi->private_data;
	int index = substream->rmidi->device;
	unsigned long flags;

	spin_lock_irqsave(&am->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&am->opcr[index].stream,
					 substream->number, substream);
	else
		amdtp_am824_midi_trigger(&am->opcr[index].stream,
					 substream->number, NULL);

	spin_unlock_irqrestore(&am->lock, flags);
}

static struct snd_rawmidi_ops midi_playback_ops = {
	.open		= midi_playback_open,
	.close		= midi_playback_close,
	.trigger	= midi_playback_trigger,
};

static void set_midi_substream_names(struct snd_rawmidi_str *stream,
				     int index, const char *name)
{
	struct snd_rawmidi_substream *substream;

	list_for_each_entry(substream, &stream->substreams, list) {
		snprintf(substream->name, sizeof(substream->name),
			 "%s %d MIDI %d",
			 name, index + 1, substream->number + 1);
	}
}

int fw_am_unit_create_midi_devices(struct fw_am_unit * am)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *stream;
	int i, err;

	for (i = 0; i < OHCI1394_MIN_TX_CTX; i++) {
		/* create midi ports */
		err = snd_rawmidi_new(am->card, am->card->driver, i,
				      8, 0, &rmidi);
		if (err < 0)
			return err;

		snprintf(rmidi->name, sizeof(rmidi->name),
			 "%s %d MIDI", am->card->shortname, i + 1);
		rmidi->private_data = am;

		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &midi_playback_ops);

		stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
		set_midi_substream_names(stream, i, am->card->shortname);
	}

	return 0;
}
