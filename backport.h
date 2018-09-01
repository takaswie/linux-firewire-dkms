#include <target/target_core_base.h>
#include <target/target_core_backend.h>

#ifdef TRANSPORT_FLAG_PASSTHROUGH_ALUA
#include <linux/sched/signal.h>
#else
#include <linux/sched.h>
#endif

#include "./include/uapi/sound/firewire.h"
#include "./include/uapi/sound/asound.h"
#include "./include/uapi/sound/tlv.h"

#include <sound/core.h>
#include <sound/pcm.h>
