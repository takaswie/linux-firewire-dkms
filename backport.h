#include "./include/uapi/sound/firewire.h"
#include "./include/uapi/sound/asound.h"

#include <sound/core.h>

void fw_schedule_bus_reset(struct fw_card *card, bool delay, bool short_reset);

/* commit 393aa9c1cc514774332d7bc861307a76206e358d */
/* This macro is just convinient to detect Linux 3.16 or later. */
#ifndef dev_to_snd_card
static inline int snd_card_new(struct device *parent, int idx, const char *xid,
			       struct module *module, int extra_size,
			       struct snd_card **card_ret)
{
	int err = snd_card_create(idx, xid, module, extra_size, card_ret);
	if (err >= 0)
		(*card_ret)->dev = parent;
	return err;
}
#endif

/* commit 16735d022f72b20ddbb2274b8e109f69575e9b2b */
#ifdef INIT_COMPLETION
static inline void reinit_completion(struct completion *x)
{
	INIT_COMPLETION(*x);
}
#endif
