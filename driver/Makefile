KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: modules clean

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
