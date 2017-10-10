/*
 * mln-protocol-v3.c - a part of driver for Yamaha MLN3 board module.
 *
 * Copyright (c) 2017-2018 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "mln.h"

#define V3_BASE_ADDR	0xffffe0000000ll

static void parse_string(char *buf, unsigned int size, __be32 *quads,
			 unsigned int count)
{
	char *bytes = (char *)quads;
	unsigned int length;
	int i;

	length = *bytes;
	if (length == 0 || length >= size || length >= sizeof(*quads) * count)
		return;
	memset(buf, 0x00, length + 1);
	++bytes;

	for (i = 0; i < length; ++i) {
		if (bytes[i] == 0x00)
			break;
		*buf = bytes[i];
		++buf;
	}
}

static void dump_section_a(struct snd_mln *mln, struct snd_info_buffer *buffer,
			   u32 offset, u32 length)
{
	union {
		struct {
			__be32 unknown1;
			__be32 offset;
			__be32 size;
		} subsection_data;
		struct {
			__be16 unknown1;
			__be16 size;
			__be32 unknown2;
		} subsection_header;
		__be32 data[16];
	} buf;
	u64 offset_end;
	unsigned int index;
	int err;

	/* TODO: investigate/describe meaning of the 68 bytes. */
	offset += 68;
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, &buf,
				 sizeof(buf.subsection_data), 0);
	if (err < 0)
		return;
	offset += sizeof(buf.subsection_data);

	/* TODO: investigate/describe meaning of the 20 bytes. */
	offset += 20;
	offset_end = offset + be32_to_cpu(buf.subsection_data.size);

	index = 0;
	while (offset < offset_end) {
		unsigned int size;
		int i;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, &buf,
					 sizeof(buf.subsection_header), 0);
		if (err < 0)
			break;
		offset += sizeof(buf.subsection_header);

		size = be16_to_cpu(buf.subsection_header.size);
		if (size == 0 || size > sizeof(buf))
			break;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, &buf, size, 0);
		if (err < 0)
			break;

		snd_iprintf(buffer, "    entry %u (0x%08x):\n", index, offset);

		offset += size;

		/* TODO: investigate/describe meaning of the content. */
		for (i = 0; i < size / 4; ++i) {
			snd_iprintf(buffer, "      %02u: %08x\n",
				    i, be32_to_cpu(buf.data[i]));
		}

		++index;
	}
}

static void dump_b1_subsection(struct snd_mln *mln,
			       struct snd_info_buffer *buffer, u32 offset,
			       u32 length)
{
	__be32 data[16];
	char label[64];
	unsigned int entry_count;
	int i;
	int err;

	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data,
				 sizeof(data[0]), 0);
	if (err < 0)
		return;
	offset += sizeof(data[0]);

	entry_count = be32_to_cpu(data[0]);

	for (i = 0; i < entry_count; ++i) {
		int j;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, data,
					 sizeof(data[0]), 0);
		if (err < 0)
			break;
		offset += sizeof(data[0]);

		snd_iprintf(buffer, "    entry %u:\n", i);

		if (i > 0) {
			u32 label_offset = be32_to_cpu(data[0]);
			err = snd_fw_transaction(mln->unit,
						 TCODE_READ_BLOCK_REQUEST,
						 V3_BASE_ADDR + label_offset,
						 data, sizeof(data[0]) * 9, 0);
			if (err < 0)
				break;
			parse_string(label, sizeof(label), data, 9);
			snd_iprintf(buffer, "      label: %s\n", label);
		}

		for (j = 0; j < 5; ++j) {
			unsigned int size;
			int k;

			err = snd_fw_transaction(mln->unit,
						 TCODE_READ_BLOCK_REQUEST,
						 V3_BASE_ADDR + offset, data,
						 sizeof(data[0]), 0);
			if (err < 0)
				break;

			size = be32_to_cpu(data[0]);
			if (size == 0 || size > sizeof(data))
				break;

			snd_iprintf(buffer, "      data %u (0x%08x):\n",
				    j, offset);

			offset += sizeof(data[0]);

			/* TODO: investigate/describe meaning of the contents. */
			err = snd_fw_transaction(mln->unit,
						 TCODE_READ_BLOCK_REQUEST,
						 V3_BASE_ADDR + offset,
						 data, size, 0);
			if (err < 0)
				break;
			offset += size;

			for (k = 0; k < size / 4; ++k) {
				snd_iprintf(buffer,
					    "        %02u: %08x\n",
					    k, be32_to_cpu(data[k]));
			}
		}
	}
}

static void dump_b2_subsection(struct snd_mln *mln,
			       struct snd_info_buffer *buffer, u32 offset,
			       u32 length)
{
	__be32 data[12];
	unsigned int entry_count;
	int i;
	int err;

	entry_count = length / sizeof(data);

	for (i = 0; i < entry_count; ++i) {
		int j;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, data,
					 sizeof(data), 0);
		if (err < 0)
			break;

		snd_iprintf(buffer, "    entry %u (0x%08x):\n", i, offset);

		/* TODO: investigate/describe meaning of the contents. */
		for (j = 0; j < ARRAY_SIZE(data); ++j) {
			snd_iprintf(buffer, "      %02u: %08x\n",
				    j, be32_to_cpu(data[j]));
		}

		offset += sizeof(data);
	}
}

static void dump_b3_subsection(struct snd_mln *mln,
			       struct snd_info_buffer *buffer, u32 offset,
			       u32 length)
{
	__be32 data[7];
	unsigned int entry_count;
	int i;
	int err;

	entry_count = length / sizeof(data);

	for (i = 0; i < entry_count; ++i) {
		__be32 chunks[9];
		char label[37];
		int j;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, data,
					 sizeof(data), 0);
		if (err < 0)
			break;
		if (be32_to_cpu(data[0]) == 0x000000000)
			break;

		snd_iprintf(buffer, "    entry %u (0x%08x):\n", i, offset);

		offset += sizeof(data);

		if (i > 0) {
			u32 label_offset = be32_to_cpu(data[0]);
			err = snd_fw_transaction(mln->unit,
						 TCODE_READ_BLOCK_REQUEST,
						 V3_BASE_ADDR + label_offset,
						 chunks, sizeof(chunks), 0);
			if (err < 0)
				break;
			parse_string(label, sizeof(label), chunks,
				     ARRAY_SIZE(chunks));
			snd_iprintf(buffer, "      label: %s\n", label);
		}

		/* TODO: investigate/describe meaning of the contents. */
		for (j = 0; j < ARRAY_SIZE(data); ++j) {
			snd_iprintf(buffer, "      %02u: %08x\n",
				    j, be32_to_cpu(data[j]));
		}
	}
}

static void dump_b4_subsection(struct snd_mln *mln,
			       struct snd_info_buffer *buffer, u32 offset,
			       u32 length)
{
	__be32 data[6];
	unsigned int entry_count;
	int i;
	int err;

	entry_count = length / sizeof(data);

	for (i = 0; i < entry_count; ++i) {
		__be32 chunks[9];
		char label[37];
		int j;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, data,
					 sizeof(data), 0);
		if (err < 0)
			break;

		if (be32_to_cpu(data[0]) == 0x000000000)
			break;

		snd_iprintf(buffer, "    entry %u (0x%08x):\n", i, offset);

		if (i > 0) {
			u32 label_offset = be32_to_cpu(data[0]);
			err = snd_fw_transaction(mln->unit,
						 TCODE_READ_BLOCK_REQUEST,
						 V3_BASE_ADDR + label_offset,
						 chunks, sizeof(chunks), 0);
			if (err < 0)
				break;
			parse_string(label, sizeof(label), chunks,
				     ARRAY_SIZE(chunks));
			snd_iprintf(buffer, "      label: %s\n", label);
		}

		/* TODO: investigate/describe meaning of the contents. */
		for (j = 0; j < ARRAY_SIZE(data); ++j) {
			snd_iprintf(buffer, "      %02u: %08x\n",
				    j, be32_to_cpu(data[j]));
		}

		offset += sizeof(data);
	}
}

static void dump_section_b(struct snd_mln *mln, struct snd_info_buffer *buffer,
			   u32 offset, u32 length)
{
	static void (*funcs[])(struct snd_mln *mln,
			       struct snd_info_buffer *buffer, u32 offset,
			       u32 length) = {
		dump_b1_subsection,
		dump_b2_subsection,
		dump_b3_subsection,
		dump_b4_subsection,
	};
	__be32 end_offsets[ARRAY_SIZE(funcs)];
	u32 subsection_offset;
	int i;
	int err;

	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, end_offsets,
				 sizeof(end_offsets), 0);
	if (err < 0)
		return;
	subsection_offset = offset + sizeof(end_offsets);

	for (i = 0; i < ARRAY_SIZE(funcs); ++i) {
		u32 end_offset;
		u32 size;

		end_offset = be32_to_cpu(end_offsets[i]);
		if (end_offset == 0 || offset + end_offset < subsection_offset)
			break;
		size = offset + end_offset - subsection_offset;

		snd_iprintf(buffer, "  subsection %u:\n", i + 1);
		snd_iprintf(buffer, "    offset %08x, size %08x\n",
			    subsection_offset, size);

		funcs[i](mln, buffer, subsection_offset, size);
		subsection_offset += size;
	}
}

static void dump_section_c(struct snd_mln *mln, struct snd_info_buffer *buffer,
			   u32 offset, u32 length)
{
	__be32 data[9];
	char label[37];
	unsigned int size;
	int i;
	int err;

	/* Response address. */
	size = sizeof(data[0]) * 2;
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data, size, 0);
	if (err < 0)
		return;
	offset += size;

	snd_iprintf(buffer, "  response address: %08x%08x\n",
		    be32_to_cpu(data[0]), be32_to_cpu(data[1]));

	/* Parameters. */
	size = sizeof(data[0]) * 7;
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data, size, 0);
	if (err < 0)
		return;

	/* TODO: investigate/describe meaning of the contents. */
	snd_iprintf(buffer, "  params (0x%08x):\n", offset);

	offset += size;

	for (i = 0; i < 7; ++i) {
		snd_iprintf(buffer, "    %02u: %08x\n",
			    i, be32_to_cpu(data[i]));
	}

	/* Model name. */
	size = sizeof(data[0]) * 9;
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data, size, 0);
	if (err < 0)
		return;
	offset += size;

	parse_string(label, sizeof(label), data, 9);
	snd_iprintf(buffer, "  model name: %s\n", label);

	/* Firmware name. */
	size = sizeof(data[0]) * 8;
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data, size, 0);
	if (err < 0)
		return;
	offset += size;

	parse_string(label, sizeof(label), data, 8);
	snd_iprintf(buffer, "  firmware version: %s\n", label);
}

static void dump_section_d(struct snd_mln *mln, struct snd_info_buffer *buffer,
			   u32 offset, u32 length)
{
	__be32 data[5];
	unsigned int entry_count;
	int i;
	int err;

	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR + offset, data, 4, 0);
	if (err < 0)
		return;
	offset += 4;

	entry_count = be32_to_cpu(data[0]);

	for (i = 0; i < entry_count; ++i) {
		int j;

		err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
					 V3_BASE_ADDR + offset, data,
					 sizeof(data), 0);
		if (err < 0)
			break;

		snd_iprintf(buffer, "  entry %u (0x%08x):\n", i, offset);

		offset += sizeof(data);

		/* TODO: investigate/describe meaning of the contents. */
		for (j = 0; j < ARRAY_SIZE(data); ++j) {
			snd_iprintf(buffer, "    %02u: %08x\n",
				    j, be32_to_cpu(data[j]));
		}
	}
}

static void v3_dump_info(struct snd_mln *mln, struct snd_info_buffer *buffer)
{
	static const struct {
		char *const name;
		void (*func)(struct snd_mln *mln,
			     struct snd_info_buffer *buffer, u32 offset,
			     u32 size);
	} *param, params[] = {
		{ .name = "A", .func = dump_section_a, },
		{ .name = "B", .func = dump_section_b, },
		{ .name = "C", .func = dump_section_c, },
		{ .name = "D", .func = dump_section_d, },
	};
	struct {
		__be32 offset;
		__be32 length;
	} *section, sections[ARRAY_SIZE(params)];
	int i;
	int err;

	/* Parse sections. */
	err = snd_fw_transaction(mln->unit, TCODE_READ_BLOCK_REQUEST,
				 V3_BASE_ADDR, sections, sizeof(sections), 0);
	if (err < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(params); ++i) {
		u32 offset;
		u32 length;

		param = params + i;
		section = sections + i;

		offset = be32_to_cpu(section->offset);
		length = be32_to_cpu(section->length);

		snd_iprintf(buffer, "section %s:\n", param->name);
		snd_iprintf(buffer, "  offset %08x, length %08x\n",
			    offset, length);

		param->func(mln, buffer, offset, length);
	}
}

const struct snd_mln_protocol snd_mln_protocol_v3 = {
	.version = 3,
	.dump_info = v3_dump_info,
};
