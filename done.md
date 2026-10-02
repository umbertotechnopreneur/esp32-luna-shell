# Done

## Cody Response Engine implementation (2026-10-02)

- [x] Add typed external-stimulus inputs, a coherent read-only view of the single
  canonical Luna snapshot, timestamp-gated gravity-baseline/transient processing,
  analytic three-axis springs, continuous stress/fatigue and one rig composer.
  Preserve input health/errors, phase continuity, cardinal rotation and fixed
  navigation. Missing charge remains unknown; charging alone does not reset it.
- [x] Author strict named YAML response parameters and share native/host limits
  through generated firmware data. Add the Windows Stimuli tab with synthetic
  inputs, pulse/shake, source-health controls, model readouts and root overlays.
- [x] Prepare focused response regressions and an English architecture blueprint.
  Source tables were regenerated; runtime checks, simulator execution, firmware
  builds and hardware validation have not been run for this checkpoint.

- Consolidated the Cody tooling behind `scripts/device.ps1`: local shared menu,
  host/firmware routing, grouped generators/checks/contracts, three boot captures,
  failure logs and artifact containment. Removed the duplicate simulator menu and
  serial-capture alias. Transient artifacts are cleaned before build/flash while
  firmware/config/Python caches survive; compiler jobs are bounded to eight for
  both build and flash targets. Console regressions were prepared but not run;
  simulator, packaging, firmware and hardware validation remain deferred.
  Executed only the requested `clean-artifacts` operation: previous PoC runtime
  output was removed and the cleanup log saved under `artifacts/logs/`.
  Removed the obsolete `scripts/__pycache__` output as well.

## Cody responsive inspector and facial controls (2026-10-02)

- [x] Fit the desktop preview to its viewport with one shared image/label/touch
  transform. Reflow the controls below the preview at narrow widths, keep the
  panel scrollable, and adapt label pagination without shrinking the font.
  Factor repeated sliders and toggles into commented helpers.
- [x] Add anchored mouth width, nose size, cheek size and cheek opacity controls.
  Enlarge default cheeks slightly, interpolate face targets, and expose an
  Embarrassed example while preserving independent breath and contour motion.
  Blend partial cheek opacity in RGB565 using the fixed source palette.
- [x] Update prepared regressions and documentation. Asset conversion and static
  review only; no test, simulator, firmware build or hardware run in this step.

## Cody contour and named debug overlays (2026-10-02)

- [x] Add YAML-authored fullness, wave amplitude, cycles-per-second speed and
  phase spread for the eleven cloud lobes, separate from the facial channels.
  Smooth targets and integrate speed without restarting the wave phase;
  preserve the original shape when both displacement controls are zero.
- [x] Extend the Windows inspector with Face/Contour/Debug tabs, independent
  contour freeze, current/target readouts, current and reference outlines,
  selectable stable names and leader lines, filtered/paged point groups and
  normalized/logical/rotated coordinates. Preserve cardinal touch transforms
  and icon-only lateral navigation.
- [x] Prepare regression coverage for contour independence, phase continuity,
  timing, invalid inputs, reference geometry and rotated name attachment.
  Source conversion and static review only: the expanded suite and simulator
  have not been run. Firmware build and hardware performance remain unverified.

## Cody cloud-rig PoC (2026-10-02)

- [x] Add a YAML-owned front-facing cloud with 145 stable named points,
  normalized feature anchors, 44 cubic segments and shared evaluated geometry
  for artwork, debug links, control handles and touch selection.
- [x] Integrate the exact 256-slot palette for the rig, named paint/debug roles,
  independent eye/brow/mouth controls and elapsed-time breathing/blinking.
  Keep original boot images and real startup boundaries. Add the animated rig
  route, lateral Back/debug icons and actual device timing instrumentation.
- [x] Add `-Cody` to the existing Windows simulator launcher, with independent
  controls, scaling, placement, all cardinal orientations and three boot views.
  Disable HTTP diagnostics for this local PoC, keeping Wi-Fi/BLE hardware.
- [x] Run ten focused host checks, including actual Tk control navigation,
  and the PowerShell boot-preview capture path. Review generated screen PNGs;
  record a 600-frame host render sample and looping animation under
  `artifacts/cody-poc/`. Firmware compilation, physical performance and native
  raster parity remain explicitly open in `TODO.md`.

## Cody base implementation (2026-10-02)

- [x] Record the source-verified 466 x 466 circle, 320 x 320 central safe
  square at (73, 73), variable rounded margins, and lateral Back slot in
  `AGENTS.md` and the architecture documentation. This is a documentation
  checkpoint from static source review, not OOBE implementation or validation.
- [x] Specify an icon-only left-chevron Back control, preserving its lateral
  position, gradient, and full touch target; update the geometry diagram.

- [x] Carry the reusable main display/touch checkpoint into a remote-main-based
  Cody branch. Add the standalone connected profile, local minimal CODY2D
  surface/fill/RLE code, boot consumer and three blue Imagegen phases in flash:
  sleeping at start, waking after local services, ready after startup completion.
  Preserve mandatory hardware, real errors and cardinal orientation. This is
  implementation plus static review and asset conversion only; firmware and
  device validation remain open in `TODO.md`.

## Standalone display and touch implementation (2026-10-02)

- [x] Separate physical display/touch selection from the Luna GUI. Add a
  mandatory display-touch profile, standalone RGB565/touch APIs and a
  hardware-owned display lock, preserving the existing UI path. This is a
  source checkpoint with static review only; dependency locks, compilation and
  physical behavior remain pending in `TODO.md`.

## Archived from the active backlog (2026-09-25)

- [x] Build the Windows simulator device twin: validated synthetic snapshots,
  deterministic scenarios and clock, semantic actions, loopback fake HTTP API,
  and hardware-free visual/interaction checks.
- [x] Implement BLE-only, user-started Wi-Fi provisioning with the System Core
  wizard, dynamic QR flow, and hardware-free checks.
- [x] Move hardware status, freshness, CPU utilization, and full-refresh
  transaction reporting onto `DeviceSnapshotService` with deterministic tests.
- [x] Use System Core as the sole app runtime, split the declarative GUI into a
  resident bootstrap and lazy route documents, and stage only product assets.
- [x] Separate internal LittleFS from removable SD and add guarded,
  CID-bound, one-shot format authorization plus Settings status/progress.
- [x] Add backend sensor state and non-blocking stationary calibration;
  orientation remains unavailable without a validated reference frame.
- [x] Implement transactional Device Standby, dim/off display modes, qualified
  IMU wake, app/GUI quiescence, and event-driven idle workers.
- [x] Add opt-in HTTP API settings persistence, the Audio FFT scope route, and
  non-UI open-source branding checks and release checklist.

Completed items moved out of `TODO.md` live here.

## Build and resource efficiency

- [x] Add versioned `headless`, `ui-minimal`, `connected`, `full`, and
  `full-distance` compositions with isolated generated configurations,
  build-local dependency resolution, and one reviewed canonical lock per
  variant.
- [x] Make runtime-package verification, staging, JSON minification, and
  LittleFS image creation timestamp-driven; keep each profile's generated
  resource tree inside its own build directory.
- [x] Reuse pre-sized FFT/detail binding keys, paths, colors, strings, and
  update vectors for the app lifetime, and skip dispatch for unchanged frames.
- [x] Add CI resource budgets for image bytes, static DIRAM, and total/managed
  translation-unit counts, with deterministic policy self-tests and reports.

These entries record host-side implementation and validation. Device latency,
heap behavior, and visual equivalence remain explicit hardware work in
`TODO.md`.

## Runtime efficiency and transactional recovery

- [x] Coalesce repeated trusted-GNSS clock updates by explicit system-drift and
  maximum RTC-persistence intervals while preserving oscillator-stop recovery.
- [x] Park the empty SD logger indefinitely, drain writes in bounded watched
  batches, and refresh FAT free space only on mount, persisted writes, an
  explicit Storage request, or the slow safety deadline.
- [x] Compute microphone FFT bands only while an FFT/detail diagnostic lease is
  active, while preserving measured mean and peak telemetry for level-only
  consumers.
- [x] Reconcile Performance Mode, display rotation, and HTTP server lifecycle as
  explicit transactions whose failed cleanup remains visible and retryable.
- [x] Publish retained liveness breadcrumbs with a versioned, non-colliding
  sentinel and cover removable-storage, logger, and formatting I/O domains.
- [x] Preserve repeated Full Snapshot aggregate errors in return values and
  structured state while coalescing only their duplicate scheduler log into
  immediate transitions, counted 60-second reminders, and immediate recovery.
- [x] Replace Brookesia's per-call `changed_updates` pointer-vector allocation
  with a compact inline bitmap that covers up to 128 updates without heap
  growth, while preserving the two-phase store/apply order, restoring listener
  suppression with a guard, and retaining host coverage for unchanged values,
  missing targets, binding errors, and dirty-node reapplication.

These entries record the implemented software contracts. Interactive UI,
rotation, removable-media, and forced-failure scenarios remain physical-device
tests where tracked in `TODO.md`.

## Audio scope visualization

- [x] Improve FFT sensitivity by mapping the measured DFT range from -90 to
  0 dBFS, so quiet valid ES7210 captures are visible without synthetic gain.

## UI rotation and Performance Mode

- [x] Cache the last complete RGB565 frame in PSRAM and replay it after a
  cardinal panel/touch rotation instead of invalidating and rasterizing the
  entire LVGL tree.
- [x] Add the Settings Command Arc Performance Mode action. It pauses effective
  SD log writes, periodic Full Snapshots, and the optional voice AFE while
  preserving saved preferences and keeping connectivity and sensor services
  available.
- [x] Enable bounded LVGL shadow and circle caches for repeated chrome drawing.
- [x] Replace the visible 10-row scrolling tiles with 64-row double buffers in
  PSRAM, copied through one transient 4-row internal DMA buffer before QSPI.
- [x] Retire the icon-only Maximum Debug preset after device monitoring showed
  that its 100 ms diagnostics, 300 ms Full Snapshots, global verbose logging,
  and unavailable SD mirroring could starve the UI. Keep each diagnostic
  setting explicit and independently selectable instead.

## Internal memory and diagnostic sampling

- [x] Bound both the canonical logger ring and its snapshot scratch to 15
  complete lines, and move the permanent logger, SD storage, and diagnostic
  task stacks to PSRAM while retaining their FreeRTOS TCBs in internal RAM.
- [x] Publish raw internal/PSRAM heap block metrics and allocation-failure hook
  counters so fragmentation and failed requests remain explicit.
- [x] Reuse each fresh Diagnostic IMU or microphone sample at most once in the
  next Full Snapshot without changing its measurement timestamp, and gate the
  two diagnostic streams independently.

Device behavior for these changes has not yet been verified; completion here
records the implementation only.

## Low-power runtime acquisition

- [x] Make the shared hardware monitor block indefinitely when no diagnostic,
  adaptive-motion, or auto-rotation consumer is active, and wake it from every
  setter that can make sampling eligible.
- [x] Enable and read back AXP2101 battery detection, sample cell telemetry only
  when a battery is present, and publish explicit percentage/voltage validity.
- [x] Latch a periodic PCF85063 oscillator-stop failure until a trusted RTC
  write succeeds and is verified by hardware readback.
- [x] Cache immutable flash, running-image, partition, and core-count metadata
  after the first complete successful read while retaining dynamic runtime
  sampling.
- [x] Move successful periodic PMU, RTC, and GNSS detail logs to `DEBUG` while
  preserving visible errors and structured health state.

These entries record the implemented software package only. Electrical savings
and the open physical standby/wake tests require target-device measurements and
remain tracked in `TODO.md` and `planned-todo.md`.

## Runtime liveness and bounded operations

- [x] Enable panic-and-reboot bootloader, interrupt, idle-core, and Task-WDT
  policy, with a 12-second Task-WDT deadline and a best-effort retained timeout
  breadcrumb exposed through the read-only health response.
- [x] Add explicit liveness domains for startup, GUI, LVGL display, Full
  Snapshot, settings NVS, provisioning, boot audio, radios, Wi-Fi operations,
  asynchronous Wi-Fi scans, and Wi-Fi events.
- [x] Replace indefinite application-owned mutex waits with bounded failures,
  serialize Wi-Fi lifecycle operations, and keep the reconnect timer callback
  non-blocking when that lifecycle is busy.
- [x] Put potentially unbounded NVS, provisioning teardown, and boot-audio work
  behind owned worker lifetimes and software deadlines that remain inside the
  Task-WDT deadline.

This section records host-validated implementation only. Forced-stall behavior,
retained reset diagnostics, and worker stack margins remain hardware tests in
`TODO.md`.
