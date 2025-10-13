=================================================
Development and test for Linux firewire subsystem
=================================================

2025/12/14
Takashi Sakamoto (坂本 貴史)
<o-takashi@sakamocchi.jp>

NOTICE
======

If you need the previous state of this repository, please visit to
`master branch <https://github.com/takaswie/linux-firewire-dkms/tree/master>`_, however it is not
maintained anymore.

General
=======

Linux kernel has
`a subsystem to operate IEEE 1394 bus <https://ieee1394.docs.kernel.org/en/latest/>`_. This
repository is dedicated for its development and test.

Previously, this repository is named as ``snd-firewire-improve`` for the development and test of
ALSA firewire stack. You can see
`master branch <https://github.com/takaswie/linux-firewire-dkms/tree/master>`_ for the purpose,
however it is not integrated anymore.

Available kernel modules
========================

* subsystem core functions

  * firewire-core

* PCI device drivers

  * firewire-ohci
  * nosy

* Network protocol implementation and unit drivers

  * firewire-net

* SCSI protocol implementation and unit drivers

  * firewire-sbp2

* FireDTV/FloppyDTV protocol implementation and unit drivers

  * firedtv

* SCSI target protocol implementation and unit drivers

  * sbp_target

* Audio and music protocol implementation and unit drivers

  * snd-firewire-lib
  * snd-fireworks
  * snd-bebob
  * snd-oxfw
  * snd-dice
  * snd-firewire-digi00x
  * snd-firewire-tascam
  * snd-firewire-motu
  * snd-fireface

License
=======

GNU General Public License version 2.0, derived from the license of Linux kernel.

Easy instruction to work with DKMS
==================================

DKMS - Dynamic Kernel Module Support is easy for installing or updating external modules.
`<https://github.com/dell/dkms>`_

This instruction is for Debian/Ubuntu. You need to make your arrangement for the other Linux
distribution you use, especially for module signing.

These distributions provide the ``dkms`` package in their official repository. Install it at first.

::

    $ sudo apt-get install dkms


Then one of ``linux-headers`` packages is required to make the kernel modules for the running
kernel. For example, if the running kernel is ``linux-generic``, it should be
``linux-headers-generic``.

::

 $ sudo apt-get install linux-headers-generic

When building and installing the kernel modules, execute the following commands.

::

    $ git clone `<https://github.com/takaswie/linux-firewire-dkms.git>`_
    $ cd linux-firewire-dkms
    $ sudo ln -s $(pwd) /usr/src/linux-firewire-6.17
    $ sudo dkms install linux-firewire/6.17 --force

The ``force`` option is required to replace the kernel modules existing in the system. After the
installation, for the affinity to package manager, it is preferable to add the following file
including the content equivalent to the ``force`` option.

::

    $ sudo mkdir -p /usr/share/dkms/modules_to_force_install
    $ echo linux-firewire_version-override | sudo tee /usr/share/dkms/modules_to_force_install/linux-firewire.conf

When uninstalling and remove the kernel modules, execute the following commands.

::

    $ sudo dkms remove linux-firewire/6.17 --all
    $ sudo rm /usr/share/dkms/modules_to_force_install/linux-firewire.conf
    $ sudo rm /usr/src/linux-firewire-6.17
    $ rm -r snd-firewire-improve
