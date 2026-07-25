# TODO

## BLE Wi-Fi pairing task

- [x] BLE-only Espressif provisioning flow; no SoftAP and no PIN/PoP.
- [x] Provisioning lifecycle bridge for the hardware foundation.
- [x] Defer Wi-Fi/BLE allocation until the explicit Settings action.
- [x] Hardware-free backend and System Core UI verification:
  `scripts/verify_pairing.py`.
- [ ] Design and implement an on-device provisioning/QR experience if the
  product requires one; no pairing wizard or QR route exists today.
- [ ] Validate the BLE credential flow with the physical radio and the
  Espressif mobile app.
- [ ] Verify reboot persistence, repeated pairing, timeout and BLE shutdown on hardware.

## ESP-Brookesia integration

- [x] Run the application as a native System Core `IApp` under System Super.
- [x] Express the seven current screens and `qsee_content` flow in the GUI LVGL
  JSON package.
- [x] Expose product Settings for diagnostic cadence, IMU smoothing, SD log
  mirroring, deferred radio startup, and persisted brightness.
- [x] Stage System Super and application resources in the internal LittleFS
  image.
- [ ] Define shared JSON design tokens for the circular 466 x 466 display and
  320 x 320 safe area.
- [ ] Add Bluetooth, GPS, and CPU indicators to System Super only when an
  official extension point is available; record the framework gap here before
  adding product-specific system chrome.
- [ ] Contribute or replace app-specific wrappers when ESP-Brookesia exposes native label, button, progress, text and log components.
- [ ] Remove proprietary branding markers (including branded identifiers and
  logo mentions) before GitHub open-source publication.

## SD formatting

- [x] Keep internal LittleFS resources independent from the removable SD
  volume.
- [x] Represent card/transport unconfirmed, ready, and format-required states
  through `DeviceSnapshotService`; the board has no card-detect pin, so a
  failed SDMMC probe is never reported as physical absence.
- [x] Make only an exact FatFs `FR_NO_FILESYSTEM` result eligible for format.
- [x] Arm `qsee_storage_request_format()` only after the destructive System
  Super confirmation is accepted, bind that one-shot permission to the
  observed CID fields and capacity, and consume it on the immediate attempt.
- [x] Poll a mounted card and invalidate/unmount it when status reports removal
  or a transport failure.
- [ ] Validate cancel, confirmed format, progress, failure, and recovery with a
  disposable card on hardware.
- [ ] Address SD-card detection regressions (card not detected in some boots) so detection and diagnostics are surfaced clearly in UI.
- [ ] Add a dedicated Storage section in Settings to surface card presence/state, free space, mount status, and safe format/reset options (with explicit confirmation and progress).

## Device metrics

- [x] Add real per-core CPU utilization samples to `DeviceSnapshotService` using
  ESP-IDF/FreeRTOS idle runtime deltas and explicit availability flags.
- Keep the UI on `CPU data unavailable` until the first valid previous-sample
  delta exists or runtime statistics are unavailable.
- Keep hardware refresh non-blocking and outside the UI thread.

## Snapshot integrity

- [x] Commit a full hardware refresh as one snapshot generation and advance the
  generation for asynchronous subsystem reports.
- [x] Apply component freshness TTLs and hide expired numeric telemetry as
  `STALE`.
- [x] Add deterministic tests for transaction visibility, TTL expiry, and
  recovery after a later successful sample via `scripts/verify_snapshot_integrity.py`.

## Sensor fusion

- [x] Add a canonical backend sensor-fusion service behind
  `DeviceSnapshotService` for gravity-dominant and stationary motion state;
  keep orientation output unavailable until a validated reference frame exists.
- The Settings route persists the QMI8658 input low-pass smoothing value and
  enables only the guarded cardinal gravity classifier used for display
  rotation; this is not a general orientation-fusion service.
- Add a canonical sensor-fusion service behind `DeviceSnapshotService` before
  exposing algorithm selection, gain, reference-frame controls, or
  pitch/roll/yaw values in the UI. Until then the snapshot exposes the real
  filtered/calibrated IMU values with timestamp and availability only.
- Stationary calibration now runs on a non-blocking platform worker; wire a
  Settings calibration action on top of it when the UI work lands.

## Power and performance profiles

- [x] Add the backend display-power API with transient `Active`, `Dimmed`, and
  `DisplayOff` transitions, preserving the user's configured brightness and
  publishing both configured and applied values in `DeviceSnapshotService`.
- Add an explicit UI/display power mode flow with `Active`, `Dimmed`, and
  `DisplayOff` states tied to real inactivity/sleep transitions.
- When the main sleep flow dims the display, reduce AMOLED brightness to 50% of
  the user's configured value rather than a fixed percentage.
- While dimmed, slow non-critical UI refresh work from the normal 1 second
  cadence to about 5 seconds where the current screen allows it.
- When the display is fully off, stop GUI refresh timers entirely and resume
  them only after wake.
- Keep event-driven platform state, critical logs, and fail-fast errors
  truthful; do not keep periodic UI timers alive just to manufacture stale or
  placeholder values.

## Deferred HTTP API activation

- [ ] Add an explicit System Core Settings action for the read-only HTTP API
  after `ApiKey` and `http_enabled` are deliberately persisted; do not start
  the server automatically during boot or invent missing settings.

## Audio scope visualization

- [ ] Add the "FFT scope" action to the Brookesia JSON/System Core audio page.
- [ ] Improve FFT sensitivity — currently input must be significantly amplified (e.g., physical microphone tapping) to show visible spectrum; investigate gain levels and response thresholds.
- [ ] Redesign FFT bar layout: reduce margins, apply color gradients instead of flat colors for better visual appeal.
- [ ] ? Investigate microphone gain/volume control: should we apply amplification via hardware (pre-processing) or in software (post-processing)?

## Open-source publication prep

- [ ] Scan repository for proprietary identifiers (for example branded names,
  logos, internal contact/footer signatures) and replace/remove with
  neutral/open-source equivalents.
- [ ] Remove or generalize internal/legal notice text from scripts, startup splash,
  and user-facing docs before publishing to GitHub.
- [x] Add a pre-release checklist that includes branding sweep, placeholder
  replacements, and export-safe defaults via `docs/release/open-source-pre-release-checklist.md`.
