#ifndef SOUND_FIREWIRE_CMP_H_INCLUDED
#define SOUND_FIREWIRE_CMP_H_INCLUDED

#include <linux/mutex.h>
#include <linux/types.h>
#include "iso-resources.h"

/* MPR common fields */
#define MPR_SPEED_MASK		0xc0000000
#define MPR_SPEED_SHIFT		30
#define MPR_XSPEED_MASK		0x00000060
#define MPR_XSPEED_SHIFT	5
#define MPR_PLUGS_MASK		0x0000001f

/* PCR common fields */
#define PCR_ONLINE		0x80000000
#define PCR_ONLINE_SHIFT	31
#define PCR_BCAST_CONN		0x40000000
#define PCR_P2P_CONN_MASK	0x3f000000
#define PCR_P2P_CONN_SHIFT	24
#define PCR_CHANNEL_MASK	0x003f0000
#define PCR_CHANNEL_SHIFT	16

/* oPCR specific fields */
#define OPCR_XSPEED_MASK	0x00C00000
#define OPCR_XSPEED_SHIFT	22
#define OPCR_SPEED_MASK		0x0000C000
#define OPCR_SPEED_SHIFT	14
#define OPCR_OVERHEAD_ID_MASK	0x00003C00
#define OPCR_OVERHEAD_ID_SHIFT	10

struct fw_unit;

enum cmp_direction {
	CMP_INPUT = 0,
	CMP_OUTPUT,
};

/**
 * struct cmp_connection - manages an isochronous connection to a device
 * @speed: the connection's actual speed
 *
 * This structure manages (using CMP) an isochronous stream between the local
 * computer and a device's input plug (iPCR) and output plug (oPCR).
 *
 * There is no corresponding oPCR created on the local computer, so it is not
 * possible to overlay connections on top of this one.
 */
struct cmp_connection {
	int speed;
	/* private: */
	bool connected;
	struct mutex mutex;
	struct fw_iso_resources resources;
	__be32 last_pcr_value;
	unsigned int pcr_index;
	unsigned int max_speed;
	enum cmp_direction direction;
};

int cmp_connection_init(struct cmp_connection *connection,
			struct fw_unit *unit,
			enum cmp_direction direction,
			unsigned int pcr_index);
int cmp_connection_check_used(struct cmp_connection *connection, bool *used);
void cmp_connection_destroy(struct cmp_connection *connection);

int cmp_connection_establish(struct cmp_connection *connection,
			     unsigned int max_payload);
int cmp_connection_update(struct cmp_connection *connection);
void cmp_connection_break(struct cmp_connection *connection);

#endif
