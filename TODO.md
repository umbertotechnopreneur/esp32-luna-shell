# TODO

Only open, actionable work belongs here. Deferred ideas live in `planned-todo.md`;
completed work lives in `done.md`. Hardware checks below require explicit device
authorization.

## Current integration

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
