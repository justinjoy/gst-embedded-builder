-include ./configs/$(PLATFORM_CHIP)-variables.mk


LIBFFI_NAME := libffi
LIBFFI_PKG := src/$(LIBFFI_NAME)-$(LIBFFI_VERSION).tar.gz

LIBFFI_HOST_BUILD_DIR := $(HOST_BUILD_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)

.PHONY: extract all

all: extract configure
	@echo "building libffi for host..."
	cd $(LIBFFI_HOST_BUILD_DIR); make
	cd $(LIBFFI_HOST_BUILD_DIR); make install

configure: $(LIBFFI_HOST_BUILD_DIR)/.config

$(LIBFFI_HOST_BUILD_DIR)/.config:
	mkdir -p $(LIBFFI_HOST_BUILD_DIR)
	cd $(LIBFFI_HOST_BUILD_DIR); \
	$(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)/configure \
		CFLAGS="$(LIBFFI_CFLAGS)" \
		LDFLAGS="$(LIBFFI_LDFLAGS)" \
		--prefix=$(HOST_STAGING_DIR) 
	touch $@


extract: $(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)

$(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(LIBFFI_NAME)-$(LIBFFI_VERSION)
	tar zxvf $(LIBFFI_PKG) -C $(EXTRACT_DIR)

