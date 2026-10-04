# Standalone display and touch runtime

## Profile and hardware ownership

The `display-touch` profile requires CO5300 AMOLED and CST9217 touch without
the Luna app, Brookesia GUI, LVGL or its adapter. Initialization failure stops
startup and remains visible in `DeviceSnapshotService`.

`LUNASHELL_BUILD_ENABLE_DISPLAY` and `LUNASHELL_BUILD_ENABLE_TOUCH` select
hardware; `LUNASHELL_BUILD_ENABLE_UI` selects the Luna GUI. Touch requires the
display and shares its logical orientation. Headless admits neither device.
Choose a UI-off catalog profile: roots are admitted before Kconfig, and an
inconsistent combination is rejected rather than retaining unused GUI roots.
The base profile selects no radios, sensors, audio, SD or GNSS; connected
consumers may admit those services and own their lifecycle without Luna Settings.

The BSP owns bounded RGB565 writes, actual touch sampling, the shared recursive
display lock and internal DMA leases. The consumer owns artwork, fonts, widgets,
animation and scene/source buffers. Input/transfer failures remain truthful.
The configure-time CST9217 patch preserves original I2C errors and invalidates
failed samples; patch drift stops configuration.

## Rotation and redraw

`LUNASHELL_display_set_rotation_degrees()` applies and persists exactly
0/90/180/270 while auto-rotation is disabled, including without IMU. Panel and
touch change atomically. Persistence failure rolls the panel back and publishes
the real error; invalid saved settings require explicit reset.

Rotation or wake changes the atomic content revision. The standalone BSP keeps
no framebuffer cache, so consumers must redraw the full frame. A frame's shared
lock prevents rotation/power from changing panel/touch mapping mid-render.

## Owned standalone frames

`LUNASHELL_display_frame_begin(budget_ms)` acquires the shared BSP lock,
starts the display watchdog and opens a task-owned frame with one DMA lease.
Nested frames are rejected. The same caller must close the frame with
`LUNASHELL_display_frame_end(original_error)`, retaining any geometry,
raster or transfer failure rather than publishing completed content.

The aggregate deadline starts at BSP frame acquisition after the initial lock
wait, which is measured separately. Cody's 500 ms failure budget is distinct
from its 33 ms scheduling target and the 1,000 ms control-lock timeout.
Checks are cooperative between stages and before submissions; completion waits
use the remaining budget without rounding upward. A wedged SDK call is covered
by the watchdog, not forcibly cancelled by the software deadline.

Frame end retires the watchdog while holding the outer lock, closes the BSP
frame, publishes its copied result and releases the lock. `last_duration_us`
includes render/transfer and watchdog retirement, not subsequent snapshot
publication/logging. One successful display publication/counter update occurs
per completed frame; partial strips never establish frame/content success.
Failures remain visible immediately.

Canonical frame metrics report real lock wait, duration, rectangle attempts,
counts and retained-memory errors; they are not CPU estimates or FPS promises.
Companion scheduling state is recorded under its state lock. Timing logs use
bounded copied state after releasing the frame and worker state locks.

Frame-level reuse/consolidation applies only to standalone consumers adopting
this API. Unframed rectangle writes retain synchronous ownership. Brookesia/
LVGL keeps its existing bounded flush leases; the shared DMA owner supports
both boundaries without adding GUI dependencies to standalone profiles.

## DMA and failure retention

PSRAM pixels are copied into short internal DMA strips. After a successful
synchronous write returns, its source buffer may be reused. Within an owned
frame, one transient lease is reused across rectangles/strips and released at
frame end/park.
Smaller-buffer and module-owned emergency-row behavior stay within that owner;
idle rendering must not retain an extra multistrip reservation competing with
radio startup.

Submission failure or timeout does not prove DMA has released its source.
Quarantine the uncertain lease until explicit reset and reject later writes.
Publish `retained_dma_bytes` and `reset_required`; intentionally retained
memory must never be reported freed.

If companion cleanup cannot acquire its required lock, retain the exact
allocated strip/scene bytes and publish `retained_consumer_bytes` plus
`retained_consumer_error`. A later successful frame cannot erase unresolved
cleanup failure. These fields describe retained ownership, not proof of a
progressive leak; actual memory/stack impact needs measurement.

## Validation boundary

Open release gates remain in [TODO](../../TODO.md). Implementation/source checks
do not prove native execution, DMA callbacks, scheduler latency, sensor behavior,
radio headroom, physical output or performance. Source-contract readers must
include every declared implementation module; a missing module remains a real
filesystem error, not an omitted check.

Use a reviewed profile-specific dependency lock; refresh affected locks when
BSP gates change. Never invent a lock or reuse a GUI lock for standalone.
An explicitly requested lock workflow uses `scripts/device.ps1 update-lock
-Profile display-touch`; missing locks are rejected by the wrapper. All output
belongs under `artifacts/`. Build/check and physical display/touch/rotation/
DMA/standby/radio workflows require their own authorization under
[AGENTS](../../AGENTS.md).
