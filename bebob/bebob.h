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

struct snd_bebob {
        struct snd_card *card;
        struct fw_device *device;
        struct fw_unit *unit;
        int card_index;

        struct mutex mutex;
        spinlock_t lock;

	unsigned int supported_sampling_rates;

	unsigned int pcm_capture_channels;
	unsigned int pcm_playback_channels;

	unsigned int midi_input_ports;
	unsigned int midi_output_ports;

	struct amdtp_stream receive_stream;
	struct amdtp_stream transmit_stream;

	struct cmp_connection output_connection;
	struct cmp_connection input_connection;

	bool loaded;
};

int snd_bebob_stream_init(struct snd_bebob *bebob, struct amdtp_stream *stream);
int snd_bebob_stream_start(struct snd_bebob *bebob, struct amdtp_stream *stream);
void snd_bebob_stream_stop(struct snd_bebob *bebob, struct amdtp_stream *stream);
void snd_bebob_stream_destroy(struct snd_bebob *bebob, struct amdtp_stream *stream);

void snd_bebob_destroy_pcm_devices(struct snd_bebob *bebob);
int snd_bebob_create_pcm_devices(struct snd_bebob *bebob);

int snd_bebob_maudio_detect(struct fw_unit *unit);

#endif
