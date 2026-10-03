# TODO

Only open, actionable work belongs here. Deferred ideas live in `planned-todo.md`;
completed work lives in `done.md`. Hardware checks below require explicit device
authorization.

## Current integration

- [ ] Integrate the Welcome/OOBE screens with the async pairing controller:
  routes 05/06/06-bis/07/08/09, copied session QR, reusable semantic action
  button, and navigation only after confirmed Cancelled. Contract:
  `docs/architecture/cody-pairing-integration.md`.
- [ ] Obtain explicit authorization for the final connectivity validation and
  device checks. Cover first pairing, failure reasons, Retry/new QR, Back,
  Cancel while starting/connecting, timeout, DHCP, telemetry handoff, standby,
  persisted-profile reboot and opt-in authenticated HTTP over the actual LAN.

- [ ] On fresh explicit authorization, validate the integrated `main` policies
  together: Debug/Release diagnostic gates, Cody Sleep/Wake preparation, first
  dark Wake frame before the 300-ms fade, and recovery after a rejected sleep.
  The branch integration has static source review only.

- [ ] After fresh specific authorization, validate the offline routine/clock
  increment: atomic preferences, overnight quiet window and Rome DST, missing/
  corrupt settings, unavailable/stale neutral state, onboarding versus Wake
  greetings, old/new title damage, bounded boot cue gating, first-IP NTP
  failure/recovery and measured RTC offset/rate before correction. Verify an
  unset RTC reaches NTP without aborting driver startup. No build, tests,
  simulator or hardware operations ran. See `docs/architecture/cody-clock.md`.

- [ ] After fresh explicit authorization, build/deploy the enabled Cody RTC
  profile and observe trusted SNTP -> RTC persistence, offline restart,
  oscillator-stop invalidity and backup retention. No build, checks or hardware
  operation ran for this activation. See `docs/architecture/cody-rtc.md`;
  external RTC INT is not a direct Deep-sleep wake route on this PCB.

- [ ] After fresh specific authorization, compile and observe the current Cody
  face lifecycle: Calm/Happy/Calm on ordinary boot, continuous breath/blink,
  35A / Sleep before display suspension, and 35B / Wake -> 16 / Idle while
  restoring the first frame before panel exposure. Verify runtime expression
  selection, rejected keys, transitions from diagnostic Pause, and actual frame
  cost. Check branding retirement with greeting enabled and explicitly disabled,
  screen-04 Happy/HUD behavior and whitesmoke text. No build, tests, simulator or
  device operation ran for this change.
  See `docs/architecture/cody-face-lifecycle.md`.

- [ ] Review the new first-boot greeting in the Windows simulator and on the
  device after fresh, specific authorization. Implementation and source assets
  are complete; checkpoint `1df7ee0a` compiled and flashed on COM10 with the
  forced first-boot flag enabled. No simulator, tests, separate size report
  or physical display observation ran for this change. A later authorized
  45-second boot capture showed the greeting entering and continuous frames
  at 19-20 FPS/core 0 around 81-83%, but not the reported freeze. The next
  checkpoint restores its measured HUD, uses 22.05 kHz cues at volume 100 and
  enables shared cardinal auto-rotation; physical sound and orientation remain
  to be observed after deployment.
  Checkpoint `b663f2ac` was subsequently flashed successfully on COM10 with a
  verified 2,074,320-byte application. Review the new 400 ms bitmap fade-out,
  two-second animated cloud fade-in, then typewriter, eye taps and elastic drag.
  This intro/touch revision `d922d75a` subsequently compiled and flashed on
  COM10 (2,081,600-byte app, verified hash and reset); no tests, simulator or
  post-flash capture ran. Physical motion/touch confirmation remains open.
  Check the two-second Ready deadline, happy pearl face, snapshot
  motion, rotations and pause/standby timing; measure real frame/CPU and memory
  cost rather than infer it from the smaller cloud. See `docs/architecture/cody-scenes.md`.
- [ ] On fresh explicit validation/hardware authorization, validate Release
  touch/IMU Deep-sleep and Debug software standby: QMI revision/protocol, 60-second
  runtime timeout and persistence, 300-ms fades, held-IRQ rejection/recovery,
  repeated wake boots, USB/radios and complete-board current including powered
  GPS. Source implementation only; see `docs/architecture/release-deep-sleep.md`.

- [ ] On fresh explicit validation authorization, validate the common Debug/Release
  split: build-mode selector/configuration rejection check, console/profile/power
  checks, both modes' clean/incremental builds, size and simulator behavior.
  Separately authorize physical PWR/IMU/touch wake, USB reconnect, radio behavior
  and active/standby current. Implementation has static review only. Evidence:
  `docs/architecture/firmware-build-modes.md`.

- [ ] Identify the intermittent display/touch stop reported after the pearl
  release. The first capture began after failure and shows DISPLAY/TOUCH in
  error while the MCU continues snapshots. The triggering error is absent.
  Boot and later captures did not reproduce it through uptime 487 seconds;
  the gap between those captures was not continuously observed. Do not claim
  the issue fixed. Evidence: `docs/architecture/cody-cloud-finish.md` and
  `artifacts/serial/cody-jelly-crash*.log`. The later 45-second greeting capture
  also did not reproduce a freeze; its evidence is preserved in
  `artifacts/build-release-evidence/cody-greeting-freeze.log`.
  The later authorized 45-second live capture of `b663f2ac` without reset
  showed continuing frames and successful rotation, but no panic/reset cycle
  or preceding failure (`artifacts/build-release-evidence/cody-reboot-loop-live.log`).
  The user subsequently reports the device is working; retain this item until
  the original intermittent failure is explained or explicitly closed.
- [ ] Review wider rotation/navigation coverage and runtime face-preference
  behavior on hardware. The user accepted the pearl appearance; the native
  renderer and face-follow preference are enabled by default but selectable.
  The latest face revision intentionally has no new test run. The older device
  capture measured about 17-18 FPS and core 0 at 82-84%; 25 FPS is only the
  target. Optimize after the visual iteration, not during this delivery.
  Scope and flags: `assets/cody/experiments/jelly-scanline/README.md`.

- [ ] Review the opt-in PNG/triangle cloud experiment with the user before any
  device port. The previous full workspace is checkpointed at `3b90f25a`;
  the experimental source is retained on `main` after branch consolidation.
  A painted transparent
  body now follows the existing lobe anchors through 99 vertices/138 triangles,
  with the face separate. Static PNG, mesh overlay and synthetic GIF previews
  are under `artifacts/simulator/cloud-png-mesh-*`. No new tests or hardware
  measurements ran. CPU cost, native texture sampling, device integration,
  extreme motion/navigation clearance and visual approval remain open.
  Scope and source prompt: `assets/cody/experiments/png-mesh/README.md`.

- [ ] Finish the cloud-volume and jelly-like IMU default iteration. Review the
  PNG/mesh direction above; the continuous height-field study
  (`artifacts/simulator/cloud-study-billows-v5.png`) was rejected because its
  added relief did not sufficiently match the border lobes. Those two studies
  remain host-only; the separately approved pearl scanline renderer is native.
  Performance
  optimization is explicitly deferred. The second flashed cloud candidate
  sustained roughly 29 FPS but CPU1 60-63%, so low CPU cost is not achieved.
  Current touched-event AA work and the newer artwork/motion defaults postdate
  the passing 27 PoC checks and native raster package. Before final delivery,
  obtain fresh test authorization, review motion/navigation clearance, regenerate
  the final assets and measure the approved firmware. All sensor consumers must
  use the canonical snapshot; no direct sensor-read path may be introduced.
  Evidence and revision boundaries: `docs/architecture/cody-cloud-finish.md`.

- [ ] Reduce Cody animation CPU usage and meet the sustained frame budget in
  both artwork and skeleton views. The pre-raster candidate is compiled and
  flashed: artwork capture shows around 29 FPS with CPU1 45%, CPU2 0%,
  frame mean about 26 ms and raster about 7 ms. The matched skeleton capture
  shows 27.9 FPS, CPU1 68%, CPU2 0%, and 15 ms raster. Aligned RGB565 word
  conversion improves it to 28.7 FPS, CPU1 66%, and 2 ms conversion. Prepared
  link/tangent batches then reach 28.9 FPS, CPU1 64%, 14 ms raster and 6 ms
  skeleton. Prepared markers reach 29.2 FPS, CPU1 63%, 13 ms raster and 5 ms
  skeleton; marker cost falls from 2 ms to 1 ms. Conservative per-tile fill
  edge shortlists then bring artwork to 29.4 FPS, CPU1 33%, 21 ms frame mean
  and 4 ms artwork raster. The matched skeleton capture reaches 29.4 FPS,
  CPU1 55%, 29 ms frame mean and 10 ms total raster.
  Pixel comparison/packing are removed, but conservative rectangles increase
  sent bytes; artwork, skeleton strokes, conversion, submission and sleeping
  DMA wait are now the remaining measured costs. Rank the next exact-pixel CPU
  change before considering a more invasive asynchronous DMA pipeline.
  Obtain a logged static-route CPU baseline before
  attributing the remaining work to hardware services. Evidence:
  `docs/architecture/cody-render-performance.md`.
  The pre-raster damage/compact-tile, reduced-log, aligned word-conversion and
  prepared-stroke, prepared-marker and fill-edge-shortlist candidates have
  narrow compilation/device timing evidence.
  On fresh test
  authorization, check compact/full pixel parity,
  old/new damage reconstruction and device artwork/skeleton/pause/rotation;
  confirm pixel correctness and measure sent bytes as well as FPS/stage costs. Asynchronous double-buffer
  DMA and multicore raster remain the next implementation stages.

- [ ] Finish the Cody offline PoC's physical checks: acoustic quality of both
  boot cues, real charge/microphone response, repeated eye taps, standby and
  0/90/180/270 input alignment, sustained FPS/CPU distributions, stack margins
  and Wi-Fi/BLE coexistence under load. Compile/flash, startup transfers, cue
  writes and continuous animation/taps now have narrow hardware evidence;
  host response/raster contracts passed. Evidence:
  `docs/architecture/cody-render-performance.md`.

- [ ] Complete repeated physical Back/overlay/press-release checks, rotation
  and power transitions for the restored Luna touch/IRQ path. USB access,
  flash and continuous demo/tap response were recovered and measured. The
  older invalid-touch freeze remains historical evidence. Long-run stability
  and hardware validation of the separate Luna ports remain open. Details:
  `docs/architecture/cody-device-debug.md`.

- [ ] On explicit validation request, validate the code-review mitigation checkpoint:
  standalone frame deadline and all release/error paths; one transient DMA lease
  and watchdog per frame; retained DMA/consumer byte counts and failure health;
  module-aware snapshot/pairing/log checks and typed log filters. Include UI-on,
  standalone and display-off source lists. No tests, builds or validation scans
  ran for this checkpoint. Blueprint: `docs/architecture/code-review-mitigations.md`.

- [ ] Measure native raster and preview optimizations on explicitly authorized
  hardware: frame distributions, concurrent standby/rotation, deliberately slow
  DMA, task stack and radio memory. Portable raster/rig parity and PoC checks
  passed in the integrated working copy; no device performance gain is measured.

- [ ] Continue R6 in a separate focused stage: isolate snapshot storage/staging
  and sensor/processor acquisition without changing canonical lock ownership.
  Continue R3 segment/debug culling only after native raster parity evidence;
  keep repeated intersecting-path validation explicit until its ownership contract
  is ready. Remaining Luna presenter families are deferred in `planned-todo.md`.

- [ ] Validate real Cody snapshot cadence, board-axis signs, charge/microphone
  response, standby/wake and radio headroom on explicitly authorized hardware.
  Host response/model and PoC checks passed, including native/Python equations,
  source validity, composable fatigue/stress and eye-tap recovery. Hardware
  evidence remains open. Blueprint: `docs/architecture/cody-response-engine.md`.

- [ ] On explicit validation request, validate the readability refactor:
  route-to-document ownership, navigation Back/Forward/return-to history, and
  zero-to-five command layouts. Implementation has static source review only.
  Include the separated document-action module: callback ownership, stale
  document generations, standby gating and cleanup after partial subscription.
  Include named v2 skin generation/selection, reordered YAML declarations,
  palette IDs, independent face/contour controls and pause/resume. The active
  rig header and manifest were regenerated; no build or simulator checks ran.
  Include frame-lock release on all render exits, measured frame timing and
  the separated preview presentation/inspector/readout methods.

- [ ] On explicit validation request, run the consolidated console's
  `device.ps1 check -Check device-console` and `script-contracts` checks, then
  exercise menu/CLI routing, simulator captures, relocated imports, packaging,
  cleanup and failure logging. Firmware build/flash paths retain separate
  authorization requirements. This tooling reorganization has static review only.

- [ ] Measure the contour and named-overlay extension on explicitly authorized
  hardware: smooth slow/fast target changes, physical point/touch alignment,
  facial alpha and overlay cost. The authorized host PoC checks passed, including
  named geometry, responsive Tk layout, interpolation and cardinal rotations.
  Keep physical performance evidence separate from those host assertions.

- [ ] On explicit firmware/device validation request, compile the Cody rig PoC
  and measure frame geometry/raster/transfer times with the overlay on and off,
  real 0/90/180/270 touch alignment, standby/wake, stack high-water mark and
  Wi-Fi/BLE headroom. Portable native raster/rig contracts passed in the integrated
  working copy; physical framebuffer output and firmware fluidity remain unverified.

- [ ] On explicit request, resolve/review the `cody-companion` dependency lock,
  then validate the finished no-LVGL source/dependency closure, clean/incremental
  build and size. Physical boot, mandatory display/touch, 0/90/180/270 rotation,
  standby/wake and Wi-Fi/BLE coexistence require separate device authorization.
  Firmware still has static source review and asset conversion only; the local
  rig's separate host validation is recorded in `docs/architecture/cody-rig-poc.md`.

- [ ] On explicit request, resolve and review the new `display-touch` dependency
  lock and refresh affected existing locks after the BSP manifest gates change.
  Validate UI-on and standalone display/touch builds, source/dependency exclusion,
  then separately authorize physical display, touch, rotation, DMA and standby
  checks. The standalone checkpoint has static review only.

- [ ] Only on explicit user request, validate the board-service corrections:
  shell-owned PWR quiescence/wake, boot-owned periodic sampling, battery-off PMU
  charging policy, directional codec source selection, and build-directory-only
  cleanup. Tests are deferred until requested. The final package includes
  `verify_board_services.ps1`, a clean/incremental build and size report,
  simulator and repository checks;
  inspect source lists/symbols for all-off, all-services, output-only, input-only,
  and PMU-on/battery-off configurations. Hardware checks need separate explicit
  authorization. SD dependency work remains deferred in `planned-todo.md`.
  Recheck the clean helper's restoration of an absent environment variable:
  the initial host check exposed an empty/null mismatch, corrected after tests
  were stopped; the correction has not been rerun.
- [ ] Finish the ESP-Brookesia 0.8 upstream merge, refresh versioned dependency
  locks, and run the repository's final validation package. Verify actual UI and
  power behavior on a device only after authorization.

## Hardware validation

- [ ] Validate the Leg Edition image on the attached board: confirm the five
  IMU/movement/settings views show only fresh measurements, the three movement
  sound levels play through ES8311, the display turns off after 25 seconds and
  wakes on qualified motion or PWR, and rotation/180-degree flip and saved
  sound/wake settings survive reboot. Confirm Wi-Fi, BLE, GNSS, microphones,
  and distance stay disabled in this profile. Record firmware image hash and
  serial evidence; do not infer weight bearing from IMU movement.
  On 2026-09-27, the motion-policy image SHA-256
  `AE130B0AD2745BD7776AAE2BE33860963896AD23C45E7E12251D0E1A544AD478`
  was flashed to COM8. Serial captures in `artifacts/leg-edition-motion-20260927.log`
  and `artifacts/leg-edition-deliberate-motion-20260927.log` show no repeated
  cue on the vibrating table, two level-3 cues on deliberate motion, motion
  wake, and display-off after roughly 25 seconds. Levels 1/2, the five views,
  PWR wake, saved settings, and full rotation behavior remain to be checked.
  The later UI revision, SHA-256
  `14EAC41A6B4AD4853BE9E302F4FBDE0C860F75246A0A4148B5CF893A91F39937`,
  was flashed to COM8 after three simulator capture rounds under `artifacts/`.
  Device serial capture `artifacts/leg-ui-device-20260927.log` showed no GUI
  error while idle; visual checks on the real display are still pending.
  The latest UI image, SHA-256
  `5F4CCE9482DE381D044B0ABDBFD55B28643C0801B7151ADB36B64AB2DCFD53AE`,
  was flashed to COM8 on 2026-09-27. Simulator captures under `artifacts/leg-ui-v6-*.png`
  cover all five pages, moving synthetic IMU indicators, a graph alert, and
  navigation hidden after five seconds at rest. The serial capture
  `artifacts/leg-ui-final-20260927.log` confirms the delayed Leg screen load
  and contains no GUI failure. The new visual layout and navigation timing
  still need confirmation on the physical display.
  The subsequent five-page revision, SHA-256
  `5BA7EAC01F597C5BA3746E9447794898AA95DEA682E4B130176795DBAD8DC9FE`,
  removes page counters and card frames, adds a relative peak and independent
  ACC/ROT activity indicators, and was flashed to COM8 on 2026-09-27. The
  simulator previews are `artifacts/leg-ui-v8-*.png`; serial capture
  `artifacts/leg-ui-acc-rot-final-20260927.log` confirms the new lazy GUI loads
  after the startup delay without a GUI error in the capture window. Visual
  behavior and blinking on the real panel remain to be checked.
  The latest UI revision, SHA-256
  `E590030873EE1C66DFB92CFEE1724BF3D4310D9D5D44C69A44002470910B9F1A`,
  uses native line icons for ACC/ROT, 500 ms fade phases, a pale alert pill
  with dark text, and an opaque dropdown popup background. It was flashed to
  COM8 on 2026-09-27. The capture
  `artifacts/leg-ui-fade-popup-final-20260927.log` confirms the Leg document loads
  and the 100 ms animation refresh starts, but does not verify the physical
  popup or sensor animations; obtain the promised device photo/video later.
  The subsequent UI image, SHA-256
  `6041536FD203FB795DC9BF6AC828FC7AD67E52DB8AA08B57C71ED9454CFA20E9`,
  was flashed to COM8 on 2026-09-27 with LittleFS image SHA-256
  `BA57C4160CA34E0857980D8F7476DB6A5F4B4C8F7BFC5F59969CFCA3E5FA22D8`.
  It uses a 250 ms fade-in and 1.5 s fade-out, circular lower navigation
  buttons enlarged by 3 px and lowered by 5 px, a lower relative-peak label,
  and right-aligned two-decimal monospace values on ACCEL + GIRO. Serial
  capture `artifacts/leg-ui-fast-slow-fade-20260927.log` confirms the Leg
  document mounts after the startup delay with a 100 ms refresh and no GUI
  error in the capture window. The physical layout, popup, and animations
  still need a visual check on the device.
  The boot-chime image, SHA-256
  `5F30C225196B543C1557F8DD9769D7EE59214EA5B2CE0EC0D694251ECD2765E5`,
  enabled the embedded 1.8 s WAV for Leg Edition and was flashed to COM8 on
  2026-09-27. `artifacts/leg-boot-audio-serial-20260927.log` records ES8311
  ready and `Playback complete` with `ESP_OK` at 22.05 kHz and volume 90.
  Confirm that the chime is audible on the physical device; serial success
  alone does not verify acoustic output.
  The louder audio image, SHA-256
  `FD9D8FB0A503B538C06886DBFC3C853A1B4ACBBB4E2725F2E099BA830916F1D9`,
  was flashed and verified on COM8 on 2026-09-27. The boot WAV was raised
  5 dB and plays at codec volume 100; the three synthesized movement cues
  use twice the PCM amplitude and codec volume 95. Serial capture
  `artifacts/leg-audio-louder-serial-20260927.log` records boot playback
  `ESP_OK` and the Leg screen loading; it contains no deliberate movement
  cue, so check the alert loudness on the physical speaker.
  The lower previous/next controls now use smaller filled, muted-green circles
  with the original touch targets. The simulator preview is
  `artifacts/leg-motion-nav-preview.png`. Firmware image SHA-256
  `69C47732DAC8CD712F71767F16994B52AC2CCCFD0D37A174E66BD7032E7AD492`
  and LittleFS image SHA-256
  `8282F023B283A4792C22C17B35287C0B96CE6B9701C3BC2A6C85A6BD0996FCC5`
  were flashed and verified on COM8 on 2026-09-27. Confirm the new controls'
  color, size, and touch behavior on the physical display.
- [ ] Investigate why the Leg Edition periodic snapshot reports `RTC=error`
  although RTC is disabled by the build profile. Preserve the actual hardware
  and time-service error states while correcting the summary or publication.
- [ ] Inject controlled GUI, display, snapshot, NVS, provisioning, audio, and
  Wi-Fi stalls on a disposable device. Verify the 12-second Task-WDT bound and
  retained liveness breadcrumb after reboot.
- [ ] Measure settings, provisioning, and boot-audio worker stack high-watermarks
  under worst-case operations before changing static stack sizes.
- [ ] Verify BLE credential setup with the Espressif mobile app, reboot
  persistence, repeated pairing, timeout, shutdown, and responsive Settings UI
  during Wi-Fi/BLE operation under serial capture.
- [ ] Measure Brookesia bootstrap and repeated cross-route navigation; confirm
  one active lazy route document and stable memory after unload.
- [ ] Validate LUNASHELL-owned Bluetooth, GPS, and CPU status on the device.
- [ ] Validate SD cancel/format/progress/failure/recovery on a disposable card;
  investigate intermittent card detection and surface diagnostics in the UI.
- [ ] Verify both TOF400F modules, R15/R16 isolation, shared-TX/dual-RX wiring,
  UART pins, calibration, persistence, timeout/recovery, memory, tilt correction,
  and Settings controls before enabling distance in a release image.
- [ ] Repeatedly exercise Performance Mode and 0/90/180/270-degree rotations
  under serial capture, including rejected transitions and rollback.
- [ ] Validate Device Standby with auto-rotation and diagnostics enabled. Repeat
  at least five PWR standby/wake cycles and check arc timeouts, 50% dimming,
  panel-off, fresh-orientation replay, and restoration of requested streams.
- [ ] Measure active/standby electrical current with radios on and off; verify
  physical ES8311/ES7210 current, qualified IMU wake, and automatic dim-to-
  standby PWR/motion recovery. Logs alone do not establish current draw.
- [ ] Validate LC76G I2C standby ACK/recovery, TTFF, accuracy, and electrical
  impact. True Backup mode needs a separately switched VCC rail absent here.
- [ ] Keep the validated 80 MHz ESP32-S3 CPU floor until repeated baseline PWR
  cycles pass after the LC76G fix; only then re-evaluate 40 MHz.
- [ ] Compare on-device FFT/detail latency, heap stability, and rendering after
  binding-cache changes; confirm unchanged frames cause no update batch.

## Product and UI

- [ ] Define shared JSON design tokens for the 466 × 466 circular display and
  320 × 320 safe area.
- [ ] Implement LUNASHELL-owned keyboard and confirmation dialogs before exposing
  log search or destructive actions without System Super.
- [ ] Evaluate native Brookesia components as replacements for app-specific
  label, button, progress, text, and log wrappers when those APIs exist.
- [ ] Add the Settings action for existing non-blocking IMU calibration.
- [ ] On PWR Device Standby, stop Wi-Fi, BLE provisioning, and BLE telemetry;
  leave them off after wake until the user restarts them. Preserve PWR and
  qualified, throttled IMU wake.
- [ ] Add an explicit System Core Settings action for the opt-in read-only HTTP
  API; do not auto-start it at boot.
- [ ] Improve FFT bar layout and gradients; decide hardware versus software
  microphone gain using measured signal and performance evidence.

## Publication

- [ ] Complete the UI/startup-asset branding and legal sweep before GitHub
  publication, including proprietary identifiers and logo mentions.
