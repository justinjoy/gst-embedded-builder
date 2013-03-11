-include ./configs/$(PLATFORM_CHIP)-variables.mk


GSTGOOD_NAME := gst-plugins-good
GSTGOOD_PKG := src/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION).tar.xz

GSTGOOD_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION)

GSTGOOD_CFLAGS = -I$(PLATFORM_STAGING_DIR)/include
GSTGOOD_LDFLAGS = -L$(PLATFORM_STAGING_DIR)/lib

.PHONY: extract all

all: extract configure
	@echo "building gst-plugins-base..."
	cd $(GSTGOOD_TARGET_BUILD_DIR); make
	cd $(GSTGOOD_TARGET_BUILD_DIR); make install

configure: $(GSTGOOD_TARGET_BUILD_DIR)/.config

$(GSTGOOD_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTGOOD_TARGET_BUILD_DIR)
	cd $(GSTGOOD_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTGOOD_CFLAGS)" \
		LDFLAGS="$(GSTGOOD_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--enable-bz2 \
		--disable-gst_v4l2  \
		--disable-x \
		--disable-gdk_pixbuf 
	touch $@

extract: $(EXTRACT_DIR)/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION)

$(EXTRACT_DIR)/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTGOOD_NAME)-$(GSTGOOD_VERSION)
	xz -dc $(GSTGOOD_PKG) | tar xvf - -C $(EXTRACT_DIR)

