#ifndef SOUND_FIREWIRE_FCP_H_INCLUDED
#define SOUND_FIREWIRE_FCP_H_INCLUDED

#define	AVC_PLUG_INFO_BUF_BYTES	4

struct fw_unit;

/*
 * AV/C Digital Interface Command Set General Specification 4.2
 * (Sep 2004, 1394TA)
 */
#define AVC_GENERIC_FRAME_MAXIMUM_BYTES	512
enum avc_general_plug_dir {
	AVC_GENERAL_PLUG_DIR_IN		= 0,
	AVC_GENERAL_PLUG_DIR_OUT	= 1,
	AVC_GENERAL_PLUG_DIR_COUNT
};
int avc_general_set_sig_fmt(struct fw_unit *unit, unsigned int rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_sig_fmt(struct fw_unit *unit, unsigned int *rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_plug_info(struct fw_unit *unit, unsigned int subunit_type,
			      unsigned int subunit_id, unsigned int subfunction,
			      u8 info[AVC_PLUG_INFO_BUF_BYTES]);

/*
 * AV/C Stream Format Information Specification 1.1 Working Draft
 * (Apr 2005, 1394TA)
 */
enum avc_stream_rates {
	AVC_STREAM_RATE_22050 = 0,
	AVC_STREAM_RATE_24000,
	AVC_STREAM_RATE_32000,
	AVC_STREAM_RATE_44100,
	AVC_STREAM_RATE_48000,
	AVC_STREAM_RATE_88200,
	AVC_STREAM_RATE_96000,
	AVC_STREAM_RATE_176400,
	AVC_STREAM_RATE_192000,
	AVC_STREAM_RATE_COUNT,
};
extern const unsigned int avc_stream_rate_table[AVC_STREAM_RATE_COUNT];
extern const unsigned int avc_stream_rate_codes[AVC_STREAM_RATE_COUNT];
int avc_stream_set_format(struct fw_unit *unit, enum avc_general_plug_dir dir,
			  unsigned int pid, u8 *format, unsigned int len);
int avc_stream_get_format(struct fw_unit *unit,
			  enum avc_general_plug_dir dir, unsigned int pid,
			  u8 *buf, unsigned int *len, unsigned int eid);
static inline int
avc_stream_get_format_single(struct fw_unit *unit,
			     enum avc_general_plug_dir dir, unsigned int pid,
			     u8 *buf, unsigned int *len)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, 0xff);
}
static inline int
avc_stream_get_format_list(struct fw_unit *unit,
			   enum avc_general_plug_dir dir, unsigned int pid,
			   u8 *buf, unsigned int *len,
			   unsigned int eid)
{
	return avc_stream_get_format(unit, dir, pid, buf, len, eid);
}
struct avc_stream_formation {
	unsigned int rate;
	unsigned int pcm;
	unsigned int midi;
};
int avc_stream_parse_format(u8 *format, struct avc_stream_formation *formation);

int fcp_avc_transaction(struct fw_unit *unit,
			const void *command, unsigned int command_size,
			void *response, unsigned int response_size,
			unsigned int response_match_bytes);
void fcp_bus_reset(struct fw_unit *unit);

#endif
