/*
 * bebob_midi.c - a part of driver for BeBoB based devices
 * Copyright (c) 2013 Takashi Sakamoto
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

#include "bebob.h"

/*
 * According to MMA/AMEI-027, MIDI stream is multiplexed with PCM stream in
 * AMDTP packet. The data rate of MIDI message is much less than PCM so there
 * is a little problem to suspend MIDI streams.
 */
static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		goto end;

	snd_bebob_stream_start_duplex(bebob, &bebob->tx_stream, 0);
	amdtp_stream_midi_add(&bebob->tx_stream, substream);
end:
	return err;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;
	int err;

	err = snd_bebob_stream_lock_try(bebob);
	if (err < 0)
		goto end;

	snd_bebob_stream_start_duplex(bebob, &bebob->rx_stream, 0);
	amdtp_stream_midi_add(&bebob->rx_stream, substream);
end:
	return err;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;

	amdtp_stream_midi_remove(&bebob->tx_stream, substream);
	snd_bebob_stream_stop_duplex(bebob);
	snd_bebob_stream_lock_release(bebob);

	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_bebob *bebob = substream->rmidi->private_data;

	amdtp_stream_midi_remove(&bebob->rx_stream, substream);
	snd_bebob_stream_stop_duplex(bebob);
	snd_bebob_stream_lock_release(bebob);

	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_bebob *bebob = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bebob->lock, flags);

	if (up)
		__set_bit(substrm->number, &bebob->tx_stream.midi_triggered);
	else
		__clear_bit(substrm->number, &bebob->tx_stream.midi_triggered);

	spin_unlock_irqrestore(&bebob->lock, flags);

	return;
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_bebob *bebob = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bebob->lock, flags);

	if (up)
		__set_bit(substrm->number, &bebob->rx_stream.midi_triggered);
	else
		__clear_bit(substrm->number, &bebob->rx_stream.midi_triggered);

	spin_unlock_irqrestore(&bebob->lock, flags);

	return;
}

static struct snd_rawmidi_ops midi_capture_ops = {
	.open		= midi_capture_open,
	.close		= midi_capture_close,
	.trigger	= midi_capture_trigger,
};

static struct snd_rawmidi_ops midi_playback_ops = {
	.open		= midi_playback_open,
	.close		= midi_playback_close,
	.trigger	= midi_playback_trigger,
};

static void set_midi_substream_names(struct snd_bebob *bebob,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d", bebob->card->shortname, subs->number + 1);
	}
}

int snd_bebob_create_midi_devices(struct snd_bebob *bebob)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	int err;

	/* create midi ports */
	err = snd_rawmidi_new(bebob->card, bebob->card->driver, 0,
			      bebob->midi_output_ports, bebob->midi_input_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
			"%s MIDI", bebob->card->shortname);
	rmidi->private_data = bebob;

	if (bebob->midi_input_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&midi_capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(bebob, str);
	}

	if (bebob->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&midi_playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(bebob, str);
	}

	if ((bebob->midi_output_ports > 0) && (bebob->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
