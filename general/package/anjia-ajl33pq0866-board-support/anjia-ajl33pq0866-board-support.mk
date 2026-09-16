################################################################################
#
# anjia-ajl33pq0866-board-support
#
################################################################################

ANJIA_AJL33PQ0866_BOARD_SUPPORT_SITE_METHOD = local
ANJIA_AJL33PQ0866_BOARD_SUPPORT_SITE = $(ANJIA_AJL33PQ0866_BOARD_SUPPORT_PKGDIR)/src
ANJIA_AJL33PQ0866_BOARD_SUPPORT_DEPENDENCIES = divinus

define ANJIA_AJL33PQ0866_BOARD_SUPPORT_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" -C $(@D)
endef

define ANJIA_AJL33PQ0866_BOARD_SUPPORT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fh8626-ptz \
		$(TARGET_DIR)/usr/sbin/fh8626-ptz
	cp -a $(ANJIA_AJL33PQ0866_BOARD_SUPPORT_PKGDIR)/files/. $(TARGET_DIR)/
endef

$(eval $(generic-package))
