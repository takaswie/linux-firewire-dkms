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
#include <linux/compat.h>

#include "../../../include/uapi/sound/firewire.h"
#include "../../../include/uapi/sound/asound.h"

#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/rawmidi.h>
//#include <sound/firewire.h>
#include <sound/hwdep.h>

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
	spinlock_t lock;

	struct snd_oxfw_stream_formation
		tx_stream_formations[SND_OXFW_STREAM_TABLE_ENTRIES];
	struct snd_oxfw_stream_formation
		rx_stream_formations[SND_OXFW_STREAM_TABLE_ENTRIES];
	struct cmp_connection out_conn;
	struct cmp_connection in_conn;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	bool mute;
	s16 volume[6];
	s16 volume_min;
	s16 volume_max;

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

int snd_oxfw_command_set_rate(struct snd_oxfw *oxfw,
			       enum avc_general_plug_dir dir,
			       unsigned int rate);
int snd_oxfw_command_get_rate(struct snd_oxfw *oxfw,
			       enum avc_general_plug_dir dir,
			       unsigned int *rate);

int snd_oxfw_stream_get_rate(struct snd_oxfw *oxfw, unsigned int *rate);
int snd_oxfw_stream_set_rate(struct snd_oxfw *oxfw, unsigned int rate);

int snd_oxfw_stream_start(struct snd_oxfw *oxfw,
			  struct amdtp_stream *stream, unsigned int rate);
void snd_oxfw_stream_stop(struct snd_oxfw *oxfw, struct amdtp_stream *stream);
void snd_oxfw_stream_destroy(struct snd_oxfw *oxfw,
			     struct amdtp_stream *stream);
void snd_oxfw_stream_update(struct snd_oxfw *oxfw, struct amdtp_stream *stream);

int snd_oxfw_streams_init(struct snd_oxfw *oxfw);

int firewave_stream_discover(struct snd_oxfw *oxfw);
int lacie_speakers_stream_discover(struct snd_oxfw *oxfw);
int snd_oxfw_stream_discover(struct snd_oxfw *oxfw);

void snd_oxfw_stream_lock_changed(struct snd_oxfw *oxfw);
int snd_oxfw_stream_lock_try(struct snd_oxfw *oxfw);
void snd_oxfw_stream_lock_release(struct snd_oxfw *oxfw);

int snd_oxfw_create_pcm(struct snd_oxfw *oxfw);

int snd_oxfw_create_mixer(struct snd_oxfw *oxfw);

void snd_oxfw_proc_init(struct snd_oxfw *oxfw);

int snd_oxfw_create_midi(struct snd_oxfw *oxfw);

int snd_oxfw_create_hwdep(struct snd_oxfw *oxfw);
