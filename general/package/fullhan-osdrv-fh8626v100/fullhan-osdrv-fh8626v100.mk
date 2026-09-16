################################################################################
#
# fullhan-osdrv-fh8626v100
#
################################################################################

FULLHAN_OSDRV_FH8626V100_SITE_METHOD = local
FULLHAN_OSDRV_FH8626V100_SITE = $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/src
FULLHAN_OSDRV_FH8626V100_LICENSE = Proprietary

define FULLHAN_OSDRV_FH8626V100_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" -C $(@D)
	$(TARGET_CROSS)readelf -l $(@D)/fh8626-media-owner | \
		grep -q 'Requesting program interpreter: .*musl' || \
		{ echo "ERROR: fh8626-media-owner is not dynamically linked against target musl" >&2; exit 1; }
endef

define FULLHAN_OSDRV_FH8626V100_INSTALL_TARGET_CMDS
	# PTZ is board-specific. Remove files retained by an incremental
	# per-package tree from revisions where this generic package owned it.
	rm -f $(TARGET_DIR)/usr/sbin/fh8626-ptz \
		$(TARGET_DIR)/usr/bin/gpio-motors \
		$(TARGET_DIR)/etc/init.d/S69fh8626-ptz \
		$(TARGET_DIR)/etc/default/fh8626-ptz
	$(INSTALL) -D -m 0755 $(@D)/fh8626-media-owner \
		$(TARGET_DIR)/usr/sbin/fh8626-media-owner
	$(INSTALL) -D -m 0755 $(@D)/fh8626-audio-rtx \
		$(TARGET_DIR)/usr/libexec/fh8626-audio-rtx
	$(INSTALL) -D -m 0755 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/fh8626-audio \
		$(TARGET_DIR)/usr/sbin/fh8626-audio
	$(INSTALL) -D -m 0644 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/fh8626-audio.default \
		$(TARGET_DIR)/etc/default/fh8626-audio
	$(INSTALL) -D -m 0755 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/fh-media-modules \
		$(TARGET_DIR)/usr/sbin/fh-media-modules
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/lib/modules/4.9.129/extra
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/lib/modules/4.9.129/extra \
		$(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/kmod/*.ko
	$(INSTALL) -D -m 0644 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/firmware/rtthread_arc.bin \
		$(TARGET_DIR)/lib/firmware/rtthread_arc.bin
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/usr/lib/fh8626
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/usr/lib/fh8626 \
		$(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/lib/*.so \
		$(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/sensor/*.so
	$(INSTALL) -D -m 0644 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/sensor/gc1054_day.bin \
		$(TARGET_DIR)/usr/share/fh8626/gc1054_day.bin
	$(INSTALL) -D -m 0644 $(FULLHAN_OSDRV_FH8626V100_PKGDIR)/files/sensor/sensor_gc1054_mipi.bin \
		$(TARGET_DIR)/usr/share/fh8626/sensor_gc1054_mipi.bin
endef

$(eval $(generic-package))
