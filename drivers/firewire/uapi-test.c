// SPDX-License-Identifier: GPL-2.0-only
//
// uapi_test.c - An application of Kunit to check layout of structures exposed to user space for
//		 FireWire subsystem.
//
// Copyright (c) 2023 Takashi Sakamoto

#include <kunit/test.h>
#include <linux/firewire-cdev.h>

// Known issue added at v2.6.27 kernel.
static void structure_layout_event_response(struct kunit *test)
{
#if defined(CONFIG_X86_32)
	// 4 bytes alignment for aggregate type including 8 bytes storage types.
	KUNIT_EXPECT_EQ(test, 20, sizeof(struct fw_cdev_event_response));
#else
	// 8 bytes alignment for aggregate type including 8 bytes storage types.
	KUNIT_EXPECT_EQ(test, 24, sizeof(struct fw_cdev_event_response));
#endif

	KUNIT_EXPECT_EQ(test, 0, offsetof(struct fw_cdev_event_response, closure));
	KUNIT_EXPECT_EQ(test, 8, offsetof(struct fw_cdev_event_response, type));
	KUNIT_EXPECT_EQ(test, 12, offsetof(struct fw_cdev_event_response, rcode));
	KUNIT_EXPECT_EQ(test, 16, offsetof(struct fw_cdev_event_response, length));
	KUNIT_EXPECT_EQ(test, 20, offsetof(struct fw_cdev_event_response, data));
}

static struct kunit_case structure_layout_test_cases[] = {
	KUNIT_CASE(structure_layout_event_response),
	{}
};

static struct kunit_suite structure_layout_test_suite = {
	.name = "firewire-uapi-structure-layout",
	.test_cases = structure_layout_test_cases,
};
kunit_test_suite(structure_layout_test_suite);
