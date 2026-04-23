obj-m := uroam_kernel.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -I$(src)/../include

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

help:
	$(MAKE) -C $(KDIR) M=$(PWD) help

.PHONY: all clean install help