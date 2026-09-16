# ANJIA AJL33PQ0866 board support

This package owns the ANJIA AJL33PQ0866 board profile, motor wiring and its stock PTZ
policy. It is selected exclusively by the matching builder device defconfig.

`overlay/etc/divinus.yaml` is the sole board-owned Divinus profile. The generic
Divinus package keeps its upstream defaults; this package supplies only the
FH8626 transport/hardware contract and board policy, including ONVIF
enablement. The late-overlay hook applies the profile after all package target
directories have been merged, so generic package installation cannot override
it.

`fh8626-ptz` controls the stock PWM groups through `/dev/fh_pwm`. It retains
the exact long-move schedule (`4t,4t,2t,2t,t...,2t,2t,2t,4t,4t`), reads the
period/range tuples and `pan_invert`/`tilt_invert` from U-Boot, and accepts
`pwm_invert` only as a compatibility fallback. State defaults to
`/etc/openipc/ptz.state`; `PTZ_STATE_FILE` may select a verified persistent
`rootfs_data` mount for a pure initramfs boot.

`S69fh8626-ptz` recalibrates both axes in the background on every boot and
returns the camera to its last valid saved coordinates without blocking the
rest of system startup. If no valid state exists, it returns to the third
coordinate in each `*_range` tuple. Normal boot follows the stock unbound
policy: both axes run the raw `+range`, wait, `-range` sequence. The endpoint
reached by the final raw pass is the numeric upper endpoint for both axes.
Absolute return uses `current - target`. `pan_invert` and `tilt_invert` swap
phase-channel slots 1 and 3 as in the stock axis constructors; they never
negate the raw initialization step count. On this board `ptz_coop=0`, so stock
assigns both jobs to
one shared worker queue and the pan/tilt hardware transactions run serially.
`startup` and `calibrate` both use this type-2 policy.
Stock sets `ptz_initmove_speed=100`; initialization therefore uses the first
(minimum-period) value from each `*_period` tuple, while ordinary and restore
moves use the third, speed-50 value. Set
`PTZ_INITMOVE=0` in
`/etc/default/fh8626-ptz` only to suppress startup movement during bench work.
Old coordinates are only a return target while initialization is busy. Before
any motor operation, state is atomically marked uncalibrated; the exact final
coordinates are written only after the full operation succeeds. A reset during
movement therefore forces calibration on the next boot. Startup still
recalibrates because the mechanism has no absolute encoder.

Every motor cycle is one stock transaction: four SET ioctls with `pulses=1`,
one group ENABLE, and one WAIT before the next cycle. The first channel is the
`finish_all` source. The complete transaction and its wait finish before the
other axis may replace the global PWM enable mask. One process owns
`/dev/fh_pwm`; external commands receive busy during startup.
The final cycle clears wrap-stop.

Run `make -C src clean test` before target work to execute the ioctl recorder
tests.

Commands:

```text
fh8626-ptz status
fh8626-ptz raw pan|tilt STEPS
fh8626-ptz move PAN_STEPS TILT_STEPS
fh8626-ptz goto PAN TILT
fh8626-ptz home
fh8626-ptz startup
fh8626-ptz calibrate
```

`/usr/bin/gpio-motors PAN TILT DELAY` is the OpenIPC compatibility entry point.
The generic GPIO delay is ignored: this board uses the stock per-axis hardware
PWM timing from U-Boot rather than userspace GPIO delays.

## Hardware acceptance

The stock mapping in this package assumes the two motor connectors are in their
correct physical sockets: logical pan uses PWM11/10/9/6 and logical tilt uses
PWM5/4/3/7. A swapped connector pair was found during bring-up; it made the
1028-cycle pan sweep drive the short vertical mechanism into its stop. After
correcting the connectors, the full type-2 sequence was accepted on hardware on
2026-09-03: left/right pan sweep, up/down tilt sweep, then a normal return to
`pan=514`, `tilt=200`. All PWM outputs were disabled at completion and the
camera, SSH, media owner and watchdog owner remained operational.

The earlier 2026-09-03 interpretation `pan=1028, tilt=0` after type 2 was
superseded by exact decompilation of the stock worker. Stock records
`pan=1028, tilt=250`, then returns with `current - target`; return to tilt home
200 is therefore 50 cycles. The previous binary hash documents historical
testing only and is not the acceptance hash for this correction.

The production controller raises only active motor operations to `SCHED_FIFO`
priority 20. It continues to sleep in every PWM completion wait, while prompt
wakeup prevents unrelated MMC/network/media activity from introducing long
gaps between the one-pulse cycles and making calibration visibly jerk.

## Deferred diagonal motion

Calibration and coordinated moves currently run pan and tilt serially to match
the accepted stock `ptz_coop=0` behavior. A future optimization may drive both
axes diagonally, reducing duration from the sum of both axis times to roughly
the longer axis time. It must remain a single `/dev/fh_pwm` owner: configure the
active four or eight channels, issue one combined `ENABLE_MUL_PWM`, and wait on
one `finish_all` source chosen from the slower pulse. Distribute unequal axis
step counts with a deterministic DDA/Bresenham schedule and stop each axis as
soon as its own range is complete.

Do not implement this as two ioctl threads. Concurrent workers previously
exposed races in the global PWM enable mask and completion handling. Diagonal
homing must also avoid holding both mechanisms against their stops at once,
because that increases peak current and mechanical load. Keep the current
serial path available as the diagnostic fallback until the combined-cycle
implementation passes recorder tests and physical calibration, direction,
range, cancellation, reboot and safe-stop validation.
