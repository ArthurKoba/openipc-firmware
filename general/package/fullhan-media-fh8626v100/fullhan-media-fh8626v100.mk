################################################################################
#
# fullhan-media-fh8626v100
#
################################################################################

FULLHAN_MEDIA_FH8626V100_VERSION = f4bf49da6ef355c9e733e00d774efe403513b1d4
FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE = https://raw.githubusercontent.com/ArthurKoba/openipc-firmware/$(FULLHAN_MEDIA_FH8626V100_VERSION)/general/package/fullhan-osdrv-fh8626v100/files
FULLHAN_MEDIA_FH8626V100_SITE = $(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/firmware
FULLHAN_MEDIA_FH8626V100_SOURCE = rtthread_arc.bin
FULLHAN_MEDIA_FH8626V100_EXTRA_DOWNLOADS = \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/bgm.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/enc.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/gpio_wave.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/isp.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/jpeg.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/media_process.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/vmm.ko \
	$(FULLHAN_MEDIA_FH8626V100_ARCHIVE_BASE)/kmod/xbus_rpc.ko
FULLHAN_MEDIA_FH8626V100_LICENSE = Proprietary
FULLHAN_MEDIA_FH8626V100_REDISTRIBUTE = NO

define FULLHAN_MEDIA_FH8626V100_EXTRACT_CMDS
	mkdir -p $(@D)/firmware $(@D)/kmod
	cp $(FULLHAN_MEDIA_FH8626V100_DL_DIR)/rtthread_arc.bin $(@D)/firmware/
	for file in bgm.ko enc.ko gpio_wave.ko isp.ko jpeg.ko media_process.ko vmm.ko xbus_rpc.ko; do \
		cp $(FULLHAN_MEDIA_FH8626V100_DL_DIR)/$$file $(@D)/kmod/$$file; \
	done
endef

define FULLHAN_MEDIA_FH8626V100_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/firmware/rtthread_arc.bin \
		$(TARGET_DIR)/lib/firmware/rtthread_arc.bin
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/lib/modules/4.9.129/extra
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/lib/modules/4.9.129/extra \
		$(@D)/kmod/*.ko
	$(INSTALL) -D -m 0755 \
		$(FULLHAN_MEDIA_FH8626V100_PKGDIR)/files/fh-media-modules \
		$(TARGET_DIR)/usr/sbin/fh-media-modules
	$(INSTALL) -D -m 0755 \
		$(FULLHAN_MEDIA_FH8626V100_PKGDIR)/files/load_fullhan \
		$(TARGET_DIR)/usr/bin/load_fullhan
endef

$(eval $(generic-package))
