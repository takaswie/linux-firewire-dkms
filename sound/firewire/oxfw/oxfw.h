/*
 * oxfw.h - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>

#include "../lib.h"
#include "../fcp.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../cmp.h"

struct device_info {
	const char *driver_name;
	unsigned int mixer_channels;
	u8 mute_fb_id;
	u8 volume_fb_id;
};

#define	SND_OXFW_STREAM_TABLE_ENTRIES	7
struct snd_oxfw_stream_formation {
	unsigned int pcm;
	unsigned int midi;
};
extern const unsigned int snd_oxfw_rate_table[SND_OXFW_STREAM_TABLE_ENTRIES];
struct snd_oxfw {
	struct snd_card *card;
	struct fw_unit *unit;
	const struct device_info *device_info;
	struct mutex mutex;

	struct snd_oxfw_stream_formation
		rx_stream_formations[SND_OXFW_STREAM_TABLE_ENTRIES];
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;

	bool mute;
	s16 volume[6];
	s16 volume_min;
	s16 volume_max;
};

int snd_oxfw_stream_init(struct snd_oxfw *oxfw);
int snd_oxfw_stream_start(struct snd_oxfw *oxfw, unsigned int rate);
void snd_oxfw_stream_stop(struct snd_oxfw *oxfw);
void snd_oxfw_stream_destroy(struct snd_oxfw *oxfw);
void snd_oxfw_stream_update(struct snd_oxfw *oxfw);

int firewave_stream_discover(struct snd_oxfw *oxfw);
int lacie_speakers_stream_discover(struct snd_oxfw *oxfw);

int snd_oxfw_create_pcm(struct snd_oxfw *oxfw);

int snd_oxfw_create_mixer(struct snd_oxfw *oxfw);

void snd_oxfw_proc_init(struct snd_oxfw *oxfw);
