-include ./configs/$(TARGET_PLATFORM_NAME)-variables.mk


GSTLIBAV_NAME := gst-libav
GSTLIBAV_PKG := src/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION).tar.xz

GSTLIBAV_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)
GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/em-libav

GSTLIBAV_LDFLAGS=-L$(PLATFORM_STAGING_DIR)/lib
GSTLIBAV_CFLAGS=-I$(PLATFORM_STAGING_DIR)/include

.PHONY: extract all

all: extract libav configure
	@echo "building gst-libav..."
	cd $(GSTLIBAV_TARGET_BUILD_DIR); make
	cd $(GSTLIBAV_TARGET_BUILD_DIR); make install

libav: $(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR)/.config
	cd $(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR); make
	cd $(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR); make install

$(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR)
	cd $(GSTLIBAV_EM_LIBAV_TARGET_BUILD_DIR); \
	PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
	CFLAGS="$(LIBAV_CFLAGS)" \
	LDFLAGS="$(LIBAV_LDFLAGS)" \
	$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/gst-libs/ext/libav/configure \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--disable-avserver \
		--disable-avplay \
		--disable-avconv \
		--disable-avprobe \
		--enable-shared \
		--enable-pic\
		--disable-encoder=flac \
		--disable-decoder=cavs \
		--disable-protocols \
		--disable-devices  \
		--disable-network \
		--disable-hwaccels \
		--disable-filters \
		--disable-doc \
		--disable-ffmpeg \
		--enable-optimizations \
		--enable-cross-compile \
		--arch=arm \
		--cross-prefix=arm-lg115x-linux-gnueabi- \
		--enable-zlib \
		--enable-bzlib  \
		--enable-neon \
		--target-os=linux
	touch $@


patch: $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/.patch


$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/.patch:
	cd $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION); \
	patch -p1 < $(PKG_DIR)/$(GSTLIBAV_NAME)/cross-compile.patch
	touch $@

configure: $(GSTLIBAV_TARGET_BUILD_DIR)/.config

$(GSTLIBAV_TARGET_BUILD_DIR)/.config:
	mkdir -p $(GSTLIBAV_TARGET_BUILD_DIR)
	cd $(GSTLIBAV_TARGET_BUILD_DIR); \
	$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)/configure \
		PKG_CONFIG_PATH="$(PLATFORM_STAGING_DIR)/lib/pkgconfig" \
		CFLAGS="$(GSTLIBAV_CFLAGS)" \
		LDFLAGS="$(GSTLIBAV_LDFLAGS)" \
		AR="$(TARGET_ARCH)-ar" \
		AS="$(TARGET_ARCH)-as" \
		as="$(TARGET_ARCH)-as" \
		--host=$(TARGET_ARCH) \
		--prefix=$(PLATFORM_STAGING_DIR) \
		--enable-shared \
		--with-system-libav 
	touch $@

extract: $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)

$(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(GSTLIBAV_NAME)-$(GSTLIBAV_VERSION)
	xz -dc $(GSTLIBAV_PKG) | tar xvf - -C $(EXTRACT_DIR)

