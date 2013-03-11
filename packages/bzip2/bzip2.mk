-include ./configs/$(PLATFORM_CHIP)-variables.mk


CC=$(TARGET_ARCHO)-gcc

BZIP2_NAME := bzip2
BZIP2_PKG := src/$(BZIP2_NAME)-$(BZIP2_VERSION).tar.gz

BZIP2_TARGET_BUILD_DIR := $(PLATFORM_BUILD_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)

BZIP2_CFLAGS=-fpic -fPIC -Wall -Winline -O2 -g -D_FILE_OFFSET_BITS=64

.PHONY: extract all build

OBJS= $(BZIP2_TARGET_BUILD_DIR)/blocksort.o \
	$(BZIP2_TARGET_BUILD_DIR)/huffman.o    \
	$(BZIP2_TARGET_BUILD_DIR)/crctable.o   \
	$(BZIP2_TARGET_BUILD_DIR)/randtable.o  \
	$(BZIP2_TARGET_BUILD_DIR)/compress.o   \
	$(BZIP2_TARGET_BUILD_DIR)/decompress.o \
	$(BZIP2_TARGET_BUILD_DIR)/bzlib.o

all: extract build install

install:
	mkdir -p $(PLATFORM_STAGING_DIR)/include
	mkdir -p $(PLATFORM_STAGING_DIR)/lib/pkgconfig
	cp -f $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/bzlib.h $(PLATFORM_STAGING_DIR)/include
	cp -a $(BZIP2_TARGET_BUILD_DIR)/libbz2.so.1.0.6 $(PLATFORM_STAGING_DIR)/lib
	cd $(PLATFORM_STAGING_DIR)/lib; \
	ln -s libbz2.so.1.0.6 libbz2.so; \
	ln -s libbz2.so.1.0.6 libbz2.so.1.0
	

build: $(OBJS)
	$(CC) -shared -Wl,-soname -Wl,libbz2.so.1.0 -o $(BZIP2_TARGET_BUILD_DIR)/libbz2.so.1.0.6 $(OBJS)

extract: $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)

$(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION):
	mkdir -p $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)
	mkdir -p $(BZIP2_TARGET_BUILD_DIR)
	tar zxvf $(BZIP2_PKG) -C $(EXTRACT_DIR)

$(BZIP2_TARGET_BUILD_DIR)/blocksort.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/blocksort.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/huffman.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/huffman.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/crctable.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/crctable.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/randtable.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/randtable.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/compress.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/compress.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/decompress.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/decompress.c -o $@

$(BZIP2_TARGET_BUILD_DIR)/bzlib.o:
	$(CC) $(BZIP2_CFLAGS) -c $(EXTRACT_DIR)/$(BZIP2_NAME)-$(BZIP2_VERSION)/bzlib.c -o $@
