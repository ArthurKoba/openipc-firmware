# FH8626V100 Majestic compatibility package

Status: FULL_FEATURE_OFFLINE_CLOSURE / BUILD_GATE_READY / HARDWARE_PENDING

Checked: 2026-09-18.

This package is the FH8626V100 userspace compatibility boundary for running the
FH8852V200 Majestic build on the ANJIA AJL33PQ0866 staging platform. It is a
bring-up/product-validation architecture, not a claim of official FH8626
Majestic support.

Majestic owns its native HTTP server, API, WebSocket and WebUI directly on port
80. This package does not patch frontend JavaScript and does not insert an HTTP
proxy.

## Runtime ownership

The package now explicitly selects `BR2_PACKAGE_FULLHAN_MEDIA_FH8626V100`.
That shared package is the build-time owner for the still-proprietary FH8626
kernel/ARC media runtime:

- `vmm.ko`
- `xbus_rpc.ko`
- `media_process.ko`
- `isp.ko`
- `enc.ko`
- `jpeg.ko`
- `bgm.ko`
- `gpio_wave.ko`
- `rtthread_arc.bin`

Those bytes are not stored on the active Majestic branch. Buildroot downloads
the hardware-proven payloads from immutable Firmware archive commit
`f4bf49da6ef355c9e733e00d774efe403513b1d4` and verifies every file against
`fullhan-media-fh8626v100.hash`.

OpenIPC `S70vendor` invokes `load_fullhan -i`, which loads the runtime in the
recovered order and requires the critical media device nodes before Majestic
acceptance proceeds.

No FH8852 kernel module, ARC firmware, load script or donor sensor plug-in is
installed.

## Majestic-facing source compatibility coverage

The active FH8852V200 Majestic feature surface has source compatibility
boundaries for the FH8626-specific ABI mismatches:

- source GC1054 sensor facade, translating the FH8852 0x7c callback table onto
  the recovered FH8626 0x68 sensor contract. The selected table is now typed
  and complete except the deliberate reserved +0x48 slot; its signatures and
  AE/frame-height semantics were cross-checked in Ghidra against FH8852
  GC4653/JXF32/MN34425, FH8852 libisp/libispcore and stock FH8626 GC1054;
- source MIPI implementation;
- source VMM allocation/mapping facade;
- SYS / VPSS translation including channel lifecycle, frame control, YC mean,
  CPY data and GraphV2;
- multi-channel H.264 VENC, capability records, channel attributes, GOP,
  VBR/CBR/AVBR/CVBR/fixed-QP style RC, realtime RC changes and readback;
- correct main/sub/analytics bind and shared encoded-FIFO channel ownership;
- bounded stream acquisition, ring-wrap conversion and exactly-once release;
- IDR;
- native JPEG snapshot and MJPEG memory/config/RC/start/stop/stream/release,
  rotate, submit and drop controls;
- motion through the retained `libadvapi_md.so` backend over the source VPSS
  facade;
- OSD through retained `libadvapi_osd.so` over source GraphV2;
- source RTX audio MPI including the retail DSP init path, AI/AO, timestamps,
  volume/config and the recovered VQE/AEC/AGC/NR-facing surface;
- board-neutral AO lifecycle hook, with physical speaker mute policy supplied
  by the ANJIA board package;
- ANJIA day/night profile using the recovered IR-cut/IR LED contract.

The retained donor userspace closure is intentionally small:

- `libadvapi.so`
- `libadvapi_isp.so`
- `libadvapi_md.so`
- `libadvapi_osd.so`
- `libadvapi_smartir.so`
- `libisp.so`
- `libispcore.so`

The active GC1054, MIPI, VMM, DSP/VPSS/VENC and ACW MPI paths are source-built
and must not silently fall back to donor equivalents.

The GC1054 facade no longer contains permissive sensor callback stubs. Key
recovered details include:

- FH8852 `GetSensorReg(reg, out)` writes through an output pointer; it is not
  the direct-value native FH8626 read form;
- FH8852 `SetIntt/SetGain` carry an exposure index; the selected linear GC1054
  owns index 0 and treats the second WDR index as inactive;
- `GetAEDefault.word4` is the base format frame length used by FH8852
  `isp_core_set_sensor_frame_height`;
- `GetAEInfo` reports current integration/gain, base line-rate and current
  frame height separately;
- FH8852 integer-command `SensorCommonIf` is not the FH8626 string-query
  control callback and is never bridged positionally;
- `Sensor_Isconnect` is a self-contained I2C ID probe and works before
  `Sensor_Init`;
- same-process destroy/re-create closes sensor state without repeatedly
  accumulating native dlopen references.

The target ABI probe opens the selected sensor facade without starting media,
calls `Sensor_Create()` and verifies every active callback slot from +0x00
through +0x78; only reserved +0x48 may be NULL.

The donor ISP/ispcore/advapi stack is retained because the recovered stock ISP
API is a shared userspace context/state machine, not a simple ioctl shim.
Replacing it with a partial reimplementation without a demonstrated runtime
mismatch would reduce correctness.

## Capability boundaries

H.265/HEVC is deliberately unsupported. The recovered stock FH8626 encoder
stack registers the H.264 engine and does not provide the HEVC backend expected
by platforms that genuinely support H.265. H.265-facing calls therefore fail
explicitly rather than returning fake success.

Crop/slice and other SDK-only exports that are not reached by the selected
Majestic/donor runtime closure remain explicit unsupported boundaries.

Both direct Majestic imports and transitive imports from retained donor
libraries are checked at build time. A future Majestic build that starts
reaching an unsupported Fullhan SDK symbol must fail the ABI guard instead of
producing an image that only appears to work.

## Default boot and acceptance runners

Default boot remains media-off. It is the control-plane baseline and is not
counted as media acceptance.

Installed runners:

- `majestic-fh8626-media-run`: strict discovery with native encoder disabled;
- `majestic-fh8626-stub-run`: permissive diagnostic discovery only;
- `majestic-fh8626-native-video-run`: legacy native-video localization path;
- `majestic-fh8626-native-av-run`: video/audio localization path;
- `majestic-fh8626-full-run`: strict full-feature acceptance profile with
  permissive stubs disabled.

The full runner exercises together:

- H.264 main 1280x720@25;
- H.264 sub 640x360@25;
- JPEG snapshot 640x384;
- OSD;
- motion;
- capture/playback audio;
- RTSP;
- ANJIA day/night/IR-cut wiring.

It bind-mounts its full-feature YAML only for the process lifetime and does not
replace the persistent media-off default configuration.

## Board day/night contract used by the full profile

Recovered ANJIA AJL33PQ0866 contract:

- IR-cut DAY/closed coil: GPIO18;
- IR-cut NIGHT/open coil: GPIO60;
- bistable pulse: 190 ms;
- IR LED: GPIO25 active high;
- white LED: GPIO23 active high and shared with SADC1;
- ambient SADC channel: 1;
- speaker amplifier mute: GPIO24 active high.

The Majestic full profile maps GPIO18/GPIO60 to `irCutPin1/irCutPin2`, GPIO25
to the Majestic backlight output and uses `pinSwitchDelayUs: 190000`.
The white LED is not misrepresented as a second Majestic backlight.

Target acceptance must verify physical DAY/NIGHT direction. Do not hide a
reversed actuator result by silently swapping the documented board contract.

## Build provenance and remaining supply-chain boundary

The shared FH8626 kernel/ARC runtime is immutable and hash-pinned.

The FH8852V200 Majestic executable itself is still obtained from the upstream
moving object:

`majestic.fh8852v200.lite.master.tar.bz2`

Therefore the first owner build must retain the exact downloaded executable
SHA-256 and resolved build provenance. The direct/transitive ABI guards protect
against an incompatible API expansion, but a moving donor is not considered
fully reproducible product supply.

This is not a blocker for the first controlled hardware bring-up if the exact
bytes are recorded. It remains a product-acceptance blocker until an immutable
donor object or official FH8626 Majestic build is available.

## Builder composition

The named target is:

`fh8626v100_lite_anjia-ajl33pq0866_majestic`

Builder composes:

1. Firmware `br-ext-chip-fullhan/configs/fh8626v100_lite_defconfig`;
2. ANJIA `base.config`;
3. the short Majestic runtime fragment.

Its sibling `.firmware` metadata selects
`ArthurKoba/openipc-firmware@work/fh8626v100-majestic` automatically.

Normal build command:

```sh
./builder.sh fh8626v100_lite_anjia-ajl33pq0866_majestic
```

No separate active Majestic Builder branch is required.

## Next gate

Offline implementation work is closed for the currently observed Majestic
feature surface. The next useful evidence is an exact build and target run:

1. build the composed target and retain resolved Builder/Firmware/Linux SHAs;
2. require `uImage <= 2048 KiB`;
3. require `rootfs.squashfs <= 5120 KiB` and record headroom;
4. require the Builder provenance manifest to contain Majestic plus all nine
   shared FH8626 media/ARC payloads and retain archive-level `SHA256SUMS`;
5. boot the unchanged media-off Majestic baseline;
6. verify `S70vendor` created the required media devices;
7. run `majestic-fh8626-abi-probe`; it must pass selected-library loading,
   required Fullhan symbols, required media devices and the complete sensor
   callback table;
8. retain the automatically recorded SHA-256 of the installed Majestic executable;
9. run strict localization only if needed, then `majestic-fh8626-full-run`;
10. prove simultaneous main/sub RTSP, runtime RC/GOP/readback, JPEG, OSD,
    motion, audio/talkback, day/night and repeated restart/reconfigure;
11. regress PTZ, lens, illumination, storage and clean shutdown/restart.

A successful source/build gate is not hardware acceptance. Production readiness
still requires either official FH8626 support in Majestic or a reproducible,
fully characterized compatibility layer backed by target evidence.
