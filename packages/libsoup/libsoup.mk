-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


LIBSOUP_NAME := libsoup
LIBSOUP_PKG := src/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION).tar.xz

LIBSOUP_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building gst-plugins-base..."
	cd $(LIBSOUP_TARGET_BUILD_DIR); make
	cd $(LIBSOUP_TARGET_BUILD_DIR); make install

configure: $(LIBSOUP_TARGET_BUILD_DIR)/.config

$(LIBSOUP_TARGET_BUILD_DIR)/.config:
	mkdir -p $(LIBSOUP_TARGET_BUILD_DIR)
	cd $(LIBSOUP_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(LIBSOUP_CFLAGS)" \
		LDFLAGS="$(LIBSOUP_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR)  \
		--without-gnome 
	touch $@

extract: $(EXTRACT_DIR)/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION)

$(EXTRACT_DIR)/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBSOUP_NAME)-$(LIBSOUP_VERSION)
	xz -dc $(LIBSOUP_PKG) | tar xvf - -C $(EXTRACT_DIR)

