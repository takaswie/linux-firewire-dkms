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

#define MIDI_FIFO_SIZE 4096

static int
midi_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	int err;

	if (!!IS_ERR(efw->transmit_stream.context))
		goto running;

	err = cmp_connection_establish(&efw->input_connection,
		amdtp_out_stream_get_max_payload(&efw->transmit_stream));
	if (err < 0)
		goto err_conn;

	err = amdtp_out_stream_start(&efw->transmit_stream,
			efw->input_connection.resources.channel,
			efw->input_connection.speed);
	if (err < 0)
		goto err_strm;

running:
	efw->midi_transmit_running = true;
	return 0;

err_strm:
	cmp_connection_break(&efw->input_connection);
err_conn:
	return err;
}

static int
midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;

	if (efw->pcm_transmit_running ||
	    !!IS_ERR(efw->transmit_stream.context))
		goto end;

	amdtp_out_stream_stop(&efw->transmit_stream);
	cmp_connection_break(&efw->input_connection);

end:
	efw->midi_transmit_running = false;
	return 0;
}

static void
midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		__set_bit(substream->number,
				&efw->midi_transmit_running);
	else
		__clear_bit(substream->number,
				&efw->midi_transmit_running);

	spin_unlock_irqrestore(&efw->lock, flags);

	return;
}

static void
midi_output_drain(struct snd_rawmidi_substream *substream)
{
	/* FIXME: magic */
	msleep(4 + 1);
	return;
}

static struct snd_rawmidi_ops midi_output_ops = {
	.open		= midi_output_open,
	.close		= midi_output_close,
	.trigger	= midi_output_trigger,
	.drain		= midi_output_drain,
};

static int
midi_input_open(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	int err;

	if (!IS_ERR(efw->transmit_stream.context))
		goto running;

	/* TODO: codes should be in fireworks_amdtp.c */
	err = cmp_connection_establish(&efw->output_connection,
		amdtp_out_stream_get_max_payload(&efw->transmit_stream));
	if (err < 0)
		goto err_conn;

	err = amdtp_out_stream_start(&efw->transmit_stream,
			efw->output_connection.resources.channel,
			efw->output_connection.speed);
	if (err < 0)
		goto err_strm;

running:
	efw->midi_receive_running = true;
	return 0;

err_strm:
	cmp_connection_break(&efw->output_connection);
err_conn:
	return err;
}

static int
midi_input_close(struct snd_rawmidi_substream *substream)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;

	if (efw->pcm_receive_running ||
	    !!IS_ERR(efw->receive_stream.context))
		goto end;

	amdtp_out_stream_stop(&efw->receive_stream);
	cmp_connection_break(&efw->output_connection);

end:
	efw->midi_receive_running = false;
	return 0;
}

static void
midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct snd_efw_t *efw = substream->rmidi->private_data;
	unsigned long flags;

	spin_lock_irqsave(&efw->lock, flags);

	if (up)
		__set_bit(substream->number,
				&efw->midi_receive_running);
	else
		__clear_bit(substream->number,
				&efw->midi_receive_running);

	spin_unlock_irqrestore(&efw->lock, flags);

	return;
}

static struct snd_rawmidi_ops midi_input_ops = {
	.open		= midi_input_open,
	.close		= midi_input_close,
	.trigger	= midi_input_trigger,
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

int snd_efw_create_midi_ports(struct snd_efw_t *efw)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *str;
	struct snd_rawmidi_substream *subs;
	int i;
	int err;

	err = snd_rawmidi_new(efw->card, efw->card->driver, 0,
			      efw->midi_output_count, efw->midi_input_count,
			      &rmidi);
	if (err < 0)
		return err;

	snprintf(rmidi->name, sizeof(rmidi->name),
			"%s MIDI", efw->card->shortname);
	rmidi->private_data = efw;

	if (efw->midi_output_count > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				    &midi_output_ops);
		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT];
		list_for_each_entry(subs, &str->substreams, list)
			efw->midi_outputs[subs->number].substream = subs;
		set_midi_substream_names(efw, str);

		/* TODO: */
		for (i = 0; i < MAX_MIDI_OUTPUTS; i += 1)
			efw->midi_outputs[i].fifo_max = (MIDI_FIFO_SIZE - 1) * 44100;
	}

	if (efw->midi_input_count > 0) {
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
		snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
				    &midi_input_ops);
		str = &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT];
		list_for_each_entry(subs, &str->substreams, list)
			efw->midi_inputs[subs->number] = subs;
		set_midi_substream_names(efw, str);
	}

	if ((efw->midi_output_count > 0) && (efw->midi_input_count > 0))
		rmidi->info_flags |= SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}
