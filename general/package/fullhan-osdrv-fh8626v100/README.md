# FH8626V100 media package

This package contains the generic FH8626V100 media-module loader, ARC
firmware, the GC1054 plug-in used by the first supported board, and the
OpenIPC media-owner utility built from source with the target toolchain.

The local-package build always relinks its userspace executables instead of
accepting generated ELF files left in `src/`.  The package build also rejects
`fh8626-media-owner` unless its program interpreter is the target musl loader;
this is required because the owner loads the MIPI and sensor plug-ins through
`dlopen()`.

The Fullhan kernel modules and sensor libraries are proprietary stock ABI
artifacts. They remain process-isolated behind `fh8626-media-owner`; the main
OpenIPC userspace remains musl-based.

No board GPIO is changed by `fh-media-modules`. Device-specific reset or power
sequencing belongs in the corresponding builder device overlay.

The package also installs `fh8626-audio`, the supported CLI for the recovered
RTX/ARC audio service. Generic format defaults live in
`/etc/default/fh8626-audio`; external amplifier mute GPIOs and analog-profile
assumptions must be overridden by a board overlay. The compiled
`fh8626-audio-rtx` helper is an implementation detail under `/usr/libexec`.

Motor wiring and PTZ calibration are board policy. The AJL33PQ0866 Builder
profile injects its separate `anjia-ajl33pq0866-board-support` package; generic
FH8626V100 firmware targets neither contain nor select it.
