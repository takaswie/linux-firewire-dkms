#include "./include/uapi/sound/firewire.h"
#include "./include/uapi/sound/asound.h"

#include <sound/core.h>

/* commit 393aa9c1cc514774332d7bc861307a76206e358d */
static inline int snd_card_new(struct device *parent, int idx, const char *xid,
			       struct module *module, int extra_size,
			       struct snd_card **card_ret)
{
	int err = snd_card_create(idx, xid, module, extra_size, card_ret);
	if (err >= 0)
		(*card_ret)->dev = parent;
	return err;
}

/* commit 16735d022f72b20ddbb2274b8e109f69575e9b2b */
static inline void reinit_completion(struct completion *x)
{
	INIT_COMPLETION(*x);
}
