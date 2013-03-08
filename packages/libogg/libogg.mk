-include ./configs/$(PLATFORM_CHIP)-variables.mk


LIBOGG_NAME := libogg
LIBOGG_PKG := src/$(LIBOGG_NAME)-$(LIBOGG_VERSION).tar.xz

LIBOGG_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(LIBOGG_NAME)-$(LIBOGG_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building libogg..."
	cd $(LIBOGG_TARGET_BUILD_DIR); make
	cd $(LIBOGG_TARGET_BUILD_DIR); make install

configure: $(LIBOGG_TARGET_BUILD_DIR)/.config

$(LIBOGG_TARGET_BUILD_DIR)/.config:
	mkdir -p $(LIBOGG_TARGET_BUILD_DIR)
	cd $(LIBOGG_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBOGG_NAME)-$(LIBOGG_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(LIBOGG_CFLAGS)" \
		LDFLAGS="$(LIBOGG_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) 
	touch $@

extract: $(EXTRACT_DIR)/$(LIBOGG_NAME)-$(LIBOGG_VERSION)

$(EXTRACT_DIR)/$(LIBOGG_NAME)-$(LIBOGG_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBOGG_NAME)-$(LIBOGG_VERSION)
	xz -dc $(LIBOGG_PKG) | tar xvf - -C $(EXTRACT_DIR)

