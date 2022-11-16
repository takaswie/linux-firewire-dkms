==================================
Developing for ALSA firewire stack
==================================

2022/11/16 坂本 貴史
Takashi Sakamoto <o-takashi@sakamocchi.jp>

General
=======

ALSA, Linux sound subsystem, includes packet streaming engine and device
drivers for audio and music units on IEEE 1394 bus. This software stack
is called as ALSA firewire stack.

This repository is a test of my patch candidates for ALSA firewire stack.
Currently, several modules are available by DKMS:

Released and merged:

* snd-fireworks: for devices based on Echo Audio's Fireworks module
* snd-bebob: for devices based on BridgeCo's chipset and BeBoB firmware
* snd-oxfw: for devices based on Oxford Semiconductor OXFW970/971 chipset
* snd-dice: for devices based on TC Applied Technologies DICE chipset family
* snd-firewire-digi00x: for devices of Digidesign 002/003 family
* snd-firewire-tascam: for devices of TASCAM FireWire series
* snd-firewire-motu: for devices of Mark of the Unicorn FireWire series
* snd-fireface: for devices of RME Fireface series
* snd-firewire-lib: for helper functions of AMDTP/CMP/FCP and AV/C commands

Under developing

* snd-firewire-transceiver: enable packet streaming between Linux systems

My aim is implementing streaming functionality in ALSA, in kernel land. The other functionalities
are out of the aim and should be implemented in userspace applications.
`<https://github.com/alsa-project/snd-firewire-ctl-services>`_ is available for the purpose.

Requirement
===========

- Linux kernel 5.19 or later


Current status
==============

* Capture/playback of PCM/MIDI is supported at any sampling rate, any clock source
* HWDEP interface is supported for mixer control application
* Model dependent functionalities which requires help of kernel space


Bug repots
==========

Linux 3.16 or later already includes snd-bebob and snd-fireworks.
Linux 3.19 or later includes snd-oxfw and updates of snd-dice.
Linux 4.4 includes snd-firewire-digi00x and snd-firewire-tascam.
Linux 4.12 includes snd-firewire-motu and snd-fireface.

Any bug reports should be sent to alsa-devel to share with users and developers.
`<http://mailman.alsa-project.org/mailman/listinfo/alsa-devel>`_


Resources
=========
You can see my report about this developing:
`<https://github.com/takaswie/alsa-firewire-report>`_


Easy instruction with DKMS
==========================

DKMS - Dynamic Kernel Module Support is easy for installing or updating external modules.
`<https://github.com/dell/dkms>`_

This instruction is for Debian/Ubuntu. You need to make your arrangement for
the other Linux distribution if you use.

Then you need to install 'dkms' package.

::

    $ sudo apt-get install dkms

Then you need to install 'linux-headers' package to make drivers for your kernel.

::

 $ sudo apt-get install linux-headers-generic

Install

::

    $ git clone `<https://github.com/takaswie/snd-firewire-improve.git>`_
    $ cd snd-firewire-improve
    $ ln -s $(pwd) /usr/src/alsa-firewire-5.19 (superuser)
    $ dkms install alsa-firewire/5.19 (superuser)

Uninstall

::

    $ modprobe -r snd-isight snd-bebob snd-fireworks snd-dice snd-oxfw \
      snd-firewire-digi00x snd-firewire-tascam snd-firewire-motu \
      snd-firewire-fireface snd-firewire-lib (superuser)
    $ dkms remove alsa-firewire/5.19 --all (superuser)
    $ rm /usr/src/alsa-firewire-5.19 (superuser)
    $ rm snd-firewire-improve


End
