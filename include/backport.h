// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The macro definitions backported from the latest upstream.
 *
 * Copyright (c) 2025 Takashi Sakamoto
 */

// Added to v6.16 kernel by a commit 092d00ead733 ("cleanup: Provide retain_and_null_ptr()")
#ifndef retain_and_null_ptr
#define retain_and_null_ptr(p)          ((void)__get_and_null(p, NULL))
#endif
