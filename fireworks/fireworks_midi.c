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

static int
midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	struct snd_efw_stream_t *stream;
	int err, run;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		stream = &efw->receive_stream;
	else
		stream = &efw->transmit_stream;

	/* check other streams */
	run = stream->strm.midi_triggered;

	/* register pointer */
	amdtp_stream_midi_register(&stream->strm, substream);

	/* the other streams running */
	if (run > 0) {
		err = 0;
		goto end;
	}

	/* start stream */
	err = snd_efw_stream_start(stream);
	if (err < 0)
		goto end;

	/* midi is transferred */
	stream->midi = true;

end:
	return err;
}

static int
midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	struct snd_efw_stream_t *stream;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		stream = &efw->receive_stream;
	else
		stream = &efw->transmit_stream;

	/* unregister pointer */
	amdtp_stream_midi_unregister(&stream->strm, substream);

	/* the other streams running */
	if (amdtp_stream_midi_running(&stream->strm) > 0)
		goto end;

	/* stop stream */
	snd_efw_stream_stop(stream);

	/* midi is not transferred */
	stream->midi = false;

end:
	return 0;
}

static void
midi_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	unsigned long *midi_triggered;
	unsigned long flags;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		midi_triggered = &efw->receive_stream.strm.midi_triggered;
	else
		midi_triggered = &efw->transmit_stream.strm.midi_triggered;

	spin_lock_irqsave(&efw->lock, flags);

	/* bit table shows MIDI stream has data or not */
	if (up)
		__set_bit(substream->number, midi_triggered);
	else
		__clear_bit(substream->number, midi_triggered);

	spin_unlock_irqrestore(&efw->lock, flags);

	return;
}

static struct snd_rawmidi_ops midi_output_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_trigger,
};

static struct snd_rawmidi_ops midi_input_ops = {
	.open		= midi_open,
	.close		= midi_close,
	.trigger	= midi_trigger,
};

static void
set_midi_substream_names(struct snd_efw_t *efw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	if (str->substream_count > 2)
		return;

	list_for_each_entry(subs, &str->substreams, list)
		snprintf(subs->name, sizeof(subs->name),
			"%s MIDI %d", efw->card->shortname, subs->number + 1);
}

int
snd_efw_create_midi_devices(struct snd_efw_t *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	struct snd_rawmidi_substream *subs;
	int err;

	/* check the number of midi stream */
	if ((efw->midi_input_count > AMDTP_MAX_MIDI_STREAMS) |
	    (efw->midi_output_count > AMDTP_MAX_MIDI_STREAMS))
		return -EINVAL;

	/* create midi ports */
	err = snd_rawmidi_new(efw->card, efw->card->driver, 0,
			      efw->midi_output_count, efw->midi_input_count,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
			"%s MIDI", efw->card->shortname);
	rmidi->private_data = efw;

	if (efw->midi_input_count > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&midi_input_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(efw, str);
	}

	if (efw->midi_output_count > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&midi_output_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		list_for_each_entry(subs, &str->substreams, list)
			efw->transmit_stream.strm.midi[subs->number] = subs;

		set_midi_substream_names(efw, str);
	}

	if ((efw->midi_output_count > 0) && (efw->midi_input_count > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
