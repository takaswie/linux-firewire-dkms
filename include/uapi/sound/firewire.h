#ifndef _UAPI_SOUND_FIREWIRE_H_INCLUDED
#define _UAPI_SOUND_FIREWIRE_H_INCLUDED

#include <linux/ioctl.h>

/* events can be read() from the hwdep device */

#define SNDRV_FIREWIRE_EVENT_LOCK_STATUS	0x000010cc
#define SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION	0xd1ce004e
#define SNDRV_FIREWIRE_EVENT_EFW_RESPONSE	0x4e617475

struct snd_firewire_event_common {
	unsigned int type; /* SNDRV_FIREWIRE_EVENT_xxx */
};

struct snd_firewire_event_lock_status {
	unsigned int type;
	unsigned int status; /* 0/1 = unlocked/locked */
};

struct snd_firewire_event_dice_notification {
	unsigned int type;
	unsigned int notification; /* DICE-specific bits */
};

#define SND_EFW_TRANSACTION_SEQNUM_MAX	((uint32_t)(BIT(28) - 1))
/* each field should be in big endian */
struct snd_efw_transaction {
	uint32_t length;
	uint32_t version;
	uint32_t seqnum;
	uint32_t category;
	uint32_t command;
	uint32_t status;
	uint32_t params[0];
};
struct snd_firewire_event_efw_response {
	unsigned int type;
	uint32_t response[0];	/* some responses */
};

union snd_firewire_event {
	struct snd_firewire_event_common            common;
	struct snd_firewire_event_lock_status       lock_status;
	struct snd_firewire_event_dice_notification dice_notification;
	struct snd_firewire_event_efw_response      efw_response;
};


#define SNDRV_FIREWIRE_IOCTL_GET_INFO _IOR('H', 0xf8, struct snd_firewire_get_info)
#define SNDRV_FIREWIRE_IOCTL_LOCK      _IO('H', 0xf9)
#define SNDRV_FIREWIRE_IOCTL_UNLOCK    _IO('H', 0xfa)

#define SNDRV_FIREWIRE_TYPE_DICE	1
#define SNDRV_FIREWIRE_TYPE_BEBOB	2
#define SNDRV_FIREWIRE_TYPE_FIREWORKS	3
/* Fireworks, AV/C, RME, MOTU, ... */

struct snd_firewire_get_info {
	unsigned int type; /* SNDRV_FIREWIRE_TYPE_xxx */
	unsigned int card; /* same as fw_cdev_get_info.card */
	unsigned char guid[8];
	char device_name[16]; /* device node in /dev */
};

/*
 * SNDRV_FIREWIRE_IOCTL_LOCK prevents the driver from streaming.
 * Returns -EBUSY if the driver is already streaming.
 */

#endif /* _UAPI_SOUND_FIREWIRE_H_INCLUDED */
