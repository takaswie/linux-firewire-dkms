/*
 * digi00x-dot.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2015 Takashi Sakamoto
 * Copyright (C) 2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Damien Zammit <damien@zamaudio.com>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * The double-oh-three algorism is invented by Robin Gareus and Damien Zammit
 * in 2012, with reverse-engineering of Digi 003 Rack.
 */

#include "digi00x.h"

struct dot_state {
	__u8 carry;
	__u8 idx;
	unsigned int off;
};

#define BYTE_PER_SAMPLE (4)
#define MAGIC_DOT_BYTE (2)

#define MAGIC_BYTE_OFF(x) (((x) * BYTE_PER_SAMPLE ) + MAGIC_DOT_BYTE)

/*
 * double-oh-three look up table
 *
 * @param idx index byte (audio-sample data) 0x00..0xff
 * @param off channel offset shift
 * @return salt to XOR with given data
 */
static const __u8 dot_scrt(const __u8 idx, const unsigned int off) {
	/*
	 * the length of the added pattern only depends on the lower nibble
	 * of the last non-zero data
	 */
	const __u8 len[16] = {0, 1, 3, 5, 7, 9, 11, 13, 14, 12, 10, 8, 6, 4, 2,
			      0};

	/*
	 * the lower nibble of the salt. Interleaved sequence.
	 * this is walked backwards according to len[]
	 */
	const __u8 nib[15] = {0x8, 0x7, 0x9, 0x6, 0xa, 0x5, 0xb, 0x4, 0xc, 0x3,
			      0xd, 0x2, 0xe, 0x1, 0xf};

	/* circular list for the salt's hi nibble. */
	const __u8 hir[15] = {0x0, 0x6, 0xf, 0x8, 0x7, 0x5, 0x3, 0x4, 0xc, 0xd,
			      0xe, 0x1, 0x2, 0xb, 0xa};

	/*
	 * start offset for upper nibble mapping.
	 * note: 9 is /special/. In the case where the high nibble == 0x9,
	 * hir[] is not used and - coincidentally - the salt's hi nibble is
	 * 0x09 regardless of the offset.
	 */
	const __u8 hio[16] = {0, 11, 12, 6, 7, 5, 1, 4, 3, 0x00, 14, 13, 8, 9,
			      10, 2};

	const __u8 ln = idx & 0xf;
	const __u8 hn = (idx >> 4) & 0xf;
	const __u8 hr = (hn == 0x9) ? 0x9 : hir[(hio[hn] + off) % 15];

	if (len[ln] < off)
		return 0x00;

	return ((nib[14 + off - len[ln]]) | (hr << 4));
}

static inline void dot_state_reset(struct dot_state *state)
{
	state->carry = 0x00;
	state->idx   = 0x00;
	state->off   = 0;
}

static void dot_encode_step(struct dot_state *state, __be32 *const buffer)
{
	__u8 * const data = (__u8*) buffer;

	if (data[MAGIC_DOT_BYTE] != 0x00) {
		state->off = 0;
		state->idx = data[MAGIC_DOT_BYTE] ^ state->carry;
	}
	data[MAGIC_DOT_BYTE] ^= state->carry;
	state->carry = dot_scrt(state->idx, ++(state->off));
}

void double_oh_three_write_s32(struct amdtp_stream *s,
			       struct snd_pcm_substream *pcm,
			       __be32 *buffer, unsigned int frames)
{
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u32 *src;
	static struct dot_state state;

	channels = s->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	for (i = 0; i < frames; ++i) {
		dot_state_reset(&state);

		for (c = 0; c < channels; ++c) {
			buffer[s->pcm_positions[c]] =
					cpu_to_be32((*src >> 8) | 0x40000000);
			dot_encode_step(&state, &buffer[s->pcm_positions[c]]);
			src++;
		}

		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}
