################################################################################
#
# majestic-fh8852v200-compat
#
################################################################################

MAJESTIC_FH8852V200_COMPAT_VERSION = master
MAJESTIC_FH8852V200_COMPAT_SITE = https://openipc.s3-eu-west-1.amazonaws.com
MAJESTIC_FH8852V200_COMPAT_SOURCE = majestic.fh8852v200.lite.master.tar.bz2
MAJESTIC_FH8852V200_COMPAT_LICENSE = PROPRIETARY
MAJESTIC_FH8852V200_COMPAT_LICENSE_FILES = LICENSE

MAJESTIC_FH8852V200_COMPAT_DEPENDENCIES = \
	json-c \
	libevent-openipc \
	libogg-openipc \
	libyaml \
	mbedtls-openipc \
	opus-openipc

MAJESTIC_FH8852V200_COMPAT_VENDOR_LIBDIR = \
	$(BR2_EXTERNAL_GENERAL_PATH)/package/fullhan-osdrv-fh8852v200/files/lib

# This is the dependency closure observed on the hardware-proven control-plane
# experiment. Do not copy the whole FH8852V200 OSDRV package: its kernel
# modules, firmware, load scripts and unrelated userspace libraries do not
# belong on FH8626V100.
MAJESTIC_FH8852V200_COMPAT_VENDOR_LIBS = \
	libadvapi.so \
	libadvapi_isp.so \
	libadvapi_smartir.so \
	libdsp.so \
	libisp.so \
	libispcore.so \
	libmipi.so \
	libvmm.so

define MAJESTIC_FH8852V200_COMPAT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/fh8626-majestic-abi-probe \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/src/fh8626-majestic-abi-probe.c \
		-ldl
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -fPIC -shared \
		-Wl,-soname,libgc1054_mipi.so \
		-o $(@D)/libgc1054_mipi.so \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/src/fh8626-gc1054-fh8852-compat.c \
		-ldl
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -fPIC -shared \
		-Wl,-soname,libdsp.so \
		-o $(@D)/libdsp.so \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/src/fh8626-dsp-fh8852-compat.c
endef

define MAJESTIC_FH8852V200_COMPAT_INSTALL_TARGET_CMDS
	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/libexec/majestic-fh8852v200
	$(INSTALL) -m 755 $(@D)/majestic \
		$(TARGET_DIR)/usr/libexec/majestic-fh8852v200/majestic
	$(INSTALL) -m 755 $(@D)/fh8626-majestic-abi-probe \
		$(TARGET_DIR)/usr/libexec/majestic-fh8852v200/fh8626-majestic-abi-probe

	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/lib/majestic-fh8626
	$(INSTALL) -m 755 $(@D)/libgc1054_mipi.so \
		$(TARGET_DIR)/usr/lib/majestic-fh8626/libgc1054_mipi.so
	$(INSTALL) -m 755 $(@D)/libdsp.so \
		$(TARGET_DIR)/usr/lib/majestic-fh8626/libdsp.so

	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/lib/majestic-fh8852v200
	for lib in $(MAJESTIC_FH8852V200_COMPAT_VENDOR_LIBS); do \
		$(INSTALL) -m 644 \
			$(MAJESTIC_FH8852V200_COMPAT_VENDOR_LIBDIR)/$$lib \
			$(TARGET_DIR)/usr/lib/majestic-fh8852v200/$$lib; \
	done

	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 755 \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/files/majestic-fh8852v200-run \
		$(TARGET_DIR)/usr/bin/majestic
	$(INSTALL) -m 755 \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/files/majestic-fh8626-abi-probe \
		$(TARGET_DIR)/usr/bin/majestic-fh8626-abi-probe
	$(INSTALL) -m 755 \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/files/majestic-fh8626-media-run \
		$(TARGET_DIR)/usr/bin/majestic-fh8626-media-run

	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc
	$(INSTALL) -m 644 \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/files/majestic-http-only.yaml \
		$(TARGET_DIR)/etc/majestic.yaml

	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc/init.d
	$(INSTALL) -m 755 \
		$(MAJESTIC_FH8852V200_COMPAT_PKGDIR)/files/S95majestic \
		$(TARGET_DIR)/etc/init.d/S95majestic
endef

$(eval $(generic-package))
