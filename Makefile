obj-m += sound/firewire/

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

#force to build all modules
export CONFIG_SND_FIREWIRE_LIB=m
export CONFIG_SND_DICE=m
export CONFIG_SND_ISIGHT=m
export CONFIG_SND_SCS1X=m
export CONFIG_SND_BEBOB=m
export CONFIG_SND_FIREWORKS=m
export CONFIG_SND_OXFW=m

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
