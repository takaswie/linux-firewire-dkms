#include "./bebob.h"

static void
snd_bebob_proc_read_clock(struct snd_info_entry *entry,
			  struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	int rate;

	if (get_sampling_rate(bebob->unit, &rate, 0, 0) == 0)
		snd_iprintf(buffer, "Sampling Rate for capture: %d\n", rate);

	if (get_sampling_rate(bebob->unit, &rate, 1, 0) == 0)
		snd_iprintf(buffer, "Sampling Rate for playback: %d\n", rate);

	return;
}

void snd_bebob_proc_init(struct snd_bebob *bebob)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(bebob->card, "#clock", &entry))
		snd_info_set_text_ops(entry, bebob, snd_bebob_proc_read_clock);

	return;
}
