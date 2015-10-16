#include "./include/uapi/sound/firewire.h"
#include "./include/uapi/sound/asound.h"

#include <sound/core.h>
#include <sound/pcm.h>

void fw_schedule_bus_reset(struct fw_card *card, bool delay, bool short_reset);

/* commit 1fb8510cdb5b7befe8a59f533c7fc12ef0dac73e */
/* This macro is just convenient to detect Linux 3.19 or later */
#include <sound/soc.h>
#ifndef SOC_DOUBLE_S_VALUE
static inline void snd_pcm_stop_xrun(struct snd_pcm_substream *substream)
{
	unsigned long flags;

	snd_pcm_stream_lock_irqsave(substream, flags);
	if (snd_pcm_running(substream))
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	snd_pcm_stream_unlock_irqrestore(substream, flags);
}
#endif

/* commit 393aa9c1cc514774332d7bc861307a76206e358d */
/* This macro is just convenient to detect Linux 3.16 or later. */
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

/* commit 67cb9366ff5f99868100198efba5ca88aaa6ad25 */
static inline bool ktime_after(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) > 0;
}
#endif

/* commit 16735d022f72b20ddbb2274b8e109f69575e9b2b */
#ifdef INIT_COMPLETION
static inline void reinit_completion(struct completion *x)
{
	INIT_COMPLETION(*x);
}
#endif
