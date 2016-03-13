/*
 * receiver-midi.c - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "receiver.h"

static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_fwtx *fwrx = substream->rmidi->private_data;
	int index = substream->rmidi->device;
	int err;

	mutex_lock(&fwrx->mutex);
	fwrx->capture_substreams[index]++;
	err = snd_fwtx_stream_start_simplex(fwrx, index, 0);
	mutex_unlock(&fwrx->mutex);

	return err;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_fwtx *fwrx = substream->rmidi->private_data;
	int index = substream->rmidi->device;

	mutex_lock(&fwrx->mutex);
	fwrx->capture_substreams[index]--;
	snd_fwtx_stream_stop_simplex(fwrx, index);
	mutex_unlock(&fwrx->mutex);

	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substream,
				 int up)
{
	struct snd_fwtx *fwrx = substream->rmidi->private_data;
	int index = substream->rmidi->device;
	unsigned long flags;

	spin_lock_irqsave(&fwrx->lock, flags);

	if (up)
		amdtp_am824_midi_trigger(&fwrx->tx_stream[index],
					  substream->number, substream);
	else
		amdtp_am824_midi_trigger(&fwrx->tx_stream[index],
					  substream->number, NULL);

	spin_unlock_irqrestore(&fwrx->lock, flags);
}

static struct snd_rawmidi_ops midi_capture_ops = {
	.open		= midi_capture_open,
	.close		= midi_capture_close,
	.trigger	= midi_capture_trigger,
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

int snd_fwtx_create_midi_devices(struct snd_fwtx *fwrx)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *stream;
	int i, err;

	for (i = 0; i < OHCI1394_MIN_RX_CTX; i++) {
		/* create midi ports */
		err = snd_rawmidi_new(fwrx->card, fwrx->card->driver, i,
				      0, 8, &rmidi);
		if (err < 0)
			return err;

		snprintf(rmidi->name, sizeof(rmidi->name),
			 "%s %d MIDI", fwrx->card->shortname, i + 1);
		rmidi->private_data = fwrx;

		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &midi_capture_ops);

		stream = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];
		set_midi_substream_names(stream, i, fwrx->card->shortname);
	}

	return 0;
}
