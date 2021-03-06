-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk

P11KIT_NAME := p11-kit
P11KIT_PKG := src/$(P11KIT_NAME)-$(P11KIT_VERSION).tar.gz

P11KIT_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(P11KIT_NAME)-$(P11KIT_VERSION)

P11KIT_CFLAGS = -I $(PLATFORM_STAGING_DIR)/include 

.PHONY: extract all

all: extract configure
	@echo "building p11-kit..."
	cd $(P11KIT_TARGET_BUILD_DIR); make
	cd $(P11KIT_TARGET_BUILD_DIR); make install

configure: $(P11KIT_TARGET_BUILD_DIR)/.config

$(P11KIT_TARGET_BUILD_DIR)/.config:
	mkdir -p $(P11KIT_TARGET_BUILD_DIR)
	cd $(P11KIT_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(P11KIT_NAME)-$(P11KIT_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(P11KIT_CFLAGS)" \
		LDFLAGS="$(P11KIT_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) 
	touch $@

extract: $(EXTRACT_DIR)/$(P11KIT_NAME)-$(P11KIT_VERSION)

$(EXTRACT_DIR)/$(P11KIT_NAME)-$(P11KIT_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(P11KIT_NAME)-$(P11KIT_VERSION)
	tar zxvf $(P11KIT_PKG) -C $(EXTRACT_DIR)

