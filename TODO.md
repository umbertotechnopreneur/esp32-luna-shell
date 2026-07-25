# TODO

## BLE Wi-Fi pairing task

- [x] BLE-only Espressif provisioning flow; no SoftAP and no PIN/PoP.
- [x] Full-screen device QR and Google Play fallback QR.
- [x] Wizard routes for Bluetooth, app installation, QR scanning, retry and result.
- [x] Global reusable settings singleton with NVS persistence.
- [x] Provisioning lifecycle bridge for the hardware foundation.
- [x] Hardware-free static verification: scripts/verify_pairing.py.
- [ ] Validate the QR and credential flow with the physical radio and the Espressif mobile app.
- [ ] Verify reboot persistence, repeated pairing, timeout and BLE shutdown on hardware.

## ESP-Brookesia integration

- [x] Run the QSee.ai application as a native System Core `IApp` under System
  Super.
- [x] Express the six current screens and `qsee_content` flow in the GUI LVGL
  JSON package.
- [x] Stage System Super and QSee.ai resources in the internal LittleFS image.
- [ ] Define shared QSee.ai JSON design tokens for the circular 466 x 466
  display and 320 x 320 safe area.
- [ ] Add Bluetooth, GPS, and CPU indicators to System Super only when an
  official extension point is available; record the framework gap here before
  adding product-specific system chrome.

## SD formatting

- [x] Keep internal LittleFS resources independent from the removable SD
  volume.
- [x] Represent card absent, ready, and format-required states through
  `DeviceSnapshotService`.
- [x] Arm `qsee_storage_request_format()` only after the destructive System
  Super confirmation is accepted.
- [ ] Validate cancel, confirmed format, progress, failure, and recovery with a
  disposable card on hardware.

## Device metrics

- Add CPU utilization samples to `DeviceSnapshotService` when sensor fusion exposes them.
- Keep the UI on `CPU data unavailable` until real samples are available.
- Keep hardware refresh non-blocking and outside the UI thread.
