/*
 * bebob.h - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013 Takashi Sakamoto
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

#ifndef SOUND_BEBOB_H_INCLUDED
#define SOUND_BEBOB_H_INCLUDED

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
#include <sound/control.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../lib.h"
#include "../cmp.h"
#include "../fcp.h"

/* basic register addresses on bebob chip */
#define BEBOB_ADDR_REG_INFO	0xffffc8020000
#define BEBOB_ADDR_REG_REQ	0xffffc8021000

/*
 * Offsets from information register.
 * In detail, see struct hw_info in bebob_proc.c
 */
#define INFO_OFFSET_GUID		0x10
#define INFO_OFFSET_HW_MODEL_ID		0x18
#define INFO_OFFSET_HW_MODEL_REVISION	0x1c

/* defined later */
struct snd_bebob;

#define SND_BEBOB_STRM_FMT_ENTRIES	9
struct snd_bebob_stream_formation {
	unsigned int pcm;
	unsigned int midi;
};
/* this is a lookup table for index of stream formations */
extern const unsigned int snd_bebob_rate_table[SND_BEBOB_STRM_FMT_ENTRIES];

/* device specific operations */
struct snd_bebob_freq_spec {
	int (*get)(struct snd_bebob *bebob, int *rate);
	int (*set)(struct snd_bebob *bebob, int rate);
	/* private */
	struct snd_ctl_elem_id *ctl_id;
};
struct snd_bebob_clock_spec {
	int num;
	char **labels;
	int (*get)(struct snd_bebob *bebob, int *id);
	int (*set)(struct snd_bebob *bebob, int id);
	int (*synced)(struct snd_bebob *bebob, bool *synced);
	/* private */
	struct snd_ctl_elem_id *ctl_id;
};
struct snd_bebob_dig_iface_spec {
	int num;
	char **labels;
	int (*get)(struct snd_bebob *bebob, int *id);
	int (*set)(struct snd_bebob *bebob, int id);
};
struct snd_bebob_meter_spec {
	int num;
	char **labels;
	int (*get)(struct snd_bebob *bebob, u32 *target, int size);
};
struct snd_bebob_spec {
	int (*load)(struct fw_unit *unit,
		    const struct ieee1394_device_id *entry);
	int (*discover)(struct snd_bebob *bebob);
	int (*map)(struct snd_bebob *bebob, struct amdtp_stream *stream);
	struct snd_bebob_freq_spec *freq;
	struct snd_bebob_clock_spec *clock;
	struct snd_bebob_dig_iface_spec *dig_iface;
	struct snd_bebob_meter_spec *meter;
};

struct snd_bebob {
        struct snd_card *card;
        struct fw_device *device;
        struct fw_unit *unit;
        int card_index;

        struct mutex mutex;
        spinlock_t lock;

	const struct snd_bebob_spec *spec;

	unsigned int supported_sampling_rates;

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	struct cmp_connection out_conn;
	struct amdtp_stream tx_stream;
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;

	bool loaded;

	struct snd_bebob_stream_formation
		tx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];
	struct snd_bebob_stream_formation
		rx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];

	/* for M-Audio special devices */
	int clk_src;
	int in_dig_fmt;
	int out_dig_fmt;
	int in_dig_iface;
	int clk_lock;
	bool maudio_special_quirk;

	/* for uapi */
	int dev_lock_count;
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;
};

static inline int
snd_bebob_read_block(struct snd_bebob *bebob, u64 addr, void *buf, int size)
{
	return snd_fw_transaction(bebob->unit, TCODE_READ_BLOCK_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  buf, size, 0);
}

static inline int
snd_bebob_read_quad(struct snd_bebob *bebob, u64 addr, void *buf, int size)
{
	return snd_fw_transaction(bebob->unit, TCODE_READ_QUADLET_REQUEST,
				  BEBOB_ADDR_REG_INFO + addr,
				  buf, size, 0);
}

int snd_bebob_get_formation_index(int sampling_rate);

/* AV/C Audio Subunit Specification 1.0 (1394TA) */
int avc_audio_set_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int num);
int avc_audio_get_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int *num);

/* AV/C Digital Interface Command Set General Specification 4.2 (1394TA) */
enum avc_general_plug_dir {
	AVC_GENERAL_PLUG_DIR_IN		= 0,
	AVC_GENERAL_PLUG_DIR_OUT	= 1,
	AVC_GENERAL_PLUG_COUNT
};
int avc_general_set_sig_fmt(struct fw_unit *unit, int rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_sig_fmt(struct fw_unit *unit, int *rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_plug_info(struct fw_unit *unit,
			      unsigned short bus_plugs[AVC_GENERAL_PLUG_COUNT],
			      unsigned short ext_plugs[AVC_GENERAL_PLUG_COUNT]);

/* Connection and Compatibility Management 1.0 (1394TA) */
int avc_ccm_get_sig_src(struct fw_unit *unit,
			int *src_stype, int *src_sid, int *src_pid,
			int dst_stype, int dst_sid, int dst_pid);
int avc_ccm_set_sig_src(struct fw_unit *unit,
			int src_stype, int src_sid, int src_pid,
			int dst_stype, int dst_sid, int dst_pid);

/* Additional AVC commands, AV/C Unit and Subunit, Revision 17 (BridgeCo.) */
enum snd_bebob_plug_dir {
	SND_BEBOB_PLUG_DIR_IN	= 0x00,
	SND_BEBOB_PLUG_DIR_OUT	= 0x01
};
enum snd_bebob_plug_unit {
	SND_BEBOB_PLUG_UNIT_ISOC	= 0x00,
	SND_BEBOB_PLUG_UNIT_EXT		= 0x01,
	SND_BEBOB_PLUG_UNIT_ASYNC	= 0x02
};
enum snd_bebob_plug_type {
	SND_BEBOB_PLUG_TYPE_ISOC	= 0x00,
	SND_BEBOB_PLUG_TYPE_ASYNC	= 0x01,
	SND_BEBOB_PLUG_TYPE_MIDI	= 0x02,
	SND_BEBOB_PLUG_TYPE_SYNC	= 0x03,
	SND_BEBOB_PLUG_TYPE_ANA		= 0x04,
	SND_BEBOB_PLUG_TYPE_DIG		= 0x05
};
int avc_bridgeco_get_plug_ch_pos(struct fw_unit *unit,
				 enum snd_bebob_plug_dir pdir,
				 unsigned short pid, u8 *buf, int len);
int avc_bridgeco_get_plug_type(struct fw_unit *unit,
			       enum snd_bebob_plug_dir pdir,
			       enum snd_bebob_plug_unit punit,
			       unsigned short p_id,
			       enum snd_bebob_plug_type *type);
int avc_bridgeco_get_plug_cluster_type(struct fw_unit *unit,
				       enum snd_bebob_plug_dir pdir,
				       int pid, int cluster_id, u8 *ctype);
int avc_bridgeco_get_plug_strm_fmt(struct fw_unit *unit,
				   enum snd_bebob_plug_dir pdir,
				   unsigned short pid,
				   int entryid, u8 *buf, int *len);
int snd_bebob_stream_get_rate(struct snd_bebob *bebob, int *rate);
int snd_bebob_stream_set_rate(struct snd_bebob *bebob, int rate);
int snd_bebob_stream_discover(struct snd_bebob *bebob);
int snd_bebob_stream_map(struct snd_bebob *bebob,
			 struct amdtp_stream *stream);
int snd_bebob_stream_init_duplex(struct snd_bebob *bebob);
int snd_bebob_stream_start_duplex(struct snd_bebob *bebob,
				  struct amdtp_stream *stream,
				   unsigned int sampling_rate);
int snd_bebob_stream_stop_duplex(struct snd_bebob *bebob);
void snd_bebob_stream_update_duplex(struct snd_bebob *bebob);
void snd_bebob_stream_destroy_duplex(struct snd_bebob *bebob);
void snd_bebob_stream_lock_changed(struct snd_bebob *bebob);
int snd_bebob_stream_lock_try(struct snd_bebob *bebob);
void snd_bebob_stream_lock_release(struct snd_bebob *bebob);

/* for hwdep component */
int snd_bebob_create_hwdep_device(struct snd_bebob *bebob);

int snd_bebob_create_pcm_devices(struct snd_bebob *bebob);

int snd_bebob_create_control_devices(struct snd_bebob *bebob);

int snd_bebob_create_midi_devices(struct snd_bebob *bebob);

void snd_bebob_proc_init(struct snd_bebob *bebob);

/* device specific operations */
extern struct snd_bebob_spec maudio_bootloader_spec;
extern struct snd_bebob_spec maudio_fw1814_spec;
extern struct snd_bebob_spec maudio_projectmix_spec;
extern struct snd_bebob_spec maudio_nrv10_spec;
extern struct snd_bebob_spec maudio_fw410_spec;
extern struct snd_bebob_spec maudio_audiophile_spec;
extern struct snd_bebob_spec maudio_solo_spec;
extern struct snd_bebob_spec maudio_ozonic_spec;
extern struct snd_bebob_spec yamaha_go_spec;
extern struct snd_bebob_spec saffirepro_26_spec;
extern struct snd_bebob_spec saffirepro_10_spec;
extern struct snd_bebob_spec saffire_le_spec;
extern struct snd_bebob_spec saffire_spec;
extern struct snd_bebob_spec phase88_rack_spec;
extern struct snd_bebob_spec phase24_series_spec;

#define SND_BEBOB_DEV_ENTRY(vendor, model, private_data) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor, \
	.model_id	= model, \
	.driver_data	= (kernel_ulong_t)&private_data \
}

#endif
