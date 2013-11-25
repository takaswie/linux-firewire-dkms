#ifndef SOUND_FIREWIRE_FCP_H_INCLUDED
#define SOUND_FIREWIRE_FCP_H_INCLUDED

struct fw_unit;

/* AV/C Digital Interface Command Set General Specification 4.2 (1394TA) */
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

int fcp_avc_transaction(struct fw_unit *unit,
			const void *command, unsigned int command_size,
			void *response, unsigned int response_size,
			unsigned int response_match_bytes);
void fcp_bus_reset(struct fw_unit *unit);

#endif
