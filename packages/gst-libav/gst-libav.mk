-include ./configs/$(PLATFORM_CHIP)-variables.mk


GSTLIBAV_NAME := gst-libav
GSTLIBAV_PKG := src/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION).tar.xz

GSTLIBAV_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)

GSTLIBAV_LDFLAGS=-L$(PLATFORM_STAGING_DIR)/lib
GSTLIBAV_CFLAGS=-I$(PLATFORM_STAGING_DIR)/include

.PHONY: extract all

all: extract patch configure
	@echo "building gst-libav..."
	cd $(GSTLIBAV_TARGET_BUILD_DIR); make
	cd $(GSTLIBAV_TARGET_BUILD_DIR); make install


patch: $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/.patch

$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/.patch:
	cd $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION); \
	patch -p1 < $(PKG_DIR)/$(GSTLIBAV_NAME)/cross-compile.patch
	touch $@

configure: $(GSTLIBAV_TARGET_BUILD_DIR)/.config

$(GSTLIBAV_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTLIBAV_TARGET_BUILD_DIR)
	cd $(GSTLIBAV_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTLIBAV_CFLAGS)" \
		LDFLAGS="$(GSTLIBAV_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--enable-shared \
		--with-libav-extra-configure=" \
			--enable-zlib \
			--enable-bzlib \
			--enable-neon \
			--target-os=linux"
	touch $@

extract: $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)

$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)
	xz -dc $(GSTLIBAV_PKG) | tar xvf - -C $(EXTRACT_DIR)

