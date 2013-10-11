#include "./bebob.h"

/* contents of information register */
struct hw_info {
	u64 manufacturer;
	u32 protocol_ver;
	u32 bld_ver;
	u32 guid[2];
	u32 model_id;
	u32 model_rev;
	u64 fw_date;
	u64 fw_time;
	u32 fw_id;
	u32 fw_ver;
	u32 base_addr;
	u32 max_size;
	u64 bld_date;
	u64 bld_time;
/* may not used in product
	u64 dbg_date;
	u64 dbg_time;
	u32 dbg_id;
	u32 dbg_version;
*/
} __packed;

static void
proc_read_hw_info(struct snd_info_entry *entry,
		  struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	struct hw_info *info;
	int err;

	info = kzalloc(sizeof(struct hw_info), GFP_KERNEL);
	if (info == NULL)
		return;

	err = snd_bebob_read_block(bebob, 0,
				   info, sizeof(struct hw_info));
	if (err < 0)
		goto end;

	snd_iprintf(buffer, "Manufacturer:\t%.8s\n",
		    (char *)&info->manufacturer);
	snd_iprintf(buffer, "Protocol Ver:\t%d\n", info->protocol_ver);
	snd_iprintf(buffer, "Build Ver:\t%d\n", info->bld_ver);
	snd_iprintf(buffer, "GUID:\t\t0x%.8X%.8X\n",
		    info->guid[0], info->guid[1]);
	snd_iprintf(buffer, "Model ID:\t0x%02X\n", info->model_id);
	snd_iprintf(buffer, "Model Rev:\t%d\n", info->model_rev);
	snd_iprintf(buffer, "Firmware Date:\t%.8s\n", (char *)&info->fw_date);
	snd_iprintf(buffer, "Firmware Time:\t%.8s\n", (char *)&info->fw_time);
	snd_iprintf(buffer, "Firmware ID:\t0x%X\n", info->fw_id);
	snd_iprintf(buffer, "Firmware Ver:\t%d\n", info->fw_ver);
	snd_iprintf(buffer, "Base Addr:\t0x%X\n", info->base_addr);
	snd_iprintf(buffer, "Max Size:\t%d\n", info->max_size);
	snd_iprintf(buffer, "Loader Date:\t%.8s\n", (char *)&info->bld_date);
	snd_iprintf(buffer, "Loader Time:\t%.8s\n", (char *)&info->bld_time);

end:
	kfree(info);
	return;
}

static void
proc_read_meters(struct snd_info_entry *entry,
		 struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	struct snd_bebob_meter_spec *spec = bebob->spec->meter;
	u32 *buf;
	int i, c, channels, size, err;

	if (spec == NULL)
		return;

	channels = spec->num * 2;
	size = channels * sizeof(u32);
	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return;

	err = spec->get(bebob, buf, size);
	if (err < 0)
		goto end;

	for (i = 0, c = 1; i < channels; i++) {
		snd_iprintf(buffer, "%s %d: %d\n",
			    spec->labels[i / 2], c++, buf[i]);
		if ((i + 1 < channels - 1) &&
		    (strcmp(spec->labels[i / 2],
			    spec->labels[(i + 1) / 2]) != 0))
			c = 1;
	}
end:
	kfree(buf);
	return;
}

static void
proc_read_formation(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	struct snd_bebob_stream_formation *formation;
	int i;

	snd_iprintf(buffer, "Reveice Stream:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = bebob->tx_stream_formations;
	for (i = 0; i < 9; i += 1) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n", snd_bebob_rate_table[i],
			formation[i].pcm, formation[i].midi);
	}

	snd_iprintf(buffer, "Transmit Stream:\n");
	snd_iprintf(buffer, "\tRate\tPCM\tMIDI\n");
	formation = bebob->rx_stream_formations;
	for (i = 0; i < 9; i += 1) {
		snd_iprintf(buffer,
			"\t%d\t%d\t%d\n", snd_bebob_rate_table[i],
			formation[i].pcm, formation[i].midi);
	}

	return;
}

static void
proc_read_clock(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_bebob *bebob = entry->private_data;
	int err;
	int rate;

	err= avc_generic_get_sampling_rate(bebob->unit, &rate, 0, 0);
	if (err == 0)
		snd_iprintf(buffer,
			    "Output Plug 0: rate %d\n", rate);
	err = avc_generic_get_sampling_rate(bebob->unit, &rate, 1, 0);
	if (err == 0)
		snd_iprintf(buffer,
			    "Input Plug 0: rate %d\n", rate);

	return;
}

void snd_bebob_proc_init(struct snd_bebob *bebob)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(bebob->card, "#hardware", &entry))
		snd_info_set_text_ops(entry, bebob, proc_read_hw_info);

	if (!snd_card_proc_new(bebob->card, "#clock", &entry))
		snd_info_set_text_ops(entry, bebob, proc_read_clock);

	if (!snd_card_proc_new(bebob->card, "#formation", &entry))
		snd_info_set_text_ops(entry, bebob, proc_read_formation);

	if (bebob->spec->meter != NULL) {
		if (!snd_card_proc_new(bebob->card, "#meter", &entry))
			snd_info_set_text_ops(entry, bebob, proc_read_meters);
	}

	return;
}
