/*
 * fireworks_midi.c - driver for Firewire devices from Echo Digital Audio
 *
 * Copyright (c) 2009-2010 Clemens Ladisch
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
#include "fireworks.h"

/*
 * According to MMA/AMEI-027, MIDI stream is multiplexed with PCM stream in
 * AMDTP packet. The data rate of MIDI message is much less than PCM so there
 * is a little problem to suspend MIDI streams.
 */
static int midi_capture_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	snd_efw_stream_start_duplex(efw, &efw->tx_stream, 0);
	amdtp_stream_midi_add(&efw->tx_stream, substream, 0);

	return 0;
}

static int midi_playback_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	snd_efw_stream_start_duplex(efw, &efw->rx_stream, 0);
	/*
	 * Fireworks ignores MIDI messages in more than 8 data blocks in an
	 * AMDTP packet.
	 */
	amdtp_stream_midi_add(&efw->rx_stream, substream, 8);

	return 0;
}

static int midi_capture_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	amdtp_stream_midi_remove(&efw->tx_stream, substream);
	snd_efw_stream_stop_duplex(efw);

	return 0;
}

static int midi_playback_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;

	amdtp_stream_midi_remove(&efw->rx_stream, substream);
	snd_efw_stream_stop_duplex(efw);

	return 0;
}

static void midi_capture_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_efw *efw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		__set_bit(substrm->number, &efw->tx_stream.midi_triggered);
	else
		__clear_bit(substrm->number, &efw->tx_stream.midi_triggered);

	spin_unlock_irqrestore(&efw->lock, flags);

	return;
}

static void midi_playback_trigger(struct snd_rawmidi_substream *substrm, int up)
{
	struct snd_efw *efw = substrm->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		__set_bit(substrm->number, &efw->rx_stream.midi_triggered);
	else
		__clear_bit(substrm->number, &efw->rx_stream.midi_triggered);

	spin_unlock_irqrestore(&efw->lock, flags);

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

static void set_midi_substream_names(struct snd_efw *efw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	list_for_each_entry(subs, &str->substreams, list) {
		snprintf(subs->name, sizeof(subs->name),
			 "%s MIDI %d", efw->card->shortname, subs->number + 1);
	}
}

int snd_efw_create_midi_devices(struct snd_efw *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	int err;

	/* check the number of midi stream */
	if ((efw->midi_input_ports > SND_EFW_MAX_MIDI_INPUTS) |
	    (efw->midi_output_ports > SND_EFW_MAX_MIDI_OUTPUTS))
		return -EIO;

	/* create midi ports */
	err = snd_rawmidi_new(efw->card, efw->card->driver, 0,
			      efw->midi_output_ports, efw->midi_input_ports,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
			"%s MIDI", efw->card->shortname);
	rmidi->private_data = efw;

	if (efw->midi_input_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&midi_capture_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(efw, str);
	}

	if (efw->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&midi_playback_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		set_midi_substream_names(efw, str);
	}

	if ((efw->midi_output_ports > 0) && (efw->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
