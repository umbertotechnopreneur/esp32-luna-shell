# Firmware Debug and Release modes

Build mode is independent of the hardware/product profile. Every firmware CLI
invocation defaults to `Debug`. `Release` requires the literal selection
`-BuildMode Release`; it is never inferred from an existing binary, build
directory, environment variable, board YAML, or earlier CLI invocation.
The interactive menu starts in Debug unless its caller explicitly passes
Release. An explicit selection under Session settings remains active only in
that menu session, and the menu displays and forwards the selected mode.

## Commands and configuration

Use the public `scripts/device.ps1` entrypoint for every firmware operation.
Initialize each profile/mode configuration explicitly before its first build:

```powershell
.\scripts\device.ps1 set-target -Profile cody-companion
.\scripts\device.ps1 build -Profile cody-companion -Jobs 8

.\scripts\device.ps1 set-target -Profile cody-companion -BuildMode Release
.\scripts\device.ps1 build -Profile cody-companion -BuildMode Release -Jobs 8
```

These are usage examples, not validation performed for this checkpoint. Building
does not flash, erase, publish, install, reset, or open a serial port. Those
actions retain their separate explicit authorization requirements.

The versioned catalog declares mode defaults, applied after profile and board
layers. Generated sdkconfigs and default build directories receive `-debug`
or `-release` suffixes, for example:

| Mode | Generated configuration | Build directory |
| --- | --- | --- |
| Debug | `artifacts/sdkconfig/v1/cody-companion-debug` | `artifacts/build-cody-companion-debug` |
| Release | `artifacts/sdkconfig/v1/cody-companion-release` | `artifacts/build-cody-companion-release` |

The hardware component graph and canonical dependency lock are shared between
modes of the same profile. The working lock, binaries, staged UI assets, CMake
state and generated configuration stay separate. Old unsuffixed artifacts are
neither migrated nor selected automatically. Use a dedicated explicit
`-BuildDir` for each mode when overriding the default.

The defaults fingerprint includes the mode and its defaults file. Before build,
flash or size, the engine checks the generated mode, logging levels, sleep
instrumentation and disabled USB SOF hook. A missing/mismatched configuration
fails with a `set-target` command preserving the requested mode. CMake also
rejects a command/configuration mode mismatch; removing `-BuildMode Release`
cannot accidentally reuse a Release build.

## What selecting Release changes

| Area | Debug | Release |
| --- | --- | --- |
| Application logging | INFO default, compiled maximum VERBOSE | WARN default and compiled maximum WARN |
| Bootloader logging | INFO | WARN |
| Boot identity | Debug notice | One Release notice warning about the USB sleep limitation |
| Automatic Light-sleep | Disabled for USB debugging | Enabled when power management is present and workloads permit it |
| Inactivity sleep | CPU/USB remain awake in software standby | Deep-sleep after confirmed standby; touch/IMU wake restarts firmware |
| Screen inactivity and fade | Runtime `idle_sleep_s`, default 60 s; 800-ms fade | Same timeout and fades |
| Standby motion observation | 10-Hz host snapshots | Accelerometer-only hardware WoM; no host polling during Deep-sleep |
| CPU frequency | Existing 80--240 MHz dynamic policy | Same 80--240 MHz policy |
| Sleep residency diagnostics | Callback counters available | Debug-only callback instrumentation disabled |
| Repeated post-wake trace reports | Available | Disabled; retained failure trace remains available |
| Cody frame diagnostics | Authored execution timings, frame distributions and FPS/CPU HUD | No collection/reporting of diagnostic frame distributions, raster stage profiling or HUD |
| Cody PWR screenshot | One click waits 500 ms, then starts the existing countdown | Admission disabled; no shutter worker |
| Button software reset | PWR double click within 500 ms or 3-second PWR/BOOT hold | Excluded; ordinary standby/fullscreen controls remain |
| Cody static/parked worker | Existing 50 ms/200 ms waits; paused HUD refresh | 1000 ms functional gate checks; touch IRQ wakes immediately; unchanged paused frames do not redraw |
| Active animation | Existing selected 40 ms or 33 ms target | Same target |
| Compiler optimization | Existing performance optimization (`-O2`) | Same optimization for comparable rendering behavior |

Release keeps real errors, canonical snapshot health/validity/timestamps,
frame ownership/deadline checks, watchdogs, persistence semantics, requested
active sensor cadences and structured input errors. Driver-owned
frame safety and published measurements are still present. User-requested
diagnostic services are functional features and are not silently disabled.
Performance Mode restores the compiled log default when it ends, so it cannot
turn Release INFO logging back on.

Release enters Deep-sleep after confirmed standby on touch-enabled profiles;
touch GPIO11 and accelerometer-only IMU INT2/GPIO21 provide physical wake.
The normal inactivity timer adds no periodic wake and PWR polling stops.
Debug keeps CPU/USB awake and retains 10-Hz software motion observation.
See [Release sleep lifecycle](release-deep-sleep.md) for runtime settings,
IRQ/revision checks, rollback, startup and shared-rail limits. Whole-board
current and battery life remain unmeasured. Native USB Serial/JTAG disconnects
during Deep-sleep. `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` stays disabled in both modes:
the known ESP-IDF 6.0.2 SOF-hook cache-error problem has not been repaired by
this change. Wake/reconnect may be necessary before a later explicitly
authorized serial/flash operation.

## Deferred validation

This is an implementation and static-review checkpoint. No configure/build,
size, host check, simulator, flash, monitor or current measurement was run.
The prepared `device.ps1 check -Check build-modes` exercises the actual selector
and generated-configuration rejection gates without hardware. Existing console,
profile and power checks, clean/incremental Debug and Release builds, size
reports and simulator checks need fresh explicit authorization. Physical
PWR/motion/touch wake, USB reconnect, radio coexistence and active/standby current
need separate hardware authorization and measurements on the resulting image.
