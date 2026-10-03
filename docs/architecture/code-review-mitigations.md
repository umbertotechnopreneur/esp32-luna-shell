# Code review mitigations: first implementation checkpoint

Date: 2026-10-02. Source: **Cody Code Review 2026-10-02**, stored in the Cody Companion Obsidian project folder.

Shared firmware changes originate in `E:/ESP32 Luna Shell` and are also integrated in `E:/ESP32 Cody Companion`. Existing branding and concurrent Cody response-engine work are preserved. This checkpoint has static source review only. No configure, firmware build, size report, test, simulator, validation scan or hardware operation was executed.

## Coverage

| Review | Implemented in this checkpoint | Remaining evidence or next stage |
| --- | --- | --- |
| R1 | Shared standalone frame begin/check/end contract; Cody selects a 500 ms failure budget spanning geometry, raster and transfers. Failed frames never publish a completed content revision. | Real duration distributions, deliberate slow transfers, concurrent rotation/standby. Budget tuning requires measurements. |
| R2 | One transient internal DMA lease per frame, reused across rectangles/strips and released at frame end. Existing smaller-buffer/emergency-row behavior stays within that owner. | Radio headroom, allocator counts and fragmentation over long runs. Existing Brookesia/LVGL flush ownership is unchanged. |
| R3 | Cody caches evaluated path bounds, rejects invalid evaluated geometry and skips paths outside each strip. Navigation fill visits only its occupied rows. | Native culling/partition parity; segment/debug culling and avoiding repeat validation for intersecting paths are later stages. |
| R4 | One display watchdog session and one successful display publication per standalone frame; failed strips remain visible immediately. Snapshot carries real lock wait, frame duration, rectangle attempts, counts and retention errors. | Device measurements and consumers of the new structured metrics. No CPU estimate or promised FPS is introduced. |
| R5 | Separate companion state/resources, named touch actions, scoped frame/lock ownership, cleanup naming and copied timing-log state. Timing logs run after the worker releases its frame/state lock. | Failure, boot, touch and standby/wake regressions; native stack measurements. |
| R6 | Snapshot reporters moved to `LUNASHELL_snapshot_reports.cpp`; DMA lease allocation/release moved to `bsp_display_dma.c`. | Snapshot store/staging and acquisition remain with the original hardware owner. Sensor/processor acquisition and other BSP owners require separate stages. |
| R7 | Log and pairing presenters moved to dedicated files; log severity uses a typed identity. | Hardware, audio, Leg, settings and remaining screen families. This is organization work for Luna, not a Cody RAM saving. |
| R8 | Preview compares presentation state after animation/response composition, reuses the Tk image and named label items, skips unchanged presentation, and polls static boot screens at 100 ms. | Interactive Tk checks, allocation/CPU measurements and remaining readout work. Rig model timing still runs while transitions/stimuli can change. |
| R9 | Touched paths use separate steps, parameter comments and ownership/decision comments. | Continue locally when touching remaining dense functions; no repository-wide formatting rewrite. |
| R10 | Prepared `device.ps1 check -Check cody-native`: actual C++ rig/raster compilation, native alpha/stride/rejection/culling/partition checks and named ready-geometry comparison with Python. | Run explicitly; host compiler must already exist. Full pixel equivalence with Pillow and broader contour/response parity remain open. |

## Standalone frame ownership

`LUNASHELL_display_frame_begin(budget_ms)` acquires the shared recursive BSP lock, starts the display watchdog, and opens a BSP frame with one DMA lease. The calling task owns that frame until `LUNASHELL_display_frame_end(original_error)`. Nested frames are rejected. Rotation and power transitions cannot change panel/touch mapping midway through a frame. Existing unframed rectangle writes retain their synchronous ownership contract.

The deadline starts at BSP frame acquisition after the initial lock wait; lock wait is recorded separately. Cody's 500 ms value is a failure policy below the 1,000 ms control-lock timeout, not a render target. The 33 ms rig scheduling target remains separate. Deadline checks are cooperative between stages and before submissions; completion waits use the remaining aggregate budget, without rounding the wait upwards. An SDK call that never returns is covered by the existing watchdog, not forcibly cancelled by the software deadline.

At frame end the platform retires the watchdog while still owning the outer lock, closes the BSP frame, publishes its copied result and finally releases its lock. `last_duration_us` measures the BSP frame through render/transfer and watchdog retirement; it does not claim to include the subsequent snapshot publication or log output. The successful-frame counter and content revision advance only after successful completion. Companion scheduling state is recorded under its retained state lock; timing logging uses bounded copied state after that level is released.

Only standalone renderers that adopt this API get frame-level reuse and watchdog/publication consolidation. The Brookesia/LVGL path keeps its existing bounded flush leases; the extracted DMA module supports both ownership boundaries without introducing GUI dependencies into standalone profiles.

## Memory retention is explicit

A timed-out or failed SDK submission does not prove that DMA has released the source. The BSP quarantines that lease until explicit reset and rejects later writes. The frame snapshot records `retained_dma_bytes` and `reset_required`. This intentionally retained allocation is never reported as freed.

If companion cleanup cannot acquire the required lock, the resource owner retains the exact allocated strip/scene byte count and publishes `retained_consumer_bytes` plus `retained_consumer_error`. A later successful frame cannot erase an unresolved consumer cleanup failure. These fields describe ownership after failure; they are not evidence of a progressive leak.

The emergency row stays small and module-owned. The normal multistrip DMA allocation is transient, so idle/parked rendering does not keep an additional internal-RAM reservation competing with radio startup. Path bounds enlarge the caller-owned PSRAM scene slightly; actual memory and stack impact remain unmeasured.

## Source checks and validation boundary

`source_contracts.py` lists the exact implementation parts associated with the principal app/hardware/BSP sources. Source-contract checks now read those parts together instead of assuming each reporter or presenter remains in the original monolith. Missing modules still raise the original filesystem error. These checks remain source contracts, not proof of native execution.

For Cody, the native check compiles only portable rig/raster sources with host-only error/log definitions. It does not emulate a driver or import LVGL. Compiler output, executable and the final status report stay under `artifacts/checks/cody-native/`. The runner never installs a compiler and records failure before propagating it. It has been prepared but not executed.

Validation is tracked in `TODO.md`. The native runner does not establish DMA callback correctness, scheduler latency, real sensor behavior, radio headroom or hardware performance. Physical workflows require separate explicit authorization and always use `scripts/device.ps1`.
