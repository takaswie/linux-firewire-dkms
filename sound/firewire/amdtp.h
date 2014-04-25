#ifndef SOUND_FIREWIRE_AMDTP_H_INCLUDED
#define SOUND_FIREWIRE_AMDTP_H_INCLUDED

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <sound/asound.h>
#include "packets-buffer.h"

/**
 * enum cip_flags - describes details of the streaming protocol
 * @CIP_NONBLOCKING: In non-blocking mode, each packet contains
 *	sample_rate/8000 samples, with rounding up or down to adjust
 *	for clock skew and left-over fractional samples.  This should
 *	be used if supported by the device.
 * @CIP_BLOCKING: In blocking mode, each packet contains either zero or
 *	SYT_INTERVAL samples, with these two types alternating so that
 *	the overall sample rate comes out right.
 * @CIP_HI_DUALWIRE: At rates above 96 kHz, pretend that the stream runs
 *	at half the actual sample rate with twice the number of channels;
 *	two samples of a channel are stored consecutively in the packet.
 *	Requires blocking mode and SYT_INTERVAL-aligned PCM buffer size.
 */
enum cip_flags {
	CIP_NONBLOCKING	= 0x00,
	CIP_BLOCKING	= 0x01,
	CIP_HI_DUALWIRE	= 0x02,
};

/**
 * enum cip_sfc - a stream's sample rate
 */
enum cip_sfc {
	CIP_SFC_32000  = 0,
	CIP_SFC_44100  = 1,
	CIP_SFC_48000  = 2,
	CIP_SFC_88200  = 3,
	CIP_SFC_96000  = 4,
	CIP_SFC_176400 = 5,
	CIP_SFC_192000 = 6,
	CIP_SFC_COUNT
};

#define AMDTP_IN_PCM_FORMAT_BITS	SNDRV_PCM_FMTBIT_S32

#define AMDTP_OUT_PCM_FORMAT_BITS	(SNDRV_PCM_FMTBIT_S16 | \
					 SNDRV_PCM_FMTBIT_S32)


/*
 * AMDTP packet can include channels for MIDI conformant data.
 * Each MIDI conformant data channel includes 8 MPX-MIDI data stream.
 * Each MPX-MIDI data stream includes one data stream from/to MIDI ports.
 *
 * This module supports maximum 1 MIDI conformant data channels.
 * Then this AMDTP packets can transfer maximum 8 MIDI data streams.
 */
#define AMDTP_MAX_CHANNELS_FOR_MIDI	1

struct fw_unit;
struct fw_iso_context;
struct snd_pcm_substream;
struct snd_rawmidi_substream;

enum amdtp_stream_direction {
	AMDTP_OUT_STREAM = 0,
	AMDTP_IN_STREAM
};

struct amdtp_stream {
	struct fw_unit *unit;
	enum cip_flags flags;
	enum amdtp_stream_direction direction;
	struct fw_iso_context *context;
	struct mutex mutex;

	enum cip_sfc sfc;
	bool dual_wire;
	unsigned int data_block_quadlets;
	unsigned int pcm_channels;
	unsigned int midi_ports;
	void (*transfer_samples)(struct amdtp_stream *s,
				 struct snd_pcm_substream *pcm,
				 __be32 *buffer, unsigned int frames);

	unsigned int syt_interval;
	unsigned int transfer_delay;
	unsigned int source_node_id_field;
	struct iso_packets_buffer buffer;

	struct snd_pcm_substream *pcm;
	struct tasklet_struct period_tasklet;

	int packet_index;
	unsigned int data_block_counter;

	unsigned int data_block_state;

	unsigned int last_syt_offset;
	unsigned int syt_offset_state;

	unsigned int pcm_buffer_pointer;
	unsigned int pcm_period_pointer;
	bool pointer_flush;

	struct snd_rawmidi_substream *midi[AMDTP_MAX_CHANNELS_FOR_MIDI * 8];
};

int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		      enum amdtp_stream_direction dir,
		      enum cip_flags flags);
void amdtp_stream_destroy(struct amdtp_stream *s);

void amdtp_stream_set_parameters(struct amdtp_stream *s,
				 unsigned int rate,
				 unsigned int pcm_channels,
				 unsigned int midi_ports);
unsigned int amdtp_stream_get_max_payload(struct amdtp_stream *s);

int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed);
void amdtp_stream_update(struct amdtp_stream *s);
void amdtp_stream_stop(struct amdtp_stream *s);

void amdtp_stream_set_pcm_format(struct amdtp_stream *s,
				 snd_pcm_format_t format);
void amdtp_stream_pcm_prepare(struct amdtp_stream *s);
unsigned long amdtp_stream_pcm_pointer(struct amdtp_stream *s);
void amdtp_stream_pcm_abort(struct amdtp_stream *s);

extern const unsigned int amdtp_syt_intervals[CIP_SFC_COUNT];

/**
 * amdtp_stream_running - check stream is running or not
 * @s: the AMDTP stream
 *
 * If this function returns true, the stream is running.
 */
static inline bool amdtp_stream_running(struct amdtp_stream *s)
{
	return !IS_ERR(s->context);
}

/**
 * amdtp_streaming_error - check for streaming error
 * @s: the AMDTP stream
 *
 * If this function returns true, the stream's packet queue has stopped due to
 * an asynchronous error.
 */
static inline bool amdtp_streaming_error(struct amdtp_stream *s)
{
	return s->packet_index < 0;
}

/**
 * amdtp_stream_pcm_running - check PCM substream is running or not
 * @s: the AMDTP stream
 *
 * If this function returns true, PCM substream in the AMDTP stream is running.
 */
static inline bool amdtp_stream_pcm_running(struct amdtp_stream *s)
{
	return !!s->pcm;
}

/**
 * amdtp_stream_pcm_trigger - start/stop playback from a PCM device
 * @s: the AMDTP stream
 * @pcm: the PCM device to be started, or %NULL to stop the current device
 *
 * Call this function on a running isochronous stream to enable the actual
 * transmission of PCM data.  This function should be called from the PCM
 * device's .trigger callback.
 */
static inline void amdtp_stream_pcm_trigger(struct amdtp_stream *s,
					    struct snd_pcm_substream *pcm)
{
	ACCESS_ONCE(s->pcm) = pcm;
}

/**
 * amdtp_stream_midi_trigger - start/stop playback/capture with a MIDI device
 * @s: the AMDTP stream
 * @port: index of MIDI port
 * @midi: the MIDI device to be started, or %NULL to stop the current device
 *
 * Call this function on a running isochronous stream to enable the actual
 * transmission of MIDI data.  This function should be called from the MIDI
 * device's .trigger callback.
 */
static inline void amdtp_stream_midi_trigger(struct amdtp_stream *s,
					     unsigned int port,
					     struct snd_rawmidi_substream *midi)
{
	if (port < s->midi_ports)
		ACCESS_ONCE(s->midi[port]) = midi;
}

static inline bool cip_sfc_is_base_44100(enum cip_sfc sfc)
{
	return sfc & 1;
}

#endif
