# Active work

Only open, actionable work belongs here. Deferred work is in
[planned-todo.md](planned-todo.md); completed implementation/evidence is in
[done.md](done.md). Authorization is governed solely by [AGENTS.md](AGENTS.md).
Unless explicitly recorded in Done, recent increments have source review only;
older host/build/flash evidence does not validate the current combined image.

## Cody integration and release gates

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
- [ ] Validate connectivity end to end: first pairing, actual failure reasons,
  Retry/new QR, Back/Cancel during Start/connect, timeout, DHCP, BLE telemetry
  handoff, standby, persisted-profile reboot and explicitly enabled authenticated
  HTTP on the actual LAN.
- [ ] Validate transient 60-second pairing standby overrides without changing
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
- [ ] Review scene entrance, Ready/branding/greeting retirement, Happy/open eyes,
  independent breath/blink, eye taps and elastic drag. Exercise expression
  selection/rejected keys, pause, software standby and all cardinal rotations.
  Check measured HUD validity, native renderer/face-follow selection and old/new
  text/artwork damage.
  [Runtime and scenes](docs/architecture/cody/runtime.md).
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
  [Freeze evidence](docs/architecture/cody/rendering.md);
  `artifacts/serial/cody-jelly-crash*.log` and
  `artifacts/build-release-evidence/cody-{greeting-freeze,reboot-loop-live}.log`.
- [ ] Meet sustained artwork/skeleton frame and CPU budgets. Establish a logged
  static-route baseline; measure stage distributions, payload bytes, DMA wait,
  stack/radio headroom and concurrent standby/rotation/slow-DMA behavior.
  Validate compact/full pixel parity and old/new damage before ranking exact-pixel
  CPU changes; asynchronous double-buffer DMA/multicore remain later stages.
  Existing narrow captures are not proof of the current image's target.
  [Rendering evidence](docs/architecture/cody/rendering.md).
- [ ] Finish offline PoC physical checks: both boot cues' acoustics, real charge/
  microphone response, snapshot cadence/board-axis signs, fatigue/stress/eye-tap
  recovery, contour slow/fast changes, facial alpha/overlays, touch alignment,
  native raster/transfer timings and Wi-Fi/BLE load. Host parity is not device
  geometry/fluidity evidence.
  [Rig and response](docs/architecture/cody/rendering.md).

## Platform, ownership and tooling

- [ ] Validate integrated Debug/Release configuration/gates, both modes' requested
  clean/incremental builds/size and simulator behavior. Separately verify real
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
  native errors. Physical serial behavior remains unverified.
- [ ] Resolve/review affected `cody-companion` and `display-touch` locks after BSP
  gates change; validate no-LVGL closure, UI/standalone builds and display-off
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
