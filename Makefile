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

# DT overlay
DTS_DIR     := $(CURDIR)/dts
OVERLAY_SRC := $(DTS_DIR)/rk3566-rknpu-overlay.dts
OVERLAY_PP  := $(OVERLAY_SRC).preprocessed
OVERLAY_OUT := $(DTS_DIR)/rk3566-rknpu.dtbo

.PHONY: all clean install dtbo install-dtbo

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) clean
	rm -f $(OVERLAY_PP) $(OVERLAY_OUT)

install:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KDIR) M=$(CURDIR) modules_install

dtbo: $(OVERLAY_OUT)

$(OVERLAY_OUT): $(OVERLAY_SRC)
	cpp -nostdinc -I $(KDIR)/include -undef -x assembler-with-cpp $< $(OVERLAY_PP)
	dtc -@ -I dts -O dtb -o $@ $(OVERLAY_PP)
	rm -f $(OVERLAY_PP)

install-dtbo: $(OVERLAY_OUT)
	install -D -m 644 $(OVERLAY_OUT) /boot/overlay-user/rk3566-rknpu.dtbo
