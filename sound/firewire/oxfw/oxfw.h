/*
 * oxford.h - a part of driver for OXFW970/971 based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_OXFW_H_INCLUDED
#define SOUND_OXFW_H_INCLUDED

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/slab.h>

/* TODO: when mering to upstream, this path should be changed. */
#include "../../../include/uapi/sound/asound.h"
#include "../../../include/uapi/sound/firewire.h"

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/hwdep.h>

#include "../lib.h"
#include "../fcp.h"
#include "../amdtp.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../cmp.h"

#define	SND_OXFW_RATE_TABLE_ENTRIES	7
struct snd_oxfw_stream_formation {
	unsigned int pcm;
	unsigned int midi;
};
/* this is a lookup table for index of stream formations */
extern const unsigned int snd_oxfw_rate_table[SND_OXFW_RATE_TABLE_ENTRIES];

struct snd_oxfw {
	struct snd_card *card;
	struct fw_device *device;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	struct snd_oxfw_stream_formation
		tx_stream_formations[SND_OXFW_RATE_TABLE_ENTRIES];
	struct snd_oxfw_stream_formation
		rx_stream_formations[SND_OXFW_RATE_TABLE_ENTRIES];

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	struct cmp_connection out_conn;
	struct amdtp_stream tx_stream;
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;
};

/* AV/C Stream Format Information Specification 1.1 (Apr 2005, 1394TA) */
#define AVC_GENERIC_FRAME_MAXIMUM_BYTES	512
int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len,
			  unsigned int eid);
static inline int
avc_stream_get_format_single(struct fw_unit *unit,
			     enum avc_general_plug_dir dir, unsigned int pid,
			     u8 *buf, unsigned int *len)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, 0xff);
}
static inline int
avc_stream_get_format_list(struct fw_unit *unit,
			   enum avc_general_plug_dir dir, unsigned int pid,
			   u8 *buf, unsigned int *len,
			   unsigned int eid)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, eid);
}

/*
 * AV/C Digital Interface Command Set General Specification 4.2
 * (Sep 2004, 1394TA)
 */
int avc_general_inquiry_sig_fmt(struct fw_unit *unit, unsigned int rate,
				enum avc_general_plug_dir dir,
				unsigned short pid);
int snd_oxfw_get_rate(struct snd_oxfw *oxfw, unsigned int *rate,
		      enum avc_general_plug_dir dir);
int snd_oxfw_set_rate(struct snd_oxfw *oxfw, unsigned int rate,
		      enum avc_general_plug_dir dir);

/* for AMDTP streaming */
int snd_oxfw_stream_get_rate(struct snd_oxfw *oxfw, unsigned int *rate);
int snd_oxfw_stream_set_rate(struct snd_oxfw *oxfw, unsigned int rate);
int snd_oxfw_stream_init_duplex(struct snd_oxfw *oxfw);
int snd_oxfw_stream_start_duplex(struct snd_oxfw *oxfw,
				  struct amdtp_stream *stream,
				   unsigned int sampling_rate);
int snd_oxfw_stream_stop_duplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_update_duplex(struct snd_oxfw *oxfw);
void snd_oxfw_stream_destroy_duplex(struct snd_oxfw *oxfw);

int snd_oxfw_stream_discover(struct snd_oxfw *oxfw);

void snd_oxfw_stream_lock_changed(struct snd_oxfw *oxfw);
int snd_oxfw_stream_lock_try(struct snd_oxfw *oxfw);
void snd_oxfw_stream_lock_release(struct snd_oxfw *oxfw);

void snd_oxfw_proc_init(struct snd_oxfw *oxfw);

int snd_oxfw_create_midi_devices(struct snd_oxfw *oxfw);

int snd_oxfw_create_pcm_devices(struct snd_oxfw *oxfw);

int snd_oxfw_create_hwdep_device(struct snd_oxfw *oxfw);

#define SND_OXFW_DEV_ENTRY(vendor, model) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor, \
	.model_id	= model, \
}

#endif
