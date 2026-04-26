# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree kernel module build for rf433_drv
#
# Usage (native):
#   make KDIR=/lib/modules/$(uname -r)/build
#
# Usage (cross-compile for RK3568):
#   make KDIR=/path/to/kernel CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64

obj-m := rf433_drv.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

.PHONY: all clean install
