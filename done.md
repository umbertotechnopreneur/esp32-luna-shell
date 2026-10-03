# Done

## Device-console menu and help presentation (2026-10-04)

- [x] Restore operation-specific emoji throughout the public device menu and
  submenus, with Exit on the main page and Back on nested pages. Organize help
  into colored categories/subsections with dividers, readable command examples,
  width-aware wrapping and descriptions for firmware/device commands, checks
  and generators. Use only `Device.ps1` in help examples, with an opening note
  for execution from `scripts`. Show every command/option in bold WhiteSmoke
  (`#F5F5F5`) with its extended description underneath; use the full terminal
  width with one blank line between entries instead of two columns.
  Place category/subsection titles beside a thin, width-aware rule with a
  three-point gradient fading toward its right edge.
  Review source only; no CLI preview, tests, build, simulator or physical-device
  operation ran.

## HTTP and Wi-Fi pairing integration (2026-10-04)

- [x] Integrate the shared Wi-Fi credential-event adoption, explicit
  telemetry/provisioning handoff, per-attempt proof of possession, session QR,
  asynchronous Start/Retry/Cancel controller and canonical lifecycle state into
  `main`. Enable opt-in HTTP diagnostics in Cody without automatic first-boot
  advertising, and document the OOBE contract. Source review only; validation,
  UI integration and device evidence remain open in `TODO.md`.

## Shared main integration (2026-10-04)

- [x] Merge Luna connectivity, hardware census, Debug/Release and Deep-sleep work
  with Cody's typed face lifecycle, branding and performance HUD on `main`.
  Preserve the existing worktree branches. Prepare standalone Wake frames with
  panel output enabled at zero brightness before exposing the 300-ms fade.
  Static source review only; build, tests and physical behavior remain unverified.

## Cody RTC activation and capability review (2026-10-04)

- [x] Enable the existing PCF85063 calendar/UTC/snapshot pipeline in the Cody
  profile without adding a sensor owner or task. Review the official schematic
  and NXP alarm/countdown specifications, and document product uses plus the
  expander interrupt's lack of a direct ESP Deep-sleep route. No alarm UI,
  scheduler or automatic wake is implemented; build, tests and hardware remain
  unverified. See `docs/architecture/cody-rtc.md`.

## Cody facial lifecycle and expression service implementation (2026-10-04)

- [x] Render Calm/Happy/Calm from the first animated boot frame on ordinary
  starts, preserve independent breath/blink phases, and use atlas 35A / Sleep,
  35B / Wake and 16 / Idle through explicit typed identities. Distinguish a
  real Deep-sleep reset from ordinary startup.
- [x] Add a single display-consumer power callback: acknowledge complete Sleep
  frames before suspension and prepare Wake while the panel is still dark.
  Preserve real transition errors and the existing resource rollback owner.
- [x] Embed a strict catalog of eight implemented combinations and three atlas
  lifecycle mappings, plus the authored 01/02/03 preview identities and real
  04 / Greeting scene, with a runtime named expression-selection API. Generate source
  assets through the sole dispatcher; no build, tests, checks, simulator or
  hardware operation was authorized for this implementation.
- [x] Keep the startup performance HUD hidden, show centered ASCII branding
  under the animated face, fade the brand before screen 04, and start measured
  FPS/CPU windows there in whitesmoke. Preserve the saved first-boot opt-out;
  the branding also retires when that greeting is disabled.

## Cody intro and tactile response implementation (2026-10-04)

- [x] Add reusable typed scene phases: 400 ms Ready bitmap fade-out, two-second
  animated Cody fade-in, then the existing greeting typewriter. Hide content/HUD
  and touch targets during the intro, preserve active-time pause/standby rules,
  and invalidate complete old/new artwork footprints when opacity changes.
  Reuse the palette and current raster tile, without another framebuffer.
- [x] Swap board X/Y in the authored panel mapping. Add canonical touch gesture
  publication and bounded elastic drag/pressure springs in the shared response
  composer; preserve eye-tap closure/annoyance and avoid double-rotating logical
  touch vectors. Mirror the implementation in the synthetic Windows preview.
- [x] Fix review P2: the runtime asset check now takes PCM/WAV sample rate from
  the explicit authored audio setting instead of assuming 16 kHz. No check or
  simulator execution was authorized for this iteration.
- [x] Deliver checkpoint `d922d75a` on explicit request: ESP-IDF 6.0.2 build
  and COM10 flash succeeded with at most eight jobs, a 2,081,600-byte app,
  verified written hash and hardware RTS reset. No tests, simulator or
  post-flash serial capture ran; physical behavior remains to be observed.

## Cody greeting diagnostics, audio and orientation implementation (2026-10-04)

- [x] Restore the measured FPS/CPU HUD on the greeting behind a default-on
  session flag with a locked runtime setter/getter. Include old HUD damage when
  hiding it. Reuse Luna's existing cardinal auto-rotation owner and API through
  a default-on Cody startup policy after strict saved-setting restoration.
  No new sensor reader, task, framebuffer or LVGL dependency is introduced.
- [x] Resolve the observed boot cue format mismatch in source: regenerate both
  original flash-owned PCM cues at the shared 22.05 kHz clock and request codec
  volume 100. Reject peer stream clock conflicts before changing transport.
  One authorized preceding-firmware 45-second capture showed continuous frames
  and the I2S mismatch, not the reported freeze. This records implementation;
  new device appearance, audible output and rotation are not yet observed.
  Checkpoint `6f42f84d` compiled successfully (2,074,320-byte application),
  with HUD/rotation enabled and LVGL excluded. The first flash was blocked by
  COM10 disconnection; the subsequent `b663f2ac` retry flashed successfully with
  a verified hash. The later live capture showed frames and rotation, no reset
  cycle in its 45-second window; long-run behavior remains unverified.

## Cody first-boot scene implementation (2026-10-03)

- [x] Implement the atlas page 05 / screen 04 greeting through a reusable
  standalone cloud/title/optional-content compositor. Keep the existing
  snapshot response and pearl face warp; move Cody ten pixels higher, use
  scale 130, and reveal `Hi, I'm Cody!` in a 70 ms-per-character typewriter.
  Schedule it two seconds after the Ready frame with a logged, strictly read
  NVS first-boot key and an explicit development override enabled by default.
  Pack printable ASCII coverage into 19,000 flash bytes without runtime font
  decoding, per-frame allocation or another framebuffer. Add the shared
  Windows preview route and document ownership/failure rules. Checkpoint
  `1df7ee0a` subsequently compiled and flashed on COM10 at explicit user
  request: 2,055,376-byte application, verified flash hash and hardware reset.
  No automated tests or simulator ran; runtime/size checks remain open.
## Release touch/motion sleep policy (2026-10-04)

- [x] Implement runtime `idle_sleep_s` (default 60 seconds), shared 300-ms fades,
  Release transactional Deep-sleep with touch GPIO11 and QMI8658 INT2/GPIO21
  accelerometer-only WoM, wake startup pin/IMU/display restoration and rejected
  entry recovery. Debug retains CPU/USB/logs and 10-Hz software motion observation.
  Update Luna Settings, canonical power snapshot and HTTP diagnostics. Source
  review and GUI document generation only; validation and device evidence remain
  open in `TODO.md`. No board-current or wake-latency claim.

## Hardware power census (2026-10-04)

- [x] Add the root hardware consumption census with manufacturer datasheet
  references, typical/maximum/peak distinctions, rail conditions and explicit
  unavailable currents. Extend the integration/JSON inventories with schematic
  flash/amplifier identities and touch, IMU, PMU/RTC interrupt routing. Document
  Deep-sleep/ULP candidates and current firmware limits. Source/PDF review only;
  no build, test, hardware or electrical validation performed.

## Common firmware build modes (2026-10-04)

- [x] Add profile-independent Debug/Release selection to the public firmware CLI
  and menu. Debug is the default; Release requires explicit selection. Isolate
  generated configuration/build output and reject mode/log/power mismatches.
  Apply Release warning/error logs, automatic Light-sleep eligibility, disabled
  debug-only residency/frame/HUD work and reduced Cody static/parked polling.
  Preserve errors, watchdogs and functional wake/acquisition contracts. Source
  implementation and static review only; validation remains in `TODO.md`.

## Cody rendering checkpoint (2026-10-03)

- [x] Port the approved concept-08 pearl texture to native scanline deformation,
  with premultiplied alpha filtering and old/new sprite damage. The first
  release was built and flashed on COM10; the user accepted its appearance.
  Add a named build-time renderer selection, currently enabled by default,
  and a session-scoped runtime face-follow flag, also initially enabled.
  Facial paths and debug/touch points receive the exact forward body warp.
  The final default/face revision was built and flashed on COM10 with both
  options enabled (2,027,936-byte application); no new tests were run.
  Document provenance, settings, measured older-firmware cost and the unresolved
  intermittent freeze separately. Performance optimization remains open.
- [x] Implement the reduced timing report and geometry-driven pre-raster damage
  candidate: compact clipped tiles, no live pixel-cache compare/pack, and
  reference geometry committed only after successful frame retirement.
  Compiled/flashed on COM10; artwork capture reaches about 29 FPS with CPU1
  45%, CPU2 0%, frame mean 26 ms and report cost 20 ms. Payload increases;
  DMA remains a bottleneck. Regression coverage is added but not run. The
  user observed no dirty pixels in the checked orientation.
- [x] Capture the real skeleton view and replace byte-at-a-time RGB565 panel
  conversion with aligned 32-bit pair swaps. On COM10, conversion falls from
  3 ms to 2 ms, CPU1 from 68% to 66%, and sustained FPS from 27.9 to 28.7;
  CPU2 remains 0%. Skeleton drawing stays at 15 ms and DMA wait at 11 ms.
  The regressing `memcpy` trial was discarded. No automated tests ran.
- [x] Prepare skeleton links and tangents once per frame and render them as
  validated ordered batches from a separate PSRAM cache. On COM10, raster
  falls from 15 ms to 14 ms, skeleton from 7 ms to 6 ms, CPU1 from 66% to
  64%, and sustained FPS from 28.7 to 28.9; payload and DMA are unchanged.
  The cache is opaque and committed only after complete preparation. No
  automated tests ran.
- [x] Prepare the exact marker disks in authored order in the same private
  cache. On COM10, marker time falls from 2 ms to 1 ms, skeleton from 6 ms to
  5 ms, raster from 14 ms to 13 ms and median CPU1 from 64% to 63%; CPU2 stays
  0% and sustained FPS reaches 29.2. No automated tests ran.
- [x] Shortlist only fill edges that can cross a compact tile and skip constant
  gradient math for solid paths without changing scanline coverage. On COM10,
  artwork CPU1 falls from 45% to 33%, artwork raster from about 7 ms to 4 ms
  and frame mean from about 26 ms to 21 ms at 29.4 FPS. The matched skeleton
  capture falls from CPU1 63% to 55% and draw from 13 ms to 10 ms. CPU2 remains
  0%; the user observed no dirty pixels in the checked orientation. No
  automated tests ran.
- [x] Add additive skeleton links/tangents/markers probes with two extra clock
  reads per drawn tile and no additional log line. Firmware `bc7c940b` compiled,
  flashed on COM10 and captured for 60 seconds. Skeleton costs about 8 ms;
  the sustained CPU/FPS target remains open. No automated tests ran.
- [x] Checkpoint shared bounded frame ownership separately from Cody rendering.
  Rasterize in the frame-owned DMA slot, compare conservative old/new coverage,
  retain exact pixel reference checks, and report individual layer durations.
- [x] Record narrow physical evidence: artwork about 27 FPS with CPU1 around
  80%; skeleton about 19-21 FPS. This is progress, not completion of the CPU
  performance target. The newest stroke/fill candidate needs separate results.

## Cody measured diagnostics and shared input integration (2026-10-03)

- [x] Add the white FPS/CPU1/CPU2 readout below the circular safe square,
  using completed-frame timing and a read-only canonical processor projection.
  Preserve unavailable/stale states instead of displaying numeric placeholders.
- [x] Acquire measured idle runtime on each owning CPU with a matching clock.
  The earlier flashed Cody capture retained valid CPU measurements through
  the capture window; port the focused correction to Luna separately.
- [x] Restore Luna's original CST9217 transport and connect its binary IRQ
  flow to standalone strip rendering. Preserve report order until the frame
  boundary, real failures and rotation/input ownership. Port shared changes
  to Luna without incorporating the parallel review's source edits.
- [x] Compile the current Cody candidate, including the temporary debug awake
  policy. This records implementation/compilation only: its flash and physical
  touch/animation/rotation checks remain open in `TODO.md`.

## Code-review mitigation implementation (2026-10-02)

- [x] Introduce frame-level standalone deadline/DMA/watchdog ownership and
  structured measured results with explicit failure retention. Extract snapshot
  reporters and the DMA lease owner, and separate Luna log/pairing presenters.
  Use typed severity identities and readable ownership/parameter comments.
  This records implementation only; validation is pending in `TODO.md`.
- [x] Integrate the shared Luna changes with Cody's current response engine;
  group companion state/resources, name touch actions, cull nonintersecting paths
  and reduce unchanged preview presentation/Tk allocation. Prepare a native C++
  raster/rig runner and Python geometry comparison without executing them.

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

## Cody offline PoC extension (2026-10-03)

- Added an authored automatic animation cycle, two original PCM16 boot cues,
  console-first boot/frame timings and optional existing SD logger mirroring.
- Extended the canonical snapshot adapter and response composer for microphone
  attention, confirmed playback, configurable idle yawn and real eye-tap
  closure/annoyance. Windows controls remain explicitly synthetic.
- Authorized host checks passed: 11 response tests, 23 PoC/Tk tests, portable
  raster/rig contracts and 500 native/host model updates. ESP-IDF compilation,
  physical audio/touch and measured firmware fluidity remain open in TODO.md.
