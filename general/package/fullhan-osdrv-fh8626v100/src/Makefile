override CPPFLAGS += -Icontrol -Isensor/gc1054 -Iisp -Iae -Iawb_ccm -Idiagnostics -Iplatform -Iboard
override CFLAGS += -Wall -Wextra -fno-strict-aliasing

SOURCES = \
	media/fh8626_media_owner_v4_3_0.c \
	media/fh8626_sidecar_publisher.c \
	media/fh8626_geometry.c \
	media/fh8626_geometry_linux.c \
	isp/fh8626_isp_runtime.c \
	ae/fh8626_ae_runtime.c \
	ae/fh8626_stock_ae_state_ref.c \
	sensor/gc1054/fh8626_sensor_gc1054.c \
	awb_ccm/fh8626_stock_awb_mode0_pipeline.c \
	awb_ccm/fh8626_stock_awb_mode0_ref.c \
	awb_ccm/fh8626_stock_c9f68_ref.c \
	diagnostics/fh8626_stock_ca4f4_diag.c \
	awb_ccm/fh8626_stock_d1724_ref.c \
	board/fh8626_gpio.c \
	board/fh8626_board_dualsensor.c \
	platform/mmap_uclibc_compat.S

# Recovered ABI adapters are part of the owner, not an offline-only workspace
# artifact. They provide the exact NR3D lifecycle and H.264 RC wire mapping.
SOURCES += \
	media_contracts/nr3d/fh8626_nr3d_kernel.c \
	media_contracts/h264/fh8626_h264_control.c \
	media_contracts/h264/fh8626_h264_app_rc.c

.PHONY: all clean FORCE

all: fh8626-media-owner fh8626-audio-rtx

# The Buildroot local source backend copies the whole package source directory.
# Never trust an ELF that may have been left there by a developer build: force
# every package build to relink each userspace executable with TARGET_CC.
FORCE:

fh8626-media-owner: $(SOURCES) FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Wl,-E -o $@ $(SOURCES) -ldl -pthread

fh8626-audio-rtx: audio/fh8626_audio_test.c FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f fh8626-media-owner fh8626-audio-rtx
