# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux IEEE 1394 implementation
#

firewire-core-y += core-card.o core-cdev.o core-device.o \
                   core-iso.o core-topology.o core-transaction.o
firewire-ohci-y += ohci.o

obj-$(CONFIG_FIREWIRE)      += firewire-core.o
obj-$(CONFIG_FIREWIRE_OHCI) += firewire-ohci.o
