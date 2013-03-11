-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


LIBFFI_NAME := libffi
LIBFFI_PKG := src/$(LIBFFI_NAME)-$(LIBFFI_VERSION).tar.gz

LIBFFI_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building libffi..."
	cd $(LIBFFI_TARGET_BUILD_DIR); make
	cd $(LIBFFI_TARGET_BUILD_DIR); make install

configure: $(LIBFFI_TARGET_BUILD_DIR)/.config

$(LIBFFI_TARGET_BUILD_DIR)/.config:
	mkdir -p $(LIBFFI_TARGET_BUILD_DIR)
	cd $(LIBFFI_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(LIBFFI_CFLAGS)" \
		LDFLAGS="$(LIBFFI_LDFLAGS)" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) 
	touch $@


extract: $(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)

$(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)
	tar zxvf $(LIBFFI_PKG) -C $(EXTRACT_DIR)

