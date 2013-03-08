-include ./configs/$(PLATFORM_CHIP)-variables.mk


GSTBASE_NAME := gst-plugins-base
GSTBASE_PKG := src/$(GSTBASE_NAME)-$(GSTBASE_VERSION).tar.xz

GSTBASE_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTBASE_NAME)-$(GSTBASE_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building gst-plugins-base..."
	cd $(GSTBASE_TARGET_BUILD_DIR); make
	cd $(GSTBASE_TARGET_BUILD_DIR); make install

configure: $(GSTBASE_TARGET_BUILD_DIR)/.config

$(GSTBASE_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTBASE_TARGET_BUILD_DIR)
	cd $(GSTBASE_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTBASE_NAME)-$(GSTBASE_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTBASE_CFLAGS)" \
		LDFLAGS="$(GSTBASE_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR)  \
		--disable-x \
		--disable-xvideo \
		--disable-alsa 
	touch $@

extract: $(EXTRACT_DIR)/$(GSTBASE_NAME)-$(GSTBASE_VERSION)

$(EXTRACT_DIR)/$(GSTBASE_NAME)-$(GSTBASE_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTBASE_NAME)-$(GSTBASE_VERSION)
	xz -dc $(GSTBASE_PKG) | tar xvf - -C $(EXTRACT_DIR)

