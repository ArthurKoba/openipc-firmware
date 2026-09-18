################################################################################
#
# divinus
#
################################################################################

# Keep normal OpenIPC Divinus provenance for every existing target. FH8626
# alone is pinned to the exact native-HAL candidate under software acceptance.
ifeq ($(BR2_OPENIPC_SOC_MODEL),fh8626v100)
DIVINUS_SITE = $(call github,ArthurKoba,openipc-divinus,$(DIVINUS_VERSION))
DIVINUS_VERSION = 50e3e300bb92b609e230ab9af1cf712f49c95a39
else
DIVINUS_SITE = $(call github,openipc,divinus,$(DIVINUS_VERSION))
DIVINUS_VERSION = HEAD
endif
DIVINUS_LICENSE = MIT
DIVINUS_LICENSE_FILES = LICENSE

ifeq ($(BR2_TOOLCHAIN_USES_GLIBC),y)
	DIVINUS_OPTIONS = "-rdynamic -s -Os -lm"
else
	DIVINUS_OPTIONS = "-rdynamic -s -Os"
endif

# Make the test image independent of compiler predefined-architecture spelling.
# Runtime identification still decides whether the FH8626 provider is selected.
ifeq ($(BR2_OPENIPC_SOC_MODEL),fh8626v100)
	DIVINUS_OPTIONS = "-rdynamic -s -Os -DFH8626_NATIVE_KERNEL"
endif

define DIVINUS_BUILD_CMDS
	$(MAKE) CC=$(TARGET_CC) OPT=$(DIVINUS_OPTIONS) -C $(@D)/src
endef

define DIVINUS_INSTALL_TARGET_CMDS
	$(INSTALL) -m 755 -d $(TARGET_DIR)/etc
	$(INSTALL) -m 644 -t $(TARGET_DIR)/etc $(@D)/divinus.yaml

	$(INSTALL) -m 755 -d $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 755 -t $(TARGET_DIR)/usr/bin $(@D)/divinus
endef

$(eval $(generic-package))
