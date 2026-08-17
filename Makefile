# SPDX-License-Identifier: GPL-2.0-or-later

obj-m += drivers/firewire/
obj-m += drivers/media/firewire/
obj-m += drivers/target/sbp/
obj-m += sound/firewire/

# The 'TOPLEVEL_HEADER_DIRECTORY' was provided into the sub-make process by the following part.
NOSTDINC_FLAGS += -I$(TOPLEVEL_HEADER_DIRECTORY) -I$(TOPLEVEL_UAPI_HEADER_DIRECTORY)

#
# The following part is not used by the top-level Makefile of kernel tree referred by the -C option.
#

# The same targets which the Makefile of Linux kernel has.
LOCAL_DERIVED_TARGETS = modules modules_install clean help

# All of defined targets. The above targets appended with some specific targets for my convenience.
LOCAL_TARGETS = $(LOCAL_DERIVED_TARGETS) check

ifdef DKMS_KERNEL_SOURCE_DIR
# Invoked by dkms command.
KERNEL_SOURCE_DIR := $(DKMS_KERNEL_SOURCE_DIR)
else
# Invoked by hand. Use built kernel for the running system.
KERNEL_SOURCE_DIR := /lib/modules/$(shell uname -r)/build
endif

# Utilize GNU Make 'Target-specific Variable Values' to introduce some variables locally in each
# targets. The one prefixed with 'export' special keyword is going to be introduced to sub-make
# processes.
$(LOCAL_TARGETS): KDIR := $(KERNEL_SOURCE_DIR)
$(LOCAL_TARGETS): PWD := $(shell pwd)
$(LOCAL_TARGETS): export TOPLEVEL_HEADER_DIRECTORY := $(PWD)/include
$(LOCAL_TARGETS): export TOPLEVEL_UAPI_HEADER_DIRECTORY := $(PWD)/include/uapi

$(LOCAL_DERIVED_TARGETS):
	$(Q)$(MAKE) W=1 -C $(KDIR) M=$(PWD) $@

check: clean
	$(Q)$(MAKE) W=1 -C $(KDIR) M=$(PWD) C=1 CF=-D__CHECK_ENDIAN__ modules

.PHONY: $(LOCAL_TARGETS)
