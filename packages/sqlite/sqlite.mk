-include ./configs/$(PLATFORM_CHIP)-variables.mk


SQLITE_NAME := sqlite-autoconf
SQLITE_PKG := src/$(SQLITE_NAME)-$(SQLITE_VERSION).tar.gz

SQLITE_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(SQLITE_NAME)-$(SQLITE_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building sqlite..."
	cd $(SQLITE_TARGET_BUILD_DIR); make
	cd $(SQLITE_TARGET_BUILD_DIR); make install

configure: $(SQLITE_TARGET_BUILD_DIR)/.config

$(SQLITE_TARGET_BUILD_DIR)/.config:
	mkdir -p $(SQLITE_TARGET_BUILD_DIR)
	cd $(SQLITE_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(SQLITE_NAME)-$(SQLITE_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(SQLITE_CFLAGS)" \
		LDFLAGS="$(SQLITE_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) 
	touch $@

extract: $(EXTRACT_DIR)/$(SQLITE_NAME)-$(SQLITE_VERSION)

$(EXTRACT_DIR)/$(SQLITE_NAME)-$(SQLITE_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(SQLITE_NAME)-$(SQLITE_VERSION)
	tar zxvf $(SQLITE_PKG) -C $(EXTRACT_DIR)

