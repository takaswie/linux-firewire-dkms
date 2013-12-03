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
#include <sound/info.h>
#include <sound/control.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/hwdep.h>

#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../amdtp.h"
#include "../lib.h"
#include "../cmp.h"
#include "../fcp.h"

/* basic register addresses on bebob chip */
#define BEBOB_ADDR_REG_INFO	0xffffc8020000
#define BEBOB_ADDR_REG_REQ	0xffffc8021000

struct snd_bebob;

#define SND_BEBOB_STRM_FMT_ENTRIES	9
struct snd_bebob_stream_formation {
	unsigned int pcm;
	unsigned int midi;
};
/* this is a lookup table for index of stream formations */
extern const unsigned int snd_bebob_rate_table[SND_BEBOB_STRM_FMT_ENTRIES];

/* device specific operations */
#define SND_BEBOB_CLOCK_INTERNAL	"Internal"
struct snd_bebob_clock_spec {
	unsigned int num;
	char **labels;
	int (*get_src)(struct snd_bebob *bebob, unsigned int *id);
	int (*set_src)(struct snd_bebob *bebob, unsigned int id);
	int (*get_freq)(struct snd_bebob *bebob, unsigned int *rate);
	int (*set_freq)(struct snd_bebob *bebob, unsigned int rate);
	int (*synced)(struct snd_bebob *bebob, bool *synced);
	/* private */
	struct snd_ctl_elem_id *ctl_id_src;
	struct snd_ctl_elem_id *ctl_id_freq;
	struct snd_ctl_elem_id *ctl_id_synced;
};
struct snd_bebob_meter_spec {
	unsigned int num;
	char **labels;
	int (*get)(struct snd_bebob *bebob, u32 *target, unsigned int size);
};
struct snd_bebob_spec {
	int (*load)(struct fw_unit *unit,
		    const struct ieee1394_device_id *entry);
	struct snd_bebob_clock_spec *clock;
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

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	struct cmp_connection out_conn;
	struct amdtp_stream tx_stream;
	struct cmp_connection in_conn;
	struct amdtp_stream rx_stream;

	struct snd_bebob_stream_formation
		tx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];
	struct snd_bebob_stream_formation
		rx_stream_formations[SND_BEBOB_STRM_FMT_ENTRIES];

	int sync_input_plug;

        /* for uapi */
        int dev_lock_count;
        bool dev_lock_changed;
        wait_queue_head_t hwdep_wait;

	/* for M-Audio special devices */
	bool maudio_special_quirk;
	bool maudio_is1814;
	unsigned int clk_src;
	unsigned int dig_in_iface;
	unsigned int dig_in_fmt;
	unsigned int dig_out_fmt;
	unsigned int clk_lock;
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

/* AV/C Digital Interface Command Set General Specification 4.2 (1394TA) */
int avc_general_get_plug_info(struct fw_unit *unit, unsigned int addr_mode,
			      u8 info[4]);

/* AV/C Audio Subunit Specification 1.0 (1394TA) */
int avc_audio_set_selector(struct fw_unit *unit, unsigned int subunit_id,
			   unsigned int fb_id, unsigned int num);
int avc_audio_get_selector(struct fw_unit *unit,unsigned  int subunit_id,
			   unsigned int fb_id, unsigned int *num);

/* AVC command extensions, AV/C Unit and Subunit, Revision 17 (BridgeCo.) */
enum snd_bebob_plug_dir {
	SND_BEBOB_PLUG_DIR_IN	= 0x00,
	SND_BEBOB_PLUG_DIR_OUT	= 0x01
};
enum snd_bebob_plug_mode {
	SND_BEBOB_PLUG_MODE_UNIT		= 0x00,
	SND_BEBOB_PLUG_MODE_SUBUNIT		= 0x01,
	SND_BEBOB_PLUG_MODE_FUNCTION_BLOCK	= 0x02
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
static inline void
avc_bridgeco_fill_unit_addr(u8 buf[6],
			    enum snd_bebob_plug_dir dir,
			    enum snd_bebob_plug_unit unit,
			    unsigned int pid)
{
	buf[0] = 0xff;	/* Unit */
	buf[1] = dir;
	buf[2] = SND_BEBOB_PLUG_MODE_UNIT;
	buf[3] = unit;
	buf[4] = 0xff & pid;
	buf[5] = 0xff;	/* reserved */
}
static inline void
avc_bridgeco_fill_subunit_addr(u8 buf[6], unsigned int mode,
			       enum snd_bebob_plug_dir dir,
			       unsigned int pid)
{
	buf[0] = 0xff & mode;	/* Subunit */
	buf[1] = dir;
	buf[2] = SND_BEBOB_PLUG_MODE_SUBUNIT;
	buf[3] = 0xff & pid;
	buf[4] = 0xff;	/* reserved */
	buf[5] = 0xff;	/* reserved */
}
int avc_bridgeco_get_plug_ch_pos(struct fw_unit *unit, u8 add[6],
				 u8 *buf, unsigned int len);
int avc_bridgeco_get_plug_type(struct fw_unit *unit, u8 addr[6],
			       enum snd_bebob_plug_type *type);
int avc_bridgeco_get_plug_cluster_type(struct fw_unit *unit, u8 addr[6],
				       unsigned int cluster_id, u8 *ctype);
int avc_bridgeco_get_plug_input(struct fw_unit *unit, u8 addr[6],
				u8 input[7]);
int avc_bridgeco_get_plug_strm_fmt(struct fw_unit *unit, u8 addr[6],
				   unsigned int entryid, u8 *buf,
				   unsigned int *len);
int avc_bridgeco_detect_plug_strm(struct fw_unit *unit,
				  enum snd_bebob_plug_dir dir,
				  unsigned int ext_pid,
				  unsigned int *detect);

int snd_bebob_get_rate(struct snd_bebob *bebob, unsigned int *rate,
                       enum avc_general_plug_dir dir);
int snd_bebob_set_rate(struct snd_bebob *bebob, unsigned int rate,
                       enum avc_general_plug_dir dir);

/* for AMDTP streaming */
int snd_bebob_stream_get_rate(struct snd_bebob *bebob, unsigned int *rate);
int snd_bebob_stream_set_rate(struct snd_bebob *bebob, unsigned int rate);
int snd_bebob_stream_check_internal_clock(struct snd_bebob *bebob,
					  bool *internal);
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

void snd_bebob_proc_init(struct snd_bebob *bebob);

int snd_bebob_create_control_devices(struct snd_bebob *bebob);

int snd_bebob_create_midi_devices(struct snd_bebob *bebob);

int snd_bebob_create_pcm_devices(struct snd_bebob *bebob);

int snd_bebob_create_hwdep_device(struct snd_bebob *bebob);

/* device specific operations */
int snd_bebob_maudio_special_discover(struct snd_bebob *bebob, bool is1814);
int snd_bebob_maudio_special_add_controls(struct snd_bebob *bebob);

extern struct snd_bebob_spec maudio_bootloader_spec;
extern struct snd_bebob_spec maudio_special_spec;
extern struct snd_bebob_spec maudio_nrv10_spec;
extern struct snd_bebob_spec maudio_fw410_spec;
extern struct snd_bebob_spec maudio_audiophile_spec;
extern struct snd_bebob_spec maudio_solo_spec;
extern struct snd_bebob_spec maudio_ozonic_spec;
extern struct snd_bebob_spec saffirepro_26_spec;
extern struct snd_bebob_spec saffirepro_10_spec;
extern struct snd_bebob_spec saffire_le_spec;
extern struct snd_bebob_spec saffire_spec;
extern struct snd_bebob_spec phase88_rack_spec;
extern struct snd_bebob_spec phase24_series_spec;
extern struct snd_bebob_spec yamaha_go_spec;

#define SND_BEBOB_DEV_ENTRY(vendor, model, private_data) \
{ \
	.match_flags	= IEEE1394_MATCH_VENDOR_ID | \
			  IEEE1394_MATCH_MODEL_ID, \
	.vendor_id	= vendor, \
	.model_id	= model, \
	.driver_data	= (kernel_ulong_t)&private_data \
}

#endif
