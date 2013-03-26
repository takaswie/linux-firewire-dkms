/*
 * fireworks.h - driver for Firewire devices from Echo Digital Audio
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
 *
 * mostly based on FFADO's efc_cmd.h, which is
 * Copyright (C) 2005-2008 by Pieter Palmers
 *
 */
#ifndef SOUND_FIREWORKS_H_INCLUDED
#define SOUND_FIREWORKS_H_INCLUDED

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../cmp.h"
#include "../fcp.h"

#define MAX_MIDI_OUTPUTS 2
#define MAX_MIDI_INPUTS 2

#define ef_err(ef, format, arg...) dev_err(&(ef)->device->device, format, ##arg)

#define SND_EFW_MUITIPLIER_MODES 3
#define HWINFO_NAME_SIZE_BYTES 32
#define HWINFO_MAX_CAPS_GROUPS 8

/* for physical metering */
enum snd_efw_channel_type_t {
	SND_EFW_CHANNEL_TYPE_ANALOG		= 0,
	SND_EFW_CHANNEL_TYPE_SPDIF		= 1,
	SND_EFW_CHANNEL_TYPE_ADAT		= 2,
	SND_EFW_CHANNEL_TYPE_SPDIF_OR_ADAT	= 3,
	SND_EFW_CHANNEL_TYPE_ANALOG_MIRRORING	= 4,
	SND_EFW_CHANNEL_TYPE_HEADPHONES		= 5,
	SND_EFW_CHANNEL_TYPE_I2S		= 6
};
struct snd_efw_phys_group_t {
	u8 type;	/* enum snd_efw_channel_type_t */
	u8 count;
} __attribute__((packed));

/* for IEC61883-1 and -6 stream */
struct snd_efw_stream_t {
	struct cmp_connection conn;
	struct amdtp_out_stream strm;
	bool pcm;
	bool midi;
};

struct snd_efw_t {
	struct snd_card *card;
	struct fw_device *device;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	/* for EFC */
	u32 sequence_number;

	/* capabilities */
	unsigned int supported_sampling_rate;
	unsigned int supported_clock_source;
	unsigned int supported_digital_mode;
	unsigned int has_phantom;
	unsigned int has_dsp_mixer;
	unsigned int has_fpga;
	unsigned int aes_ebu_xlr_support;
	unsigned int mirroring_support;
	unsigned int dynaddr_support;

	/* physical metering */
	unsigned int output_group_counts;
	struct snd_efw_phys_group_t *output_groups;
	unsigned int output_meter_counts;
	unsigned int input_group_counts;
	struct snd_efw_phys_group_t *input_groups;
	unsigned int input_meter_counts;

	/* mixer */
	unsigned int mixer_output_channels;
	unsigned int mixer_input_channels;

	/* MIDI output */
	unsigned int midi_output_count;
	struct {
		struct snd_rawmidi_substream *substream;
		int fifo_filled;
		int fifo_max;
	} midi_outputs[MAX_MIDI_OUTPUTS];

	/* MIDI input */
	unsigned int midi_input_count;
	struct snd_rawmidi_substream *midi_inputs[MAX_MIDI_INPUTS];

	/* PCM playback */
	unsigned int pcm_playback_channels;
	unsigned int pcm_playback_channels_sets[SND_EFW_MUITIPLIER_MODES];

	/* PCM capture */
	unsigned int pcm_capture_channels;
	unsigned int pcm_capture_channels_sets[SND_EFW_MUITIPLIER_MODES];

	/* notification to control components */
	struct snd_ctl_elem_id *control_id_sampling_rate;
	struct snd_ctl_elem_id *control_id_clock_source;

	/* audio and music data transmittion protocol */
	struct snd_efw_stream_t transmit_stream;
	unsigned long midi_transmit_running;
	struct snd_efw_stream_t receive_stream;
	unsigned long midi_receive_running;
};

struct efc_hwinfo_t {
	u32 flags;
	u32 guid_hi;
	u32 guid_lo;
	u32 type;
	u32 version;
	char vendor_name[HWINFO_NAME_SIZE_BYTES];
	char model_name[HWINFO_NAME_SIZE_BYTES];
	u32 supported_clocks;
	u32 nb_1394_playback_channels;
	u32 nb_1394_capture_channels;
	u32 nb_phys_audio_out;
	u32 nb_phys_audio_in;
	u32 nb_out_groups;
	struct snd_efw_phys_group_t out_groups[HWINFO_MAX_CAPS_GROUPS];
	u32 nb_in_groups;
	struct snd_efw_phys_group_t in_groups[HWINFO_MAX_CAPS_GROUPS];
	u32 nb_midi_out;
	u32 nb_midi_in;
	u32 max_sample_rate;
	u32 min_sample_rate;
	u32 dsp_version;
	u32 arm_version;
	u32 mixer_playback_channels;
	u32 mixer_capture_channels;
	/* only with version 1: */
	u32 fpga_version;
	u32 nb_1394_playback_channels_2x;
	u32 nb_1394_capture_channels_2x;
	u32 nb_1394_playback_channels_4x;
	u32 nb_1394_capture_channels_4x;
	u32 reserved[16];
} __attribute__((packed));

struct efc_isoc_map {
	__be32 sample_rate;
	__be32 flags;
	__be32 num_playmap_entries;
	__be32 num_phys_out;
	__be32 playmap[32];
	__be32 num_recmap_entries;
	__be32 num_phys_in;
	__be32 recmap[32];
} __attribute__((packed));

/* clock source parameters */
enum snd_efw_clock_source_t {
	SND_EFW_CLOCK_SOURCE_INTERNAL	= 0,
	SND_EFW_CLOCK_SOURCE_SYTMATCH	= 1,
	SND_EFW_CLOCK_SOURCE_WORDCLOCK	= 2,
	SND_EFW_CLOCK_SOURCE_SPDIF	= 3,
	SND_EFW_CLOCK_SOURCE_ADAT_1	= 4,
	SND_EFW_CLOCK_SOURCE_ADAT_2	= 5,
};

/* SNDRV_PCM_RATE_XXX should be used to indicate sampling rate */

/* digital mode parameters */
enum snd_efw_digital_mode_t {
	SND_EFW_DIGITAL_MODE_SPDIF_COAXIAL	= 0,
	SND_EFW_DIGITAL_MODE_ADAT_COAXIAL	= 1,
	SND_EFW_DIGITAL_MODE_SPDIF_OPTICAL	= 2,
	SND_EFW_DIGITAL_MODE_ADAT_OPTICAL	= 3
};

/* S/PDIF format parameters */
enum snd_efw_iec60958_format_t {
	SND_EFW_IEC60958_FORMAT_CONSUMER	= 0,
	SND_EFW_IEC60958_FORMAT_PROFESSIONAL	= 1
};

/* Echo Fireworks Command functions */
/* for phys_in/phys_out/playback/capture/monitor category commands */
enum snd_efw_mixer_cmd_t {
	SND_EFW_MIXER_SET_GAIN		= 0,
	SND_EFW_MIXER_GET_GAIN		= 1,
	SND_EFW_MIXER_SET_MUTE		= 2,
	SND_EFW_MIXER_GET_MUTE		= 3,
	SND_EFW_MIXER_SET_SOLO		= 4,
	SND_EFW_MIXER_GET_SOLO		= 5,
	SND_EFW_MIXER_SET_PAN		= 6,
	SND_EFW_MIXER_GET_PAN		= 7,
	SND_EFW_MIXER_SET_NOMINAL	= 8,
	SND_EFW_MIXER_GET_NOMINAL	= 9
};
int snd_efw_command_identify(struct snd_efw_t *efw);
int snd_efw_command_get_hwinfo(struct snd_efw_t *efw, struct efc_hwinfo_t *hwinfo);
int snd_efw_command_get_phys_meters_count(struct snd_efw_t *efw, int *inputs, int *outputs);
int snd_efw_command_get_phys_meters(struct snd_efw_t *efw, int count, u32 *polled_meters);
int snd_efw_command_get_mixer_usable(struct snd_efw_t *efw, int *usable);
int snd_efw_command_set_mixer_usable(struct snd_efw_t *efw, int usable);
int snd_efw_command_get_iec60958_format(struct snd_efw_t *efw, enum snd_efw_iec60958_format_t *format);
int snd_efw_command_set_iec60958_format(struct snd_efw_t *efw, enum snd_efw_iec60958_format_t format);
int snd_efw_command_get_clock_source(struct snd_efw_t *efw, enum snd_efw_clock_source_t *source);
int snd_efw_command_set_clock_source(struct snd_efw_t *efw, enum snd_efw_clock_source_t source);
int snd_efw_command_get_sampling_rate(struct snd_efw_t *efw, int *sampling_rate);
int snd_efw_command_set_sampling_rate(struct snd_efw_t *efw, int sampling_rate);
int snd_efw_command_get_digital_mode(struct snd_efw_t *efw, enum snd_efw_digital_mode_t *mode);
int snd_efw_command_set_digital_mode(struct snd_efw_t *efw, enum snd_efw_digital_mode_t mode);
int snd_efw_command_get_phantom_state(struct snd_efw_t *efw, int *state);
int snd_efw_command_set_phantom_state(struct snd_efw_t *efw, int state);
int snd_efw_command_monitor(struct snd_efw_t *efw, enum snd_efw_mixer_cmd_t cmd, int input, int output, int *value);
int snd_efw_command_playback(struct snd_efw_t *efw, enum snd_efw_mixer_cmd_t cmd, int channel, int *value);
int snd_efw_command_phys_out(struct snd_efw_t *efw, enum snd_efw_mixer_cmd_t cmd, int channel, int *value);
int snd_efw_command_capture(struct snd_efw_t *efw, enum snd_efw_mixer_cmd_t cmd, int channel, int *value);
int snd_efw_command_phys_in(struct snd_efw_t *efw, enum snd_efw_mixer_cmd_t cmd, int channel, int *value);

/* for AMDTP stream and CMP */
int snd_efw_stream_init(struct snd_efw_t *efw, struct snd_efw_stream_t *stream);
int snd_efw_stream_start(struct snd_efw_stream_t *stream);
void snd_efw_stream_stop(struct snd_efw_stream_t *stream);
void snd_efw_stream_destroy(struct snd_efw_stream_t *stream);

/* for procfs subsystem */
void snd_efw_proc_init(struct snd_efw_t *efw);

/* for control component */
int snd_efw_create_control_devices(struct snd_efw_t *efw);

/* for midi component */
int snd_efw_create_midi_ports(struct snd_efw_t *ef);

/* for pcm component */
int snd_efw_create_pcm_devices(struct snd_efw_t *efw);
void snd_efw_destroy_pcm_devices(struct snd_efw_t *efw);

#endif
