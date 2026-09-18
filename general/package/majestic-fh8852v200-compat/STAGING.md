# FH8626V100 Majestic compatibility package

Status: SOFTWARE_MEDIA_STAGING / BUILD_AND_HARDWARE_PENDING

Majestic HTTP/WebUI is outside this compatibility layer. Majestic keeps its
normal HTTP port and serves its own API, WebSocket and frontend directly.
No proxy and no JavaScript patch is part of this direction.

The historical metrics symptom is left for the real Majestic/platform metrics
provider boundary. It must not be hidden by an auxiliary web server.

Implemented source compatibility layers:
- FH8852 0x7c sensor facade -> recovered FH8626 GC1054 0x68 callbacks;
- FH8852 VMM allocation boundary -> native FH8626 VMM 0x68 wire;
- FH8852 SYS/VPSS subset -> native FH8626 VPU/media contracts;
- fixed native 1280x720@25 H.264 VENC bring-up with recovered PAE/VBR/IDR;
- native FH8626 stream descriptor -> FH8852 public packs with lease discipline;
- recovered FH8852 audio MPI subset -> FH8626 RTX transport.

Manual modes remain strict, permissive discovery, and native-video. The default
boot remains media-off.

Still requires runtime evidence: arbitrary FH8852 VENC/RC records, complete ISP
replacement, Majestic JPEG/snapshot surface, full audio policy/two-way behavior,
day/night integration and same-boot opaque ISP teardown.

Next gate is build and real target validation. No further web/control-plane
replacement should be introduced in this package.
