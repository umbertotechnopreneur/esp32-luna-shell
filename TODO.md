# Active work

Only open, actionable work belongs here. Deferred work is in
[planned-todo.md](planned-todo.md); completed implementation/evidence is in
[done.md](done.md). Authorization is governed solely by [AGENTS.md](AGENTS.md).
The latest feature-worktree Debug Cody image at `58757005` (`tts-sd`) was built
and flashed on COM10 on 2026-10-05. Its authorized 30-second boot capture
completes mandatory startup without the prior allocation abort; SD initialization
timed out before synthesis. Earlier ordinary Cody evidence belongs to
`09ea3b70` and `160a080a`. Verified flash hashes prove the written bytes;
every open physical observation below remains open.

## Optional local TTS on feat/luna-speak

- [ ] Verify SD initialization after the user reinserts the prepared card.
  Corrected `58757005` completes boot, with native BSS/text/package directory
  in PSRAM and speech creation after mandatory owners. The first corrected
  30-second capture shows two card-init timeouts and a real failed speech request;
  it does not establish mounted storage, synthesis or audible playback.
- [ ] After fresh exact-command authorization, validate the
  [implemented native integration](docs/architecture/text-to-speech.md): normal
  builds exclude the engine/data/task; SD firmware includes no speech payload;
  flash includes English only. Check isolated configuration/build/flash lists,
  source contracts, package generation and SHA-256 rejection paths.
- [ ] Establish firmware/PSRAM/stack/CPU budgets and continuous PCM performance
  for the chosen variant. Validate UTF-8 in all four languages, language
  rejection in flash, cancellation, clock policy, standby, codec lifecycle,
  card removal/changed package and terminal allocation failure. The authorized
  Cody `tts-sd` Debug configure/build/flash and SD package generation/copy completed
  on 2026-10-05. Tests/scans, size reports, simulator and runtime measurements
  remain open; existing main hardware evidence does not validate TTS.
- [ ] Validate the newly wired Italian boot phrase on `tts-sd`, one-shot admission,
  bounded initial SD wait and real failures, test-symbol exclusion from flash,
  diagnostic-only clock/quiet bypass and regular-request policy preservation.
  Verify TTS on core 0 and every Cody/LVGL render worker on core 1, including
  guarded LVGL patch application and actual simultaneous rendering/speech load.
  Cody `tts-sd` Debug compiles; physical listening and runtime/core observations
  remain unverified. Full Luna/LVGL and flash-English builds remain open.
- [ ] Decide additional speech triggers beyond the requested Italian boot test.
  No UI, scene, HTTP or assistant automatically requests speech. Authorized SD
  provisioning to G: completed; audible voice tuning remains open.

## Cody integration and release gates

- [ ] Remind the user after approval of the new boot behavior: update both Cody
  PDF atlases with the shortened 2 / 1.5 / 2-second cold-boot sequence and random
  returning-device Affection/Joy welcome, plus touch/icon/cycle corrections and
  enlarged/shifted icon placement and a final dated errata page. Preserve pairing
  and every previous PDF page.
- [ ] After fresh authorization, build/flash and review the icon readability
  correction: threefold scale-dependent extent (Full 87 px, Greeting 78 px,
  Medium 63 px, smaller eligible sizes at least 36 px), fixed +30/+40-pixel
  center translation and intentional upper-body overlap. Micro remains hidden.
  Check all eligible sizes, fourteen symbols, motion/rotation, circle/status/Back
  separation and frame cost. Current correction has static review only; the
  latest flashed image remains `09ea3b70`.
- [ ] Physically review the new 300-ms caress thresholds, heart interruption,
  eye-tap Fear/Frustration variation under stress, existing icon SFX and random
  complete three-scene cycles. Build/flash does not prove these observations.

- [ ] After fresh command-specific authorization, validate the 2026-10-05
  [idle scenes](docs/architecture/cody/idle-scenes.md): visible Stressed mouth
  breathing; random 20-40-second waits/no immediate repeats; Yawn, Whistle and
  Greeting; touch/Sophia/held-touch/stress/route/pause/standby cancellation;
  canonical own-whistle identity; quiet/mute/busy suppression and real errors;
  native/Windows visual parity and physical whistle acoustics. Code and PDF
  appendices received static review. Firmware checkpoint `09ea3b70` compiled and
  flashed successfully on COM10; no host tests, simulator or physical behavior
  observations were performed.

- [ ] Diagnose the reported caress freeze and missing heart. Capture the first
  rejected controller packet or terminal touch/display transition, then verify
  one continuous in-body stroke above the current 300-ms minimum and its synchronized
  Affection SFX. Current diagnostics distinguish zone exit, speed, acquisition
  gap and the other recognizer guards; do not relax driver acceptance or infer
  icon performance from the earlier HUD-only observation. Historical diagnosis,
  fixes and now-removed local captures are summarized in [done.md](done.md).

- [ ] After fresh command-specific authorization, run the pure host
  `scripts/device.ps1 check -Check cody-preview-raster` regression checks for
  sparse coverage and RGB565 composition. DPI and renderer corrections have
  static review only. The first Studio UI opened but the user reported blurred
  Windows scaling and ~100 ms host ticks, then closed it; leave it closed until
  a new restart is requested. Measure actual host timings and review DPI,
  controls, per-tab scrolling and gesture feel only after separate authorization.
  Different-DPI monitor transitions remain unimplemented.

- [ ] Review petting interactively and on the physical display after the
  2026-10-05 merged Debug build and focused host suite passed. Verify slow
  strokes above eyes/below mouth, threshold feel, heart clearance and perceived
  smoothness. When the Rig simulator is reopened, disable Debug geometry and select
  symbol None for automatic hearts. Native/host gesture trace parity remains
  unestablished. Confirm actual touch, busy-channel mood gain and park/resume
  timing on hardware only after separate authorization. Facial influence and
  persistence remain deferred; dormant wellbeing has no runtime adapter or UI.
- [ ] Validate the merged personality-icon implementation on the device:
  generated-palette/coverage contracts, all fourteen cues in Main
  and Sleep, suppression on other scenes and Micro, symbol readability, actual
  upper-body overlay, circle/status/Back separation, configured face anchor,
  bounded follow under pause/wake/rotation, stable selection, fade/hold/release,
  canonical device-state gating and old/new damage erasure. Listen to/refine all
  fourteen original SFX and verify completed-frame triggering, busy drops,
  cooldown, quiet/zero-volume/standby, idle-mute restoration, expiry, retained
  errors and codec contention. Profile complete frame/DMA/CPU/stack costs;
  static area counts and successful flashing are not physical performance
  evidence. Historical host results and merge evidence are in Done.

- [ ] Confirm normal-UI animation, touch/PWR response and repeated rotation/
  standby wake cycles with native DMA overlap on the physical display. Measure
  complete-frame and touch latency in the merged image; prior branch benchmarks
  and boot captures establish ownership only and are retained in Done.
- [ ] Validate the current face/pearl-boot/PWR-capture image:
  countdown timing, overlay-free BMP, original shutter cue,
  SD absent/failure feedback, Debug HUD placement, screen 04 open eyes and
  screen 08 Retry on the physical display.
- [ ] Capture repeated cold boots of the corrected image and at least two full
  60-second standby/wake cycles. Confirm no contiguous-DMA preflight rejection,
  teardown panic, touch critical-section watchdog or stale failed-wake trace is
  reported as a new failure. Verify degraded-radio cleanup and error/recovery
  publication; prior corrected-image captures are summarized in Done.
- [ ] Verify 05/06 return to Welcome after 30 awake seconds and real pairing 07
  after 90 awake seconds without
  real touch/pairing progress, including animation pause, standby, late worker
  results, cancellation errors and a fresh tap to restart. Verify deferred boot
  audio when the canonical clock becomes trusted, quiet hours and standby races.
- [ ] Capture the first renderer termination in the intermittent wake failure.
  `bb8fd327` was healthy at 204 s and already DISPLAY-error/INVALID_ARG at 314 s;
  the missing interval prevents cause attribution. The reset/reproduction log
  records clean standby at 64 s and touch wake at 179 s, confirmed by the owner.
  Do not treat that single success as a fix or hide terminal errors with restart.
- [ ] Investigate intermittent SD transport after warm resets/reinsertion.
  Reproduce one successful boot followed by standby/wake before changing policy;
  inspect card seating, power and signals. Validate mount, retry/backoff, access
  and free space while preserving the original OCR timeout. The board uses
  `SDMMC_SLOT_NO_CD`, so do not invent card-presence GPIO or format implicitly.
- [ ] Confirm boot greeting C acoustics, chime-to-voice timing, trusted-clock/
  quiet policy and no ordinary standby-wake repeat. A/B/chime-only authoring
  alternatives remain available.
- [ ] Verify clean Retry from 08 returns to 05 after the teardown/admission split.
  Exercise explicit Cancel/Retry, inactivity closure, standby rearm and repeated
  sessions. Preserve separate cleanup/reservation errors and actual SDK results;
  do not restore the disproven monolithic 30-KiB application guard.
- [ ] Investigate the interrupted 130-second capture: COM10's stream closed
  after 43 seconds without a captured panic. The following 100-second capture
  completed; the first USB/host interruption's cause remains unidentified.
- [ ] Continue raster optimization in separate checkpoints while preserving exact
  output. First measure separate jelly, face, preparation, RGB565 composition and
  complete-frame costs in the normal full UI; the 100% workload still misses the
  40-ms target. Historical A/B measurements are retained in Done.
- [ ] Complete finite raster validation on the merged main image: fresh physical
  pixel parity after row trimming, 50/75/100% coarse timing, disabled-benchmark
  behavior, non-Pearl firmware, and touch/PWR/Sleep interruption. Add deterministic
  animated work only after trustworthy timings; include packing, alignment, tails,
  device clock, radio state, logging overhead and cleanup.
  [Benchmark contract](docs/architecture/cody/rendering.md).
- [ ] Validate asynchronous look selection: save success/failure/retry, busy
  Back/Done, route reset during writes, rotated touch, canonical errors and
  bounded memory across repeated entries. Host persistence faults are covered;
  device races and leak-soak remain open.
  [Onboarding](docs/architecture/cody/onboarding.md).
- [ ] Validate current OOBE pairing 05/06/07/08/09: footer actions, immediate
  entry to 07, installation versus copied live-session QR, pending/ready repaint,
  error/online gates, `cody_firstboot=false` write/readback, Debug-only 39,
  Release skip to 09 and navigation only after observed cancellation.
  Check white-theme contrast, integer modules/quiet zones, circle clipping,
  translucent edges and all rotations; confirm real phone scans.
  [Pairing](docs/architecture/cody/onboarding.md).
- [ ] Physically confirm regenerated scene assets: `Scan to install` on 06
  and `Scan to pair` / `Scan me in the app.` with the actual session QR inside
  07. Verify title/caption separation, white theme and shared compact cloud.
- [ ] Validate connectivity end to end: first pairing, actual failure reasons,
  Retry/new QR, Back/Cancel during Start/connect, timeout, DHCP, BLE telemetry
  handoff, standby, persisted-profile reboot and explicitly enabled authenticated
  HTTP on the actual LAN.
- [ ] Validate transient 60-second invitation/install and 90-second real-pairing
  standby overrides without changing
  configured settings; confirm dim suppression and timeout restoration on exit.
  Exercise software Sleep -> Wake -> Ready with retained route, text/entrance
  clocks, paused expression/geometry and backend changes while asleep.
  Verify normal wake does not reset to 04; cold/deep-sleep reset remains distinct.
- [ ] Validate the continued first-run route 09 -> 10 -> 36 -> 11 -> optional
  12 -> 13 -> 16 (10 active seconds) -> 14 -> 15: screen-09 wake retention,
  the explicit Codex Kconfig gate, display-code timeout/retry, no Back on 11,
  mandatory row confirmation and disabled/enabled Done states. Exercise CCP1
  with one and many PCs, Windows private-firewall rules, wrong/expired codes,
  corrupt persisted records, packet loss, liveness loss/recovery and later
  re-entry through reusable 36/11. Wi-Fi/BLE alone must leave 14 pending.
- [ ] Validate teletype titles/body/footer/look/status and original microclick:
  completed-frame reveal only, no clicks on scrolling/replacement/resume,
  bounded notifications, quiet/time/volume gating, excluded-audio profiles,
  retained real audio errors, cancellation and physical loudness.
- [ ] Review scene entrance, Ready/greeting retirement, Happy/open eyes,
  independent breath/blink, eye taps and elastic drag. Exercise expression
  selection/rejected keys, pause, software standby and all cardinal rotations.
  Check measured HUD validity, native renderer/face-follow selection and old/new
  text/artwork damage.
  [Runtime and scenes](docs/architecture/cody/runtime.md).
- [ ] Verify regenerated rig/jelly assets and wider breathing mouth/tongue across
  every expression, all cloud sizes, overlays and pearl warp. Confirm closed
  poses stay tongue-free, clipped coverage has no leaks and the 12 KiB worker
  retains measured headroom; source review alone establishes none of these.
- [ ] Validate rounded graphite navigation and touch-only Debug reveal/fades,
  swipe versus drag/vertical-scroll ownership, rotated cancellation and repeated
  press/release. Cover real Back/overlay input and power transitions on both Cody
  and the separate Luna touch/IRQ ports.
  [Device debug](docs/architecture/cody/runtime.md).
- [ ] Validate native status/screenshot chrome: date/time and battery/Wi-Fi/BLE
  semantics, glyphs, touch fades, Screenshot hit bounds and hidden Pause/Skeleton
  targets. Cover rotations, atomic BMP output, absent/removed/full SD, concurrent
  log mirroring, Performance Mode, standby and PSRAM allocation failure.
  Firmware status/BMP behavior is unverified; the source-based preview checked
  only control visibility.
- [ ] Validate offline SD logging/native USB readback on the updated profile:
  cable connected with monitor closed, current/previous log identity and CRCs,
  absent/unformatted/removed/full SD, mirror off, boot-buffer loss, rotated/remounted
  files, interrupted USB, busy COM and standby/wake. Verify passive attach and
  explicit ResetBeforeMonitor. The reported boot/display loop is not proven fixed.
  [Offline logging](docs/architecture/offline-logging-usb.md).
- [ ] Validate offline clock routines: atomic timezone/quiet preferences, overnight
  windows/Rome DST, corrupt/missing settings, neutral unavailable/stale state,
  onboarding/Wake greetings, title damage and boot-cue gating. Check first-IP
  NTP failure/recovery, RTC offset/rate before correction, unset RTC reaching NTP,
  trusted SNTP-to-RTC persistence, offline reboot, oscillator-stop and backup
  retention. [Clock and RTC](docs/architecture/cody/clock.md).
- [ ] Resolve/review `cody-companion-wake-word` and `full-wake-word` locks through
  the public CLI, then perform the specifically authorized final validation.
  Measure Sophia false positives, AEC during audio, standby wake, diagnostic/
  Performance Mode suspension/resume, CPU/PSRAM/current and real model/capture
  failures. Other-board input integration is deferred.
  [Wake-on-word](docs/architecture/wake-on-word.md).
- [ ] Diagnose the intermittent display/touch stop, retaining actual failure
  evidence. Captures began after DISPLAY/TOUCH failure or did not reproduce it;
  later continuous frames/rotation and the user's working-device report do not
  identify the trigger. Keep open until explained or explicitly closed.
  Historical evidence is summarized in Done; the ignored local captures were
  removed by the authorized 2026-10-05 cleanup.
  [Freeze evidence](docs/architecture/cody/rendering.md).
- [ ] Meet sustained artwork/skeleton frame and CPU budgets. Establish a logged
  static-route baseline; measure stage distributions, payload bytes, DMA wait,
  stack/radio headroom and concurrent standby/rotation/slow-DMA behavior. Verify
  the Cody CPU0 radio/IP and CPU1 renderer partition under idle, pairing, HTTP and
  BLE telemetry load, including per-core utilization and frame latency.
  Validate compact/full pixel parity and old/new damage before ranking exact-pixel
  CPU changes. The current affinity moves one frame owner; a deeper parallel
  raster pipeline remains a later stage and must not create a second display owner.
  Existing narrow captures are not proof of the current image's target.
  [Rendering evidence](docs/architecture/cody/rendering.md).
- [ ] Finish Cody Alpha physical checks: both boot cues' acoustics, real charge/
  microphone response, snapshot cadence/board-axis signs, fatigue/stress/eye-tap
  recovery, contour slow/fast changes, facial alpha/overlays, touch alignment,
  native raster/transfer timings and Wi-Fi/BLE load. Host parity is not device
  geometry/fluidity evidence.
  [Rig and response](docs/architecture/cody/rendering.md).

## Platform, ownership and tooling

- [ ] Complete physical Debug/Release validation. The merged Debug Cody image
  `160a080a` was rebuilt and flashed twice on COM10 on 2026-10-05 with verified
  hashes; Release and other firmware profiles are not covered. Verify real
  Sleep presentation, dark Wake before panel fade, rejected-entry rollback,
  PWR/IMU/touch wake, USB reconnect, radios and complete-board current.
  For Release Deep-sleep cover QMI revision/CmdDone protocol, held IRQ recovery,
  runtime timeout persistence and repeated wake boots; Debug remains software
  standby. [Build modes](docs/architecture/firmware-build-modes.md),
  [Deep-sleep](docs/architecture/release-deep-sleep.md).
- [ ] Validate code-review ownership changes: standalone frame deadlines, release/
  error paths, one transient DMA lease/watchdog, retained byte counts/failure
  health, typed log filters and module-aware snapshot/pairing/log checks across
  UI-on, standalone and display-off source lists.
  [Mitigations](docs/architecture/display-touch-runtime.md).
- [ ] Finish R6 snapshot storage/staging and sensor/processor acquisition separation
  without changing canonical locks. Continue R3 segment/debug culling only after
  native parity; retain explicit intersecting-path validation until ownership is
  settled. Remaining presenter families are deferred.
- [ ] Validate readability/document-action changes: route ownership, Back/Forward/
  return history, zero-to-five commands, stale generations, standby gating and
  partial-subscription cleanup. Include named v2 skin/reordered YAML/palette IDs,
  independent face/contour controls, frame-lock exits/timing, pause/resume and
  separated preview presentation/inspector/readouts.
- [ ] Validate the consolidated console: `device-console` and `script-contracts`,
  menu/CLI routing, relocated imports, simulator captures, packaging/cleanup and
  diagnostic failure ownership. Cover deliberate COM selection before ESP-IDF,
  numeric/COM equivalence, cancellation, unavailable/ambiguous ports and retained
  native errors. Normal explicit COM10 flashing is verified; selection and failure
  edge cases remain open.
- [ ] Resolve/review the missing `display-touch` canonical lock after BSP gates
  change. The current Cody lock/native no-LVGL closure passed on 2026-10-04;
  still validate other UI/standalone builds and display-off
  exclusion. Separately verify mandatory display/touch, rotations, DMA, standby
  and Wi-Fi/BLE coexistence.
- [ ] Validate board-service corrections: shell-owned PWR, boot sampling,
  PMU-on/battery-off charging, directional codecs and build-local cleanup.
  Cover all-off/all-services/output-only/input-only profiles and the corrected,
  not-rerun absent-environment-variable restoration. The requested final package
  includes board-service verification; SD dependency removal remains deferred.
- [ ] Confirm/finish the ESP-Brookesia 0.8 upstream integration and refresh reviewed
  locks before final validation. Supported-version policy is not proof that this
  external integration/release gate is complete.

## Hardware and reliability

- [ ] Verify Leg Edition's current five views, fresh ACC/ROT/relative-peak readings,
  lower controls/dropdown/fades/navigation timing, levels 1/2/3 and audible boot/
  movement cues, PWR wake, saved sound/wake settings and full rotations/reboot.
  Confirm Wi-Fi/BLE/GNSS/microphones/distance remain disabled. Earlier serial
  evidence covers qualified motion/no table-repeat, level 3 and roughly 25-second
  display-off, not the latest physical UI. Record current image hash; do not infer
  weight bearing from IMU. Prior images/evidence are summarized in Done.
- [ ] Explain Leg Edition's `RTC=error` summary while RTC is disabled; preserve
  truthful hardware/time-service errors.
- [ ] On a disposable device, inject GUI/display/snapshot/NVS/provisioning/audio/
  Wi-Fi stalls; verify the 12-second Task-WDT and retained breadcrumb. Measure
  settings/provisioning/boot-audio worker stack margins before resizing.
- [ ] Verify Espressif-app BLE setup, reboot persistence, repeated pairing,
  timeout/shutdown and responsive Settings under Wi-Fi/BLE; measure bootstrap,
  cross-route navigation, single lazy document ownership and memory after unload.
- [ ] Verify device-owned Bluetooth/GPS/CPU status and FFT/detail latency, heap and
  rendering; unchanged frames must produce no binding-update batch.
- [ ] Verify SD detection/diagnostics and cancel/format/progress/failure/recovery
  on a disposable card, with explicit one-shot formatting approval.
- [ ] Before enabling distance in release, verify both TOF400F modules, R15/R16
  isolation, shared-TX/dual-RX wiring/UART pins, calibration/persistence, timeout/
  recovery, memory, tilt correction and Settings controls.
- [ ] Repeat Performance Mode and all rotations, including rejection/rollback.
  With auto-rotation/diagnostics, repeat at least five PWR standby/wake cycles:
  arc timeouts, 50% dim, panel-off, fresh orientation and restored requested streams.
- [ ] Measure active/standby current with radios on/off, ES8311/ES7210 shutdown,
  qualified IMU wake and automatic dim-to-standby recovery. Verify LC76G I2C
  standby ACK/recovery, TTFF/accuracy/current; true Backup needs a switched rail.
  Retain the validated 80 MHz floor until repeated PWR cycles pass; 40 MHz is
  deferred. Logs do not establish electrical savings.

## Product changes and publication

- [ ] Standardize character size/position presets across scenes. Decide whether
  04 and 05 share a preset (three total) or retain separate sizes (four total);
  preserve response composition and QR/content clearance.
- [ ] Define shared JSON design tokens for the circular canvas/safe area.
- [ ] Add product-owned keyboard/confirmation dialogs before log search or
  destructive actions; evaluate native Brookesia replacements when APIs exist.
- [ ] Add Settings access to the existing non-blocking IMU calibration.
- [ ] Review/implement the PWR policy that stops Wi-Fi/BLE provisioning/telemetry
  and leaves them off after wake until explicit restart. Existing suspend/resume
  behavior does not establish this policy; preserve PWR/qualified IMU wake.
- [ ] Add System Core Settings access to the opt-in read-only HTTP API; no boot
  auto-start.
- [ ] Improve FFT layout/gradients and choose microphone gain from measured
  signal/performance evidence.
- [ ] Complete startup/UI branding, proprietary-identifier and legal review
  before GitHub publication.
