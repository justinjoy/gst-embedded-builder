-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


NETTLE_NAME := nettle
NETTLE_PKG := src/$(NETTLE_NAME)-$(NETTLE_VERSION).tar.gz

NETTLE_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(NETTLE_NAME)-$(NETTLE_VERSION)
NETTLE_CFLAGS = -I$(PLATFORM_STAGING_DIR)/include
NETTLE_LDFLAGS = -L$(PLATFORM_STAGING_DIR)/lib

.PHONY: extract all

all: extract configure
	@echo "building nettle..."
	cd $(NETTLE_TARGET_BUILD_DIR); make
	cd $(NETTLE_TARGET_BUILD_DIR); make install

configure: $(NETTLE_TARGET_BUILD_DIR)/.config

$(NETTLE_TARGET_BUILD_DIR)/.config:
	mkdir -p $(NETTLE_TARGET_BUILD_DIR)
	cd $(NETTLE_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(NETTLE_NAME)-$(NETTLE_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(NETTLE_CFLAGS)" \
		CPPFLAGS="$(NETTLE_CFLAGS)" \
		LDFLAGS="$(NETTLE_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR)
		--with-include-path=$(PLATFORM_STAGING_DIR)/include \
		--with-lib-path=$(PLATFORM_STAGING_DIR)/lib \
	touch $@

extract: $(EXTRACT_DIR)/$(NETTLE_NAME)-$(NETTLE_VERSION)

$(EXTRACT_DIR)/$(NETTLE_NAME)-$(NETTLE_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(NETTLE_NAME)-$(NETTLE_VERSION)
	tar xvzf $(NETTLE_PKG) -C $(EXTRACT_DIR)

