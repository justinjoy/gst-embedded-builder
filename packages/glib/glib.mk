-include ./configs/$(PLATFORM_CHIP)-variables.mk


GLIB_NAME := glib
GLIB_PKG := src/$(GLIB_NAME)-$(GLIB_VERSION).tar.xz

GLIB_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building glib2..."
	cd $(GLIB_TARGET_BUILD_DIR); make
	cd $(GLIB_TARGET_BUILD_DIR); make install

configure: $(GLIB_TARGET_BUILD_DIR)/.config

$(GLIB_TARGET_BUILD_DIR)/.config: $(GLIB_TARGET_BUILD_DIR)/$(GLIB_NAME)-$(GLIB_VERSION).config.cache
	cd $(GLIB_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GLIB_CFLAGS)" \
		LDFLAGS="$(GLIB_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--enable-debug=yes \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--disable-modular-tests \
		--cache-file=$(GLIB_TARGET_BUILD_DIR)/$(GLIB_NAME)-$(GLIB_VERSION).config.cache
	touch $@

$(GLIB_TARGET_BUILD_DIR)/$(GLIB_NAME)-$(GLIB_VERSION).config.cache:
	mkdir -p $(GLIB_TARGET_BUILD_DIR)
	cp $(PKG_DIR)/glib/$(GLIB_NAME)-$(GLIB_VERSION).config.cache $@
	chmod a-w $@
	

extract: $(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)

$(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)
	xz -dc $(GLIB_PKG) | tar xvf - -C $(EXTRACT_DIR)

