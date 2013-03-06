-include ./configs/$(PLATFORM_CHIP)-variables.mk


GLIB_NAME := glib
GLIB_PKG := src/$(GLIB_NAME)-$(GLIB_VERSION).tar.xz

GLIB_HOST_BUILD_DIR := $(HOST_BUILD_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building glib2 for host..."
	cd $(GLIB_HOST_BUILD_DIR); make
	cd $(GLIB_HOST_BUILD_DIR); make install

configure: $(GLIB_HOST_BUILD_DIR)/.config

$(GLIB_HOST_BUILD_DIR)/.config: 
	mkdir -p $(GLIB_HOST_BUILD_DIR) 
	cd $(GLIB_HOST_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)/configure \
		PKG_CONFIG_PATH="$(HOST_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GLIB_CFLAGS)" \
		LDFLAGS="$(GLIB_LDFLAGS)" \
		--enable-debug=yes \
		--prefix=$(HOST_STAGING_DIR) 
	touch $@

extract: $(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)

$(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GLIB_NAME)-$(GLIB_VERSION)
	xz -dc $(GLIB_PKG) | tar xvf - -C $(EXTRACT_DIR)

