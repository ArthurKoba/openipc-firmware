# FH8626V100 Majestic compatibility package

Status: FULL_FEATURE_OFFLINE_CLOSURE / BUILD_AND_HARDWARE_PENDING

Majestic HTTP/WebUI remains native and owns port 80 directly. This package does
not patch frontend JavaScript and does not proxy the Majestic HTTP server.

## Source compatibility coverage

The active FH8852V200 Majestic feature surface is translated onto recovered
FH8626V100 contracts:

- source GC1054 sensor and MIPI compatibility;
- source VMM allocation ABI;
- SYS / VPSS / multi-channel H.264 VENC and shared-stream ownership;
- full H.264 channel attributes, GOP and stock RC modes with readback/runtime RC;
- native JPEG snapshot and MJPEG configuration, RC, stream, rotate and drop control;
- motion YC-mean / CPY backend through libadvapi_md;
- OSD GraphV2 backend through libadvapi_osd;
- source RTX audio including retail DSP init, AI/AO, AEC/NR/AGC and extension APIs.

H.265 is deliberately unsupported because the stock FH8626 encoder stack does
not register an HEVC engine. Unsupported SDK-only exports are explicit errors,
not permissive fake-success paths.

Build-time ABI checks reject both direct Majestic imports and transitive donor
library imports that reach an unsupported Fullhan API.

## Manual runner ladder

The default boot remains media-off.

- `majestic-fh8626-media-run`: strict donor/compatibility discovery, native
  encoder disabled.
- `majestic-fh8626-stub-run`: permissive discovery only.
- `majestic-fh8626-native-video-run`: legacy native-video discovery with
  permissive optional stubs.
- `majestic-fh8626-native-av-run`: video/audio discovery profile.
- `majestic-fh8626-full-run`: final offline acceptance profile. It enables
  native VENC with **no permissive stubs** and exercises main + sub H.264,
  JPEG snapshot, OSD, motion, capture/playback audio and RTSP together.

`majestic-fh8626-full-run` bind-mounts the dedicated full-feature YAML only
for the process lifetime. It does not modify the persistent default config.

## Remaining gate

The useful next step after a successful build is target evidence, not more
speculative SDK emulation:

1. boot the unchanged media-off control-plane baseline;
2. run the ABI probe;
3. run strict/native single-video tests if a failure must be localized;
4. run `majestic-fh8626-full-run`;
5. prove simultaneous main/sub RTSP, JPEG, OSD/motion and audio;
6. exercise runtime bitrate/RC/GOP/readback and repeated restart/reconfigure;
7. regress day/night, lens, PTZ, storage and shutdown/restart.

Crop/slice/extra codec SDK functions that are not imported or exposed by the
current Fullhan Majestic build remain explicit unsupported boundaries. If a
future moving Majestic begins importing one, the build guard must fail before
the image reaches hardware.
