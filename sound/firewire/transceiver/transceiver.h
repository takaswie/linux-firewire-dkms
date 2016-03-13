/*
 * transceiver.h - something
 *
 * Copyright (c) 2015-2016 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_FIREWIRE_TRANSCEIVER_H_INCLUDED
#define SOUND_FIREWIRE_TRANSCEIVER_H_INCLUDED

#include <linux/firewire.h>
#include <linux/firewire-constants.h>

/* TODO: remove when merging to upstream. */
#include "../../../backport.h"

#include <sound/core.h>

#include "../iso-resources.h"
#include "../cmp.h"
#include "../amdtp-am824.h"

int fw_am_unit_probe(struct fw_unit *unit);
void fw_am_unit_update(struct fw_unit *unit);
void fw_am_unit_remove(struct fw_unit *unit);

int fw_am_cmp_init(void);
void fw_am_cmp_destroy(void);

int fw_am_fcp_init(void);
void fw_am_fcp_destroy(void);

int snd_fwtxrx_stream_add_pcm_constraints(struct amdtp_stream *stream,
					  struct snd_pcm_runtime *runtime);
int snd_fwtxrx_name_card(struct fw_unit *unit, struct snd_card *card);

#endif
