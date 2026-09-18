# FH8626V100 proprietary media runtime package

This package is the build-time owner for the proprietary FH8626V100 kernel
media ABI that has not yet been replaced by open kernel drivers.

Binary payloads are intentionally **not** committed in the active Firmware or
Builder branches. Buildroot downloads the exact hardware-proven artifacts from
immutable Firmware archive commit:

`f4bf49da6ef355c9e733e00d774efe403513b1d4`

Every file has a pinned SHA-256 in
`fullhan-media-fh8626v100.hash`. Independent immutable copies are also kept
in Koba artifact storage for provenance.

Installed runtime:
- bgm.ko
- enc.ko
- gpio_wave.ko
- isp.ko
- jpeg.ko
- media_process.ko
- vmm.ko
- xbus_rpc.ko
- rtthread_arc.bin

The package does not install the archived vendor sensor plug-in or libmipi.
Current Majestic and Divinus paths own their sensor/userspace integration
separately.

`/usr/bin/load_fullhan -i` integrates with OpenIPC `S70vendor` and delegates
to `/usr/sbin/fh-media-modules`. The loader preserves the hardware-proven
module order and fails if the required media character devices are missing.

This remains a transitional proprietary runtime. Replacing these kernel modules
with open drivers is a separate retirement project; removing the package before
that work is complete makes the produced image unusable for media.
