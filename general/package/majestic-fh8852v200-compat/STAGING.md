# FH8626V100 Majestic compatibility package

Status: SOFTWARE_STAGING_COMPLETE / BUILD_AND_HARDWARE_PENDING

This package is the isolated FH8852V200 Majestic compatibility direction for
FH8626V100. It is not the generic FH8626 platform implementation.

## Default control plane

Normal boot remains media-off. Majestic listens only on loopback port 18080.
The source-built `fh8626-majestic-http` proxy owns external port 80.

The proxy preserves ordinary HTTP, Majestic API and WebSocket traffic as a raw
bidirectional tunnel. Only `GET /metrics` is handled locally. It reports real
FH8626 Linux data from `/proc/stat`, `/proc/meminfo`, `/proc/uptime` and
`/proc/net/dev` under the metric names expected by the stock Majestic WebUI.
There is deliberately no JavaScript/frontend rewrite and no invented
temperature metric.

## Explicit media runners

- `majestic-fh8626-media-run`: strict compatibility mode.
- `majestic-fh8626-stub-run`: permissive discovery mode.
- `majestic-fh8626-native-video-run`: fixed native FH8626 H.264 bring-up.

The runners temporarily bind-mount the dedicated media YAML over
`/etc/majestic.yaml`; the default media-off configuration remains unchanged.

## Source compatibility boundary

### Sensor

FH8626 GC1054 exposes a recovered 0x68-byte callback table. FH8852 sensor
plug-ins expose a differently ordered 0x7c-byte table. The source GC1054 facade
presents the FH8852 shape and translates only recovered native callbacks.

### VMM

FH8852 VMM allocation uses a 0x50-byte ioctl record; FH8626 uses the recovered
0x68-byte record. The source `libvmm.so` facade prevents the donor allocation
wire from reaching the FH8626 driver.

### SYS / VPSS

The source `libdsp.so` facade translates the recovered FH8852 VPSS subset onto
FH8626 VPU operations: memory queries/init, VI attributes, channel memory,
geometry/open, enable/disable and media binding.

### VENC / stream

Native-video mode is intentionally constrained to the accepted FH8626 contract:

- 1280x720 at 25 fps;
- H.264 Baseline;
- recovered PAE system/channel allocation and configuration;
- recovered 0x54-byte VBR RC wire;
- explicit producer MMIO gate;
- recovered force-IDR;
- native 0x170 stream dequeue translated into the FH8852 public stream object;
- ring wrap represented as two public packs;
- exactly-one descriptor lease per acquire/release;
- bounded userspace emulation for FH8852 blocking stream acquisition.

Compile-time assertions pin all native VPU/PAE wire sizes used by the facade.

## Offline work intentionally not fabricated

The following still require real runtime evidence or an official Majestic
platform contract:

- arbitrary FH8852 VENC attribute/RC structures beyond fixed 720p bring-up;
- full replacement of donor ISP/advapi libraries;
- Majestic-facing JPEG/snapshot API;
- Majestic microphone/speaker/two-way-audio ownership/API;
- final Majestic day/night policy mapping to AJL33PQ0866 illumination;
- full same-boot destruction/recreation of every opaque ISP object.

Native FH8626 JPEG, RTX audio, image-control, illumination and PTZ primitives
already exist elsewhere in the project. They are not wrapped in guessed
Majestic ABIs just to claim completion.

## Remaining gate

The next meaningful work is build/runtime/hardware validation:

1. build the composed Builder Majestic target;
2. boot and verify default HTTP/WebUI plus the unchanged stock frontend;
3. confirm `/metrics` contains live CPU/RAM/uptime/network data;
4. run `majestic-fh8626-abi-probe`;
5. run strict media mode and preserve the first failing call;
6. use permissive mode only to reveal subsequent optional call ordering;
7. run native-video mode and prove VI -> VENC -> stream leases -> sustained RTSP;
8. only after H.264 stability, close the observed ISP/JPEG/audio call surfaces;
9. regress shutdown/restart plus PTZ, lens, illumination and storage.

A successful build or one frame is not production acceptance.
