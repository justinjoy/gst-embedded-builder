-include ./configs/$(PLATFORM_CHIP)-variables.mk


GSTBAD_NAME := gst-plugins-bad
GSTBAD_PKG := src/$(GSTBAD_NAME)-$(GSTBAD_VERSION).tar.xz

GSTBAD_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTBAD_NAME)-$(GSTBAD_VERSION)

GSTBAD_CFLAGS = -I$(PLATFORM_STAGING_DIR)/include
GSTBAD_LDFLAGS = -L$(PLATFORM_STAGING_DIR)/lib

.PHONY: extract all

all: extract configure
	@echo "building gst-plugins-bad ..."
	cd $(GSTBAD_TARGET_BUILD_DIR); make
	cd $(GSTBAD_TARGET_BUILD_DIR); make install

configure: $(GSTBAD_TARGET_BUILD_DIR)/.config

$(GSTBAD_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTBAD_TARGET_BUILD_DIR)
	cd $(GSTBAD_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTBAD_NAME)-$(GSTBAD_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTBAD_CFLAGS)" \
		LDFLAGS="$(GSTBAD_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--enable-bz2 
	touch $@

extract: $(EXTRACT_DIR)/$(GSTBAD_NAME)-$(GSTBAD_VERSION)

$(EXTRACT_DIR)/$(GSTBAD_NAME)-$(GSTBAD_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTBAD_NAME)-$(GSTBAD_VERSION)
	xz -dc $(GSTBAD_PKG) | tar xvf - -C $(EXTRACT_DIR)

