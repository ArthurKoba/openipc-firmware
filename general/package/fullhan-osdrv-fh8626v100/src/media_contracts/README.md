# FH8626 media contract layer

This directory is the canonical imported implementation of the reverse
continuation C-I01…C-I16 host-side media contracts.

Run the complete host verification from `tests/`:

```sh
./run_host_gates.sh
```

The gate strictly compiles 22 implementation units, runs 23 normal suites,
repeats the 23 suites with ASan+UBSan, executes lifecycle passes A/B and checks
that generated test binaries are removed. A passing host gate does not claim
Divinus production wiring or camera hardware acceptance.

The imported subdirectories retain the continuation's functional boundaries:
`bgm`, `h264`, `nr3d`, `jpeg`, `mux`, `audio`, `integration`, `transport`,
`timing`, and `observability`.

2026-09-10: Makefile.camera now links the NR3D kernel adapter into owner.
OFF uses the correct proc token and ioctl readback. ON remains explicitly
unavailable pending cold/restart lifecycle integration. Hardware acceptance
is pending; source import is not full production wiring. Current report:
`../../../agents/sessions/FH8626-A08-S01-20260830/ISP_TARGETED_INTEGRATION_20260910.md`.
