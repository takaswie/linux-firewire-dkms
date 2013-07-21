#include "./bebob.h"

static void
proc_read_formation(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	struct snd_bebob_stream_formation *formation;
	int i;

	snd_iprintf(buffer, "Reveice Stream:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = bebob->tx_stream_formations;
	for (i = 0; i < 9; i += 1) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n",
			formation[i].sampling_rate,
			formation[i].pcm,
			formation[i].midi);
	}

	snd_iprintf(buffer, "Transmit Stream:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = bebob->rx_stream_formations;
	for (i = 0; i < 9; i += 1) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n",
			formation[i].sampling_rate,
			formation[i].pcm,
			formation[i].midi);
	}

	return;
}

static void
proc_read_clock(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	int err;
	int rate;

	err= avc_generic_get_sampling_rate(bebob->unit, &rate, 0, 0);
	if (err == 0)
		snd_iprintf(buffer,
			    "Output Plug 0: rate %d\n", rate);
	err = avc_generic_get_sampling_rate(bebob->unit, &rate, 1, 0);
	if (err == 0)
		snd_iprintf(buffer,
			    "Input Plug 0: rate %d\n", rate);

	return;
}

void snd_bebob_proc_init(struct snd_bebob *bebob)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(bebob->card, "#clock", &entry))
		snd_info_set_text_ops(entry, bebob, proc_read_clock);

	if (!snd_card_proc_new(bebob->card, "#formation", &entry))
		snd_info_set_text_ops(entry, bebob, proc_read_formation);

	return;
}
