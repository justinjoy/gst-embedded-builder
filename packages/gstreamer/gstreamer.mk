-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


GSTREAMER_NAME := gstreamer
GSTREAMER_PKG := src/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION).tar.xz

GSTREAMER_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building gstreamer..."
	cd $(GSTREAMER_TARGET_BUILD_DIR); make CC=$(TARGET_ARCH)-gcc
	cd $(GSTREAMER_TARGET_BUILD_DIR); make install

configure: $(GSTREAMER_TARGET_BUILD_DIR)/.config

$(GSTREAMER_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTREAMER_TARGET_BUILD_DIR)
	cd $(GSTREAMER_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTREAMER_CFLAGS)" \
		LDFLAGS="$(GSTREAMER_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--disable-tests \
		--disable-examples \
		--disable-introspection
	touch $@

extract: $(EXTRACT_DIR)/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION)

$(EXTRACT_DIR)/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTREAMER_NAME)-$(GSTREAMER_VERSION)
	xz -dc $(GSTREAMER_PKG) | tar xvf - -C $(EXTRACT_DIR)

