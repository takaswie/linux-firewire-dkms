obj-m += sound/firewire/

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)


all:
	$(MAKE) -Wall -C $(KDIR) M=$(PWD) modules

install:
	$(MAKE) -Wall -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -Wall -C $(KDIR) M=$(PWD) clean
