-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk

GNUTLS_NAME := gnutls
GNUTLS_PKG := src/$(GNUTLS_NAME)-$(GNUTLS_VERSION).tar.xz

GNUTLS_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GNUTLS_NAME)-$(GNUTLS_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building gnutls..."
	cd $(GNUTLS_TARGET_BUILD_DIR); make
	cd $(GNUTLS_TARGET_BUILD_DIR); make install

configure: $(GNUTLS_TARGET_BUILD_DIR)/.config

$(GNUTLS_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GNUTLS_TARGET_BUILD_DIR)
	cd $(GNUTLS_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GNUTLS_NAME)-$(GNUTLS_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GNUTLS_CFLAGS)" \
		LDFLAGS="$(GNUTLS_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--with-libnettle-prefix=$(PLATFORM_STAGING_DIR) \
		--disable-crywrap 
	touch $@

extract: $(EXTRACT_DIR)/$(GNUTLS_NAME)-$(GNUTLS_VERSION)

$(EXTRACT_DIR)/$(GNUTLS_NAME)-$(GNUTLS_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GNUTLS_NAME)-$(GNUTLS_VERSION)
	xz -dc $(GNUTLS_PKG) | tar xvf - -C $(EXTRACT_DIR)

