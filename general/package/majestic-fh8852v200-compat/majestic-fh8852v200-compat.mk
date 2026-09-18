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

define MAJESTIC_FH8852V200_COMPAT_INSTALL_TARGET_CMDS
	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/libexec/majestic-fh8852v200
	$(INSTALL) -m 755 $(@D)/majestic \
		$(TARGET_DIR)/usr/libexec/majestic-fh8852v200/majestic

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
