#ifndef SOUND_BEBOB_H_INCLUDED
#define SOUND_BEBOB_H_INCLUDED

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
#include "../lib.h"
#include "../cmp.h"
#include "../fcp.h"

/* basic register addresses on bebob chip */
#define BEBOB_ADDR_REG_INFO	0xffffc8020000
#define BEBOB_ADDR_REG_REQ	0xffffc8021000

/* offsets of information register */
#define REG_INFO_SIZE				0x68
#define REG_INFO_OFFSET_MANUFACTURER_NAME	0x00
#define REG_INFO_OFFSET_PROTOCOL_VERSION	0x08
#define REG_INFO_OFFSET_BOOTLOADER_VERSION	0x0c
#define REG_INFO_OFFSET_GUID			0x10
#define REG_INFO_OFFSET_HW_MODEL_ID		0x18
#define REG_INFO_OFFSET_HW_MODEL_REVISION	0x1c
#define REG_INFO_OFFSET_FW_DATE			0x20
#define REG_INFO_OFFSET_FW_TIME			0x28
#define REG_INFO_OFFSET_FW_ID			0x30
#define REG_INFO_OFFSET_FW_VERSION		0x34
#define REG_INFO_OFFSET_BASE_ADDRESS		0x38
#define REG_INFO_OFFSET_MAX_IMAGE_LENGTH	0x3c
#define REG_INFO_OFFSET_BOOTLOADER_DATE		0x40
#define REG_INFO_OFFSET_BOOTLOADER_TIME		0x48
#define REG_INFO_OFFSET_DEBUGGER_DATE		0x50
#define REG_INFO_OFFSET_DEBUGGER_TIME		0x58
#define REG_INFO_OFFSET_DEBUGGER_ID		0x60
#define REG_INFO_OFFSET_DEBUGGER_VERSION	0x64

/* contents of information register */
struct snd_bebob_device_info {
	__be64 manufacturer;
	__be32 protocol_version;
	__be32 bld_version;
	__be64 guid;
	__be32 model_id;
	__be32 model_revision;
	__be64 fw_date;
	__be64 fw_time;
	__be32 fw_id;
	__be32 fw_version;
	__be32 base_address;
	__be32 maximum_size;
	__be64 bld_date;
	__be64 bld_time;
	__be64 dbg_date;
	__be64 dbg_time;
	__be32 dbg_id;
	__be32 dbg_version;
} __attribute__((packed));

/* defined later */
struct snd_bebob;

/* for caching entries of stream formation */
#define SND_BEBOB_STREAM_FORMATION_ENTRIES	9
struct snd_bebob_stream_formation {
	unsigned int sampling_rate;
	unsigned int pcm;
	unsigned int midi;
	u8 entry[64];	/* '64' is arbitrary number but enough */
};
/* this is a lookup table for index of stream formations */
extern int sampling_rate_table[SND_BEBOB_STREAM_FORMATION_ENTRIES];

/* device specific operations */
struct snd_bebob_clock_spec {
	int num;
	char **labels;
	int (*get)(struct snd_bebob *bebob, int *id);
	int (*set)(struct snd_bebob *bebob, int id);
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
	int (*load)(struct snd_bebob *bebob);
	int (*discover)(struct snd_bebob *bebob);
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
		tx_stream_formations[SND_BEBOB_STREAM_FORMATION_ENTRIES];
	struct snd_bebob_stream_formation
		rx_stream_formations[SND_BEBOB_STREAM_FORMATION_ENTRIES];

	/* MAudio specific */
	int clk_src;
	int in_dig_fmt;
	int out_dig_fmt;
	int in_dig_iface;
	int out_dig_iface;
	int clk_lock;
};

int snd_bebob_get_formation_index(int sampling_rate);

int avc_audio_set_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int number);
int avc_audio_get_selector(struct fw_unit *unit, int subunit_id,
			   int fb_id, int *number);

int avc_generic_set_sampling_rate(struct fw_unit *unit, int rate,
				int direction, unsigned short plug);
int avc_generic_get_sampling_rate(struct fw_unit *unit, int *rate,
				int direction, unsigned short plug);
int avc_generic_get_plug_info(struct fw_unit *unit,
				unsigned short bus_plugs[2],
				unsigned short ext_plugs[2]);
int avc_bridgeco_get_plug_channels(struct fw_unit *unit, int direction,
				unsigned short plugid, int *channels);
int avc_bridgeco_get_plug_channel_position(struct fw_unit *unit, int direction,
				unsigned short plugid, u8 *position);
int avc_bridgeco_get_plug_type(struct fw_unit *unit, int direction,
				unsigned short plugid, int *type);
int avc_bridgeco_get_plug_cluster_type(struct fw_unit *unit, int direction,
				int plugid, int cluster_id, u8 *format);
int avc_bridgeco_get_plug_stream_formation_entry(struct fw_unit *unit,
				int direction, unsigned short plugid,
				int entryid, u8 *buf, int *len);

int snd_bebob_stream_get_formation_index(int sampling_rate);
int snd_bebob_stream_init(struct snd_bebob *bebob,
			  struct amdtp_stream *stream);
int snd_bebob_stream_start(struct snd_bebob *bebob,
			   struct amdtp_stream *stream,
			   unsigned int sampling_rate);
void snd_bebob_stream_stop(struct snd_bebob *bebob,
			   struct amdtp_stream *stream);
void snd_bebob_stream_destroy(struct snd_bebob *bebob,
			      struct amdtp_stream *stream);

void snd_bebob_destroy_pcm_devices(struct snd_bebob *bebob);
int snd_bebob_create_pcm_devices(struct snd_bebob *bebob);

int snd_bebob_create_control_devices(struct snd_bebob *bebob);

void snd_bebob_create_midi_devices(struct snd_bebob *bebob);

void snd_bebob_proc_init(struct snd_bebob *bebob);

int snd_bebob_discover(struct snd_bebob *bebob);

/* device specific operations */
extern struct snd_bebob_spec maudio_bootloader_spec;
extern struct snd_bebob_spec maudio_projectmix_spec;
extern struct snd_bebob_spec maudio_fw1814_spec;
extern struct snd_bebob_spec maudio_nrv10_spec;
extern struct snd_bebob_spec maudio_fw410_spec;
extern struct snd_bebob_spec maudio_audiophile_spec;
extern struct snd_bebob_spec maudio_solo_spec;
extern struct snd_bebob_spec maudio_ozonic_spec;

extern struct snd_bebob_spec yamaha_go_spec;

#endif
