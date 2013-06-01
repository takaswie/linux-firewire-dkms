/*
 * fireworks_proc.c - driver for Firewire devices from Echo Digital Audio
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

#include "./fireworks.h"

static void
proc_read_hwinfo(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	unsigned short i;
	struct snd_efw_hwinfo hwinfo;

	if(snd_efw_command_get_hwinfo(efw, &hwinfo) < 0)
		goto end;

	snd_iprintf(buffer, "guid_hi: 0x%X\n", hwinfo.guid_hi);
	snd_iprintf(buffer, "guid_lo: 0x%X\n", hwinfo.guid_lo);
	snd_iprintf(buffer, "type: 0x%X\n", hwinfo.type);
	snd_iprintf(buffer, "version: 0x%X\n", hwinfo.version);
	snd_iprintf(buffer, "vendor_name: %s\n", hwinfo.vendor_name);
	snd_iprintf(buffer, "model_name: %s\n", hwinfo.model_name);

	snd_iprintf(buffer, "dsp_version: 0x%X\n", hwinfo.dsp_version);
	snd_iprintf(buffer, "arm_version: 0x%X\n", hwinfo.arm_version);
	snd_iprintf(buffer, "fpga_version: 0x%X\n", hwinfo.fpga_version);

	snd_iprintf(buffer, "flags: 0x%X\n", hwinfo.flags);

	snd_iprintf(buffer, "max_sample_rate: 0x%X\n", hwinfo.max_sample_rate);
	snd_iprintf(buffer, "min_sample_rate: 0x%X\n", hwinfo.min_sample_rate);
	snd_iprintf(buffer, "supported_clock: 0x%X\n", hwinfo.supported_clocks);

	snd_iprintf(buffer, "nb_phys_audio_out: 0x%X\n", hwinfo.nb_phys_audio_out);
	snd_iprintf(buffer, "nb_phys_audio_in: 0x%X\n", hwinfo.nb_phys_audio_in);

	snd_iprintf(buffer, "nb_in_groups: 0x%X\n", hwinfo.nb_in_groups);
	for (i = 0; i < hwinfo.nb_in_groups; i += 1) {
		snd_iprintf(buffer, "in_group[0x%d]: type 0x%d, count 0x%d\n",
			i, hwinfo.out_groups[i].type, hwinfo.out_groups[i].count);
	}

	snd_iprintf(buffer, "nb_out_groups: 0x%X\n", hwinfo.nb_out_groups);
	for (i = 0; i < hwinfo.nb_out_groups; i += 1) {
		snd_iprintf(buffer, "out_group[0x%d]: type 0x%d, count 0x%d\n",
			i, hwinfo.out_groups[i].type, hwinfo.out_groups[i].count);
	}

	snd_iprintf(buffer, "nb_1394_playback_channels: 0x%X\n", hwinfo.nb_1394_playback_channels);
	snd_iprintf(buffer, "nb_1394_capture_channels: 0x%X\n", hwinfo.nb_1394_capture_channels);
	snd_iprintf(buffer, "nb_1394_playback_channels_2x: 0x%X\n", hwinfo.nb_1394_playback_channels_2x);
	snd_iprintf(buffer, "nb_1394_capture_channels_2x: 0x%X\n", hwinfo.nb_1394_capture_channels_2x);
	snd_iprintf(buffer, "nb_1394_playback_channels_4x: 0x%X\n", hwinfo.nb_1394_playback_channels_4x);
	snd_iprintf(buffer, "nb_1394_capture_channels_4x: 0x%X\n", hwinfo.nb_1394_capture_channels_4x);

	snd_iprintf(buffer, "nb_midi_out: 0x%X\n", hwinfo.nb_midi_out);
	snd_iprintf(buffer, "nb_midi_in: 0x%X\n", hwinfo.nb_midi_in);

	snd_iprintf(buffer, "mixer_playback_channels: 0x%X\n", hwinfo.mixer_playback_channels);
	snd_iprintf(buffer, "mixer_capture_channels: 0x%X\n", hwinfo.mixer_capture_channels);

end:
	return;
}

static void
proc_read_clock(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;
	enum snd_efw_clock_source clock_source;
	int sampling_rate;

	if (snd_efw_command_get_clock_source(efw, &clock_source) < 0)
		goto end;

	if (snd_efw_command_get_sampling_rate(efw, &sampling_rate) < 0)
		goto end;

	snd_iprintf(buffer, "Clock Source: %d\n", clock_source);
	snd_iprintf(buffer, "Sampling Rate: %d\n", sampling_rate);

end:
	return;
}

static void
proc_read_phys_meters(struct snd_info_entry *entry,
		      struct snd_info_buffer *buffer)
{
	struct snd_efw *efw = entry->private_data;

	char const *descs[] = {"Analog", "S/PDIF", "ADAT", "S/PDIF or ADAT",
			       "Analog Mirroring", "Headphones", "I2S"};

	struct snd_efw_phys_meters *meters;
	int i, g, c;
	int base = sizeof(struct snd_efw_phys_meters);
	int count = efw->input_meter_counts + efw->output_meter_counts;
	int err;

	meters = kzalloc(base + count * 4, GFP_KERNEL);
	if (meters == NULL)
		return;

	err = snd_efw_command_get_phys_meters(efw, meters, base + count * 4);
	if (err < 0)
		goto end;

	snd_iprintf(buffer, "Physical Meters:\n");

	snd_iprintf(buffer, " %d Inputs:\n", efw->input_meter_counts);
	g = 0;
	c = 0;
	for (i = 0; i < efw->input_meter_counts; i += 1) {
		if (c == efw->input_groups[g].count) {
			g += 1;
			c = 0;
		}
		snd_iprintf(buffer, "\t%s [%d]: %d\n",
			descs[efw->input_groups[g].type], c,
			meters->values[efw->output_meter_counts + i]);
		c += 1;
	}

	snd_iprintf(buffer, " %d Outputs:\n", efw->output_meter_counts);
	g = 0;
	c = 0;
	for (i = 0; i < efw->output_meter_counts; i += 1) {
		if (c == efw->output_groups[g].count) {
			g += 1;
			c = 0;
		}
		snd_iprintf(buffer, "\t%s [%d]: %d\n",
			descs[efw->output_groups[g].type], c,
			meters->values[i]);
		c += 1;
	}

end:
	kfree(meters);
	return;
}

void snd_efw_proc_init(struct snd_efw *efw)
{
	struct snd_info_entry *entry;

	if(!snd_card_proc_new(efw->card, "#hardware", &entry))
		snd_info_set_text_ops(entry, efw, proc_read_hwinfo);
	if(!snd_card_proc_new(efw->card, "#clock", &entry))
		snd_info_set_text_ops(entry, efw, proc_read_clock);
	if(!snd_card_proc_new(efw->card, "#meters", &entry))
		snd_info_set_text_ops(entry, efw, proc_read_phys_meters);
	return;
}
