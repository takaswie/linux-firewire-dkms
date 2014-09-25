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
#include <sound/info.h>
#include <sound/rawmidi.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>

#include "digimagic.h"
#include "../packets-buffer.h"
#include "../iso-resources.h"
#include "../lib.h"

#define SND_DG00X_ADDR_BASE	0xffffe0000000
#define UNKNOWN0	0x0000	/* Streaming status change? */
#define UNKNOWN1	0x0004	/* Streaming control */
#define UNKNOWN2	0x0008	/* Node address 1 higher */
#define UNKNOWN3	0x000c	/* Node address 1 lower */
#define UNKNOWN4	0x0010	/* Streaming control? */
#define UNKNOWN5	0x0014	/* Node address 2 higher */
#define UNKNOWN6	0x0018	/* Node address 2 lower */

#define SND_DG00X_OFFSET_UNKNOWN0	0x0100	/* Unknown */
#define SND_DG00X_OFFSET_UNKNOWN1	0x0110	/* Current sampling rate */
#define SND_DG00X_OFFSET_UNKNOWN2	0x0118	/* Current source of clock */
#define SND_DG00X_OFFSET_UNKNOWN3	0x011c	/* 0x00 */
#define SND_DG00X_OFFSET_UNKNOWN4	0x0120	/* 0x03 */
#define SND_DG00X_OFFSET_UNKNOWN5	0x0124	/* 0x00/0x01 */

/* DSP control: 0x0300 ~ 0x038c */

struct snd_dg00x {
	struct snd_card *card;
	struct fw_unit *unit;
	int card_index;

	struct mutex mutex;
	spinlock_t lock;

	/* TODO? */
	struct mutex mutexhw;
	spinlock_t lockhw;

	/* Handle heartbeat */
	struct fw_address_handler heartbeat_handler;

	/* for IEC 61883-1 and -6 streaming
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	atomic_t capture_substreams;
	atomic_t playback_substreams;
	*/

	/* Digidesign 003 needs hardcoded iso stream numbers */
	struct fw_iso_resources iso_rx;
	struct fw_iso_resources iso_tx;

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
};

/* values for SND_DG00X_ADDR_OFFSET_CLOCK */
enum snd_dg00x_clock {
	SND_DG00X_CLOCK_INTERNAL = 0,
	SND_DG00X_CLOCK_SPDIF,
	SND_DG00X_CLOCK_ADAT,
	SND_DG00X_CLOCK_WORD,
};

/*
int snd_dg00x_get_rate(struct snd_dg00x *dg00x, unsigned int *rate);
int snd_dg00x_set_rate(struct snd_dg00x *dg00x, unsigned int rate);
int snd_dg00x_get_clock(struct snd_dg00x *dg00x, enum snd_dg00x_clock *clock);

int snd_dg00x_stream_init_duplex(struct snd_dg00x *dg00x);
int snd_dg00x_stream_start_duplex(struct snd_dg00x *dg00x,
				  struct amdtp_stream *request,
				  int sampling_rate);
int snd_dg00x_stream_stop_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_update_duplex(struct snd_dg00x *dg00x);
void snd_dg00x_stream_destroy_duplex(struct snd_dg00x *dg00x);

void snd_dg00x_stream_lock_changed(struct snd_dg00x *dg00x);
int snd_dg00x_stream_lock_try(struct snd_dg00x *dg00x);
void snd_dg00x_stream_lock_release(struct snd_dg00x *dg00x);

void snd_dg00x_proc_init(struct snd_dg00x *dg00x);

int snd_dg00x_create_midi_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_pcm_devices(struct snd_dg00x *dg00x);

int snd_dg00x_create_hwdep_device(struct snd_dg00x *dg00x);
*/
#endif
