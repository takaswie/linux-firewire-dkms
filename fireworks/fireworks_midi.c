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
 * According to MMA/AMEI-027, MIDI stream is multiplexed with PCM stream in an
 * Firewire isochronous stream. And Fireworks need to handle input and output
 * stream with the same sampling rate. Then this module use the rules below:
 *
 * [MIDI stream side]
 *  1.When no stream in both direction is started, start stream with 48000
 *  2.When stream in opposite direction is started, start stream with the same
 *    sampling rate.
 *  3.When stream in the same direction has PCM stream and request to stop MIDI
 *    stream, don't stop stream itself.
 * [PCM stream side]
 *  1.When stream in the both direction is started and has no PCM stream, stop
 *    the stream because it include just MIDI stream. Then restart it with
 *    requested sampling rate by PCM component.
 *  2.When MIDI stream is going to be closed but PCM stream is still running,
 *    the stream is kept to be running.
 */

static int
midi_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	struct amdtp_stream *stream;
	struct amdtp_stream *opposite;
	int *pcm_channels;
	int run, mode, sampling_rate = 48000;
	int err;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT) {
		stream = &efw->receive_stream;
		pcm_channels = efw->pcm_capture_channels;
		opposite = &efw->transmit_stream;
	} else {
		stream = &efw->transmit_stream;
		pcm_channels = efw->pcm_playback_channels;
		opposite = &efw->receive_stream;
	}

	/* check other streams */
	run = stream->midi_triggered;

	/* register pointer */
	amdtp_stream_midi_add(stream, substream);

	/* the other MIDI streams running */
	if (run > 0) {
		err = 0;
		goto end;
	}
	/* PCM stream starts the stream */
	else if (amdtp_stream_running(stream)) {
		err = 0;
		goto end;
	}

	/* opposite stream is running then use the same sampling rate */
	if (amdtp_stream_running(opposite))
		err = snd_efw_command_get_sampling_rate(efw, &sampling_rate);
	else {
		err = snd_efw_command_set_sampling_rate(efw, sampling_rate);
		snd_ctl_notify(efw->card, SNDRV_CTL_EVENT_MASK_VALUE,
				efw->control_id_sampling_rate);
	}
	if (err < 0)
		goto end;
	mode = snd_efw_get_multiplier_mode(sampling_rate);
	amdtp_stream_set_rate(stream, sampling_rate);
	amdtp_stream_set_pcm(stream, pcm_channels[mode]);

	/* start stream just for MIDI */
	err = snd_efw_stream_start(efw, stream);
	if (err < 0)
		goto end;

end:
	return err;
}

static int
midi_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	struct amdtp_stream *stream;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		stream = &efw->receive_stream;
	else
		stream = &efw->transmit_stream;

	/* unregister pointer */
	amdtp_stream_midi_remove(stream, substream);

	/* the other streams is running */
	if (amdtp_stream_midi_running(stream) > 0)
		goto end;

	/* PCM stream is running */
	if (stream->pcm)
		goto end;

	/* stop stream */
	snd_efw_stream_stop(efw, stream);

end:
	return 0;
}

static void
midi_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_efw *efw = substream->rmidi->private_data;
	unsigned long *midi_triggered;
	unsigned long flags;

	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		midi_triggered = &efw->receive_stream.midi_triggered;
	else
		midi_triggered = &efw->transmit_stream.midi_triggered;

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
set_midi_substream_names(struct snd_efw *efw,
				     struct snd_rawmidi_str *str)
{
	struct snd_rawmidi_substream *subs;

	if (str->substream_count > 2)
		return;

	list_for_each_entry(subs, &str->substreams, list)
		snprintf(subs->name, sizeof(subs->name),
			"%s MIDI %d", efw->card->shortname, subs->number + 1);
}

int snd_efw_create_midi_devices(struct snd_efw *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	struct snd_rawmidi_substream *subs;
	int err;

	/* check the number of midi stream */
	if ((efw->midi_input_ports > AMDTP_MAX_MIDI_STREAMS) |
	    (efw->midi_output_ports > AMDTP_MAX_MIDI_STREAMS))
		return -EINVAL;

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
					&midi_input_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];

		set_midi_substream_names(efw, str);
		amdtp_stream_set_midi(&efw->receive_stream,
				      efw->midi_input_ports);
	}

	if (efw->midi_output_ports > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;

		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&midi_output_ops);

		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];

		list_for_each_entry(subs, &str->substreams, list)
			efw->transmit_stream.midi[subs->number] = subs;

		set_midi_substream_names(efw, str);
		amdtp_stream_set_midi(&efw->transmit_stream,
				      efw->midi_output_ports);
	}

	if ((efw->midi_output_ports > 0) && (efw->midi_input_ports > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
