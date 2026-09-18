################################################################################
#
# divinus
#
################################################################################

# FH8626 Divinus hardware-test staging pin. This direction branch must build
# the exact native-HAL candidate under test; replace with OpenIPC provenance
# once the implementation is accepted upstream.
DIVINUS_SITE = $(call github,ArthurKoba,openipc-divinus,$(DIVINUS_VERSION))
DIVINUS_VERSION = f986a82f309b8794a5aae251589c6a5d07690c53
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
