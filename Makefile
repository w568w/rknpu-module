# RKNPU Out-of-tree Kernel Module Makefile
#
# Supports two build modes:
#   1. Cross-compilation (local x86_64 host → arm64 target):
#        make KDIR=./kernel-headers
#   2. Native build on device / DKMS:
#        make  (KDIR defaults to /lib/modules/$(uname -r)/build)
#
# Module build definitions are in the Kbuild file (read by the
# kernel build system directly).

KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= arm64

# Auto-detect: only cross-compile when the host is not arm64
ifeq ($(shell uname -m),aarch64)
  CROSS_COMPILE ?=
else
  CROSS_COMPILE ?= aarch64-linux-gnu-
endif

.PHONY: all clean install

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) clean

install:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) modules_install
