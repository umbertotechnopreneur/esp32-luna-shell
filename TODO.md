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

- [x] Run the QSee.ai application as a native System Core `IApp` under System
  Super.
- [x] Express the seven current screens and `qsee_content` flow in the GUI LVGL
  JSON package.
- [x] Expose product Settings for diagnostic cadence, IMU smoothing, SD log
  mirroring, deferred radio startup, and persisted brightness.
- [x] Stage System Super and QSee.ai resources in the internal LittleFS image.
- [ ] Define shared QSee.ai JSON design tokens for the circular 466 x 466
  display and 320 x 320 safe area.
- [ ] Add Bluetooth, GPS, and CPU indicators to System Super only when an
  official extension point is available; record the framework gap here before
  adding product-specific system chrome.

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

## Device metrics

- Add CPU utilization samples to `DeviceSnapshotService` when sensor fusion exposes them.
- Keep the UI on `CPU data unavailable` until real samples are available.
- Keep hardware refresh non-blocking and outside the UI thread.

## Snapshot integrity

- [x] Commit a full hardware refresh as one snapshot generation and advance the
  generation for asynchronous subsystem reports.
- [x] Apply component freshness TTLs and hide expired numeric telemetry as
  `STALE`.
- [ ] Add deterministic tests for transaction visibility, TTL expiry, and
  recovery after a later successful sample.

## Sensor fusion

- The Settings route currently persists and applies the QMI8658 input
  low-pass smoothing value; it does not claim to provide orientation fusion.
- Add a canonical sensor-fusion service behind `DeviceSnapshotService` before
  exposing algorithm selection, gain, or reference-frame controls in the UI.
- Move stationary calibration to a non-blocking platform worker before adding
  a Settings calibration action.
