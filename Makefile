-include configs/$(PLATFORM_CHIP)-variables.mk

PACKAGES= zlib libffi glib libogg libvorbis bzip2 gstreamer gst-plugins-base gst-libav

all: env host target


env: 
	@echo "checking environment variables."
ifeq ($(TARGET_ARCH)x, x)
	@echo "user-specified variables required."
	@echo "to set variables, please follow next steps."
	@$(SHELL) configs/build_variables.sh
	@echo "please, re-run building process...."
	@exit 1
else
	@echo "building libraries for [$(TARGET_ARCH)]"
endif

host:
	@if [ -f $(HOST_STAGING_DIR)/.completed ]; then \
		echo "skipping host staging step"; \
	else \
		echo "preparing to host libraries"; \
		make -f pacakges/zlib/zlib-host.mk; \
		make -f pacakges/libffi/libffi-host.mk; \
		make -f pacakges/glib/glib-host.mk; \
	fi 
	
	@echo -e "\033[32m============================================ "
	@echo -e " please, set PATH to build target libaries "
	@echo -e " export PATH=""$$""PATH:$(HOST_STAGING_DIR)/bin"
	@echo -e "============================================\033[0m "

target:	$(PACKAGES)

$(PACKAGES):
	@make -f packages/$(PACKAGES)/$(PACKAGES).mk

