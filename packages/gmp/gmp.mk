-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk

GMP_NAME := gmp
GMP_PKG := src/$(GMP_NAME)-$(GMP_VERSION).tar.xz

GMP_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GMP_NAME)-$(GMP_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building gmp..."
	cd $(GMP_TARGET_BUILD_DIR); make
	cd $(GMP_TARGET_BUILD_DIR); make install

configure: $(GMP_TARGET_BUILD_DIR)/.config

$(GMP_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GMP_TARGET_BUILD_DIR)
	cd $(GMP_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GMP_NAME)-$(GMP_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GMP_CFLAGS)" \
		LDFLAGS="$(GMP_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR)
	touch $@

extract: $(EXTRACT_DIR)/$(GMP_NAME)-$(GMP_VERSION)

$(EXTRACT_DIR)/$(GMP_NAME)-$(GMP_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GMP_NAME)-$(GMP_VERSION)
	xz -dc $(GMP_PKG) | tar xvf - -C $(EXTRACT_DIR)

