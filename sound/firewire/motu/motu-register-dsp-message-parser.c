// SPDX-License-Identifier: GPL-2.0-only
//
// motu-register-dsp-message-parser.c - a part of driver for MOTU FireWire series
//
// Copyright (c) 2021 Takashi Sakamoto <o-takashi@sakamocchi.jp>

// Below models allow software to configure their DSP functions by asynchronous transaction
// to access their internal registers.
// * 828 mk2
// * 896hd
// * Traveler
// * 8 pre
// * Ultralite
// * 4 pre
// * Audio Express
//
// Additionally, isochronous packets from the above models include messages to notify state of
// DSP. The messages are two set of 3 byte data in 2nd and 3rd quadlet of data block. When user
// operates hardware components such as dial and switch, corresponding messages are transferred.
// The messages include Hardware metering and MIDI messages as well.

#include "motu.h"

#define MSG_FLAG_POS                    4
#define MSG_FLAG_TYPE_MASK              0xf8
#define MSG_FLAG_MIDI_MASK              0x01
#define MSG_FLAG_MODEL_SPECIFIC_MASK    0x06
#define   MSG_FLAG_8PRE                 0x00
#define   MSG_FLAG_ULTRALITE            0x04
#define   MSG_FLAG_TRAVELER             0x04
#define   MSG_FLAG_828MK2               0x04
#define   MSG_FLAG_896HD                0x04
#define   MSG_FLAG_4PRE                 0x05 // MIDI mask is in 8th byte.
#define   MSG_FLAG_AUDIOEXPRESS         0x05 // MIDI mask is in 8th byte.
#define MSG_FLAG_TYPE_SHIFT             3
#define MSG_VALUE_POS                   5
#define MSG_MIDI_BYTE_POS		6
#define MSG_METER_IDX_POS               7

// In 4 pre and Audio express, meter index is in 6th byte. MIDI flag is in 8th byte and MIDI byte
// is in 7th byte.
#define MSG_METER_IDX_POS_4PRE_AE	6
#define MSG_MIDI_BYTE_POS_4PRE_AE	7
#define MSG_FLAG_MIDI_POS_4PRE_AE	8

enum register_dsp_msg_type {
	// Used for messages with no information.
	INVALID = 0x00,
	MIXER_SELECT = 0x01,
	MIXER_SRC_GAIN = 0x02,
	MIXER_SRC_PAN = 0x03,
	MIXER_SRC_FLAG = 0x04,
	MIXER_OUTPUT_PAIRED_VOLUME = 0x05,
	MIXER_OUTPUT_PAIRED_FLAG = 0x06,
	MAIN_OUTPUT_PAIRED_VOLUME = 0x07,
	HP_OUTPUT_PAIRED_VOLUME = 0x08,
	HP_OUTPUT_ASSIGN = 0x09,
	// Transferred by all models but the purpose is still unknown.
	UNKNOWN_0 = 0x0a,
	// Specific to 828mk2, 896hd, Traveler.
	UNKNOWN_2 = 0x0c,
	// Specific to 828mk2, Traveler, and 896hd (not functional).
	LINE_INPUT_BOOST = 0x0d,
	// Specific to 828mk2, Traveler, and 896hd (not functional).
	LINE_INPUT_NOMINAL_LEVEL = 0x0e,
	// Specific to Ultralite, 4 pre, Audio express, and 8 pre (not functional).
	INPUT_GAIN_AND_INVERT = 0x15,
	// Specific to 4 pre, and Audio express.
	INPUT_FLAG = 0x16,
	// Specific to 4 pre, and Audio express.
	MIXER_SRC_PAIRED_BALANCE = 0x17,
	// Specific to 4 pre, and Audio express.
	MIXER_SRC_PAIRED_WIDTH = 0x18,
	// Transferred by all models. This type of message interposes the series of the other
	// messages. The message delivers signal level up to 96.0 kHz. In 828mk2, 896hd, and
	// Traveler, one of physical outputs is selected for the message. The selection is done
	// by LSB one byte in asynchronous write quadlet transaction to 0x'ffff'f000'0b2c.
	METER = 0x1f,
};

struct msg_parser {
	struct snd_firewire_motu_register_dsp_meter meter;
	bool meter_pos_quirk;
};

int snd_motu_register_dsp_message_parser_new(struct snd_motu *motu)
{
	struct msg_parser *parser;
	parser = devm_kzalloc(&motu->card->card_dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return -ENOMEM;
	if (motu->spec == &snd_motu_spec_4pre || motu->spec == &snd_motu_spec_audio_express)
		parser->meter_pos_quirk = true;
	motu->message_parser = parser;
	return 0;
}

int snd_motu_register_dsp_message_parser_init(struct snd_motu *motu)
{
	return 0;
}

void snd_motu_register_dsp_message_parser_parse(struct snd_motu *motu, const struct pkt_desc *descs,
					unsigned int desc_count, unsigned int data_block_quadlets)
{
	struct msg_parser *parser = motu->message_parser;
	bool meter_pos_quirk = parser->meter_pos_quirk;
	int i;

	for (i = 0; i < desc_count; ++i) {
		const struct pkt_desc *desc = descs + i;
		__be32 *buffer = desc->ctx_payload;
		unsigned int data_blocks = desc->data_blocks;
		int j;

		for (j = 0; j < data_blocks; ++j) {
			u8 *b = (u8 *)buffer;
			u8 msg_type = (b[MSG_FLAG_POS] & MSG_FLAG_TYPE_MASK) >> MSG_FLAG_TYPE_SHIFT;
			u8 val = b[MSG_VALUE_POS];

			buffer += data_block_quadlets;

			switch (msg_type) {
			case METER:
			{
				u8 pos;

				if (!meter_pos_quirk)
					pos = b[MSG_METER_IDX_POS];
				else
					pos = b[MSG_METER_IDX_POS_4PRE_AE];

				if (pos < 0x80)
					pos &= 0x1f;
				else
					pos = (pos & 0x1f) + 20;
				parser->meter.data[pos] = val;
				break;
			}
			default:
				break;
			}
		}
	}
}
