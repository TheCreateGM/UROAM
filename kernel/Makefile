# Kernel Module Makefile
# Build with: make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

obj-m += uroam.o
uroam-objs := main.o hooks.o pressure.o tracking.o numa.o ipc.o

# Kernel source directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Module name
MODULE_NAME := uroam

# Default build target
all: build

build:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Install
install: build
	mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/misc
	cp $(MODULE_NAME).ko /lib/modules/$(shell uname -r)/kernel/drivers/misc/
	depmod -a

# Uninstall
uninstall:
	rm -f /lib/modules/$(shell uname -r)/kernel/drivers/misc/$(MODULE_NAME).ko
	depmod -a

# Load module
load: build
	sudo insmod $(MODULE_NAME).ko

# Unload module
unload:
	sudo rmmod $(MODULE_NAME)

# Reload module
reload: unload load

.PHONY: all build clean install uninstall load unload reload
