/*
 * digi00x.h - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_DIGI00X_H_INCLUDED
#define SOUND_DIGI00X_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

/* TODO: remove when merging to upstream. */
#include "../../../backport.h"

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rawmidi.h>
//#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "../lib.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"

struct snd_dg00x {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	/* Asynchronous message handler. */
	struct fw_address_handler message_handler;

	struct amdtp_stream tx_stream;
	struct fw_iso_resources tx_resources;

	struct amdtp_stream rx_stream;
	struct fw_iso_resources rx_resources;

	unsigned int playback_substreams;
	unsigned int capture_substreams;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;
};

/* values for SND_DG00X_ADDR_OFFSET_RATE */
enum snd_dg00x_rate {
	SND_DG00X_RATE_44100 = 0,
	SND_DG00X_RATE_48000,
	SND_DG00X_RATE_88200,
	SND_DG00X_RATE_96000,
	SND_DG00X_RATE_COUNT,
};

/* values for SND_DG00X_ADDR_OFFSET_CLOCK */
enum snd_dg00x_clock {
	SND_DG00X_CLOCK_INTERNAL = 0,
	SND_DG00X_CLOCK_SPDIF,
	SND_DG00X_CLOCK_ADAT,
	SND_DG00X_CLOCK_WORD,
};

extern const unsigned int snd_dg00x_stream_rates[SND_DG00X_RATE_COUNT];
extern const unsigned int
snd_dg00x_stream_mbla_data_channels[SND_DG00X_RATE_COUNT];
int snd_dg00x_stream_get_rate(struct snd_dg00x *dg00x, unsigned int *rate);
int snd_dg00x_stream_set_rate(struct snd_dg00x *dg00x, unsigned int rate);
int snd_dg00x_stream_get_clock(struct snd_dg00x *dg00x,
			       enum snd_dg00x_clock *clock);
int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x);
int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x, unsigned int rate);
void snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x);

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x);
int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x);
void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x);

void double_oh_three_write_s32(struct amdtp_stream *s,
			       struct snd_pcm_substream *pcm,
			       __be32 *buffer, unsigned int frames);

int snd_dg00x_create_pcm_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_hwdep_device(struct snd_dg00x *dg00x);
#endif
