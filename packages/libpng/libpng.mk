-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


LIBPNG_NAME := libpng
LIBPNG_PKG := src/$(LIBPNG_NAME)-$(LIBPNG_VERSION).tar.xz

LIBPNG_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(LIBPNG_NAME)-$(LIBPNG_VERSION)

LIBPNG_CFLAGS = -I$(PLATFORM_STAGING_DIR)/include
LIBPNG_LDFLAGS = -L$(PLATFORM_STAGING_DIR)/lib

.PHONY: extract all

all: extract configure
	@echo "building libpng ..."
	cd $(LIBPNG_TARGET_BUILD_DIR); make
	cd $(LIBPNG_TARGET_BUILD_DIR); make install

configure: $(LIBPNG_TARGET_BUILD_DIR)/.config

$(LIBPNG_TARGET_BUILD_DIR)/.config:
	mkdir -p $(LIBPNG_TARGET_BUILD_DIR)
	cd $(LIBPNG_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBPNG_NAME)-$(LIBPNG_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(LIBPNG_CFLAGS)" \
		LDFLAGS="$(LIBPNG_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--with-pkgconfigdir="$(PLATFORM_STAGING_DIR)/lib/pkgconfig"
	touch $@

extract: $(EXTRACT_DIR)/$(LIBPNG_NAME)-$(LIBPNG_VERSION)

$(EXTRACT_DIR)/$(LIBPNG_NAME)-$(LIBPNG_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBPNG_NAME)-$(LIBPNG_VERSION)
	xz -dc $(LIBPNG_PKG) | tar xvf - -C $(EXTRACT_DIR)

