-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk

LIBCURL_NAME := curl
LIBCURL_PKG := src/$(LIBCURL_NAME)-$(LIBCURL_VERSION).tar.gz
LIBCURL_DOWNLOAD := http://curl.haxx.se/download/$(LIBCURL_NAME)-$(LIBCURL_VERSION).tar.gz

LIBCURL_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(LIBCURL_NAME)-$(LIBCURL_VERSION)

.PHONY: extract all

all: download extract configure
	@echo "building libcurl..."
	cd $(LIBCURL_TARGET_BUILD_DIR); make
	cd $(LIBCURL_TARGET_BUILD_DIR); make install

download: $(LIBCURL_PKG)

$(LIBCURL_PKG):
	wget $(LIBCURL_DOWNLOAD) -P src

configure: $(LIBCURL_TARGET_BUILD_DIR)/.config

$(LIBCURL_TARGET_BUILD_DIR)/.config:
	mkdir -p $(LIBCURL_TARGET_BUILD_DIR)
	cd $(LIBCURL_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBCURL_NAME)-$(LIBCURL_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(LIBCURL_CFLAGS)" \
		LDFLAGS="$(LIBCURL_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR)
	touch $@

extract: $(EXTRACT_DIR)/$(LIBCURL_NAME)-$(LIBCURL_VERSION)

$(EXTRACT_DIR)/$(LIBCURL_NAME)-$(LIBCURL_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBCURL_NAME)-$(LIBCURL_VERSION)
	tar zxvf $(LIBCURL_PKG) -C $(EXTRACT_DIR)

