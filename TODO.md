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

- [ ] Contribute or replace app-specific wrappers when ESP-Brookesia exposes native label, button, progress, text and log components.
- [ ] Register a QSee stylesheet for the circular 466 x 466 display and 320 x 320 safe area.
- [ ] Add native Bluetooth/GPS/CPU indicators to the standard Brookesia status bar when supported.

## SD formatting

- The UI handles card absent, format required, confirmation and progress/unavailable states.
- `qsee_storage_request_format()` is currently guarded by the hardware storage abstraction and must remain fail-safe.
- Implement formatting only after the hardware/storage abstraction exposes a documented formatter.

## Device metrics

- Add CPU utilization samples to `DeviceSnapshotService` when sensor fusion exposes them.
- Keep the UI on `CPU data unavailable` until real samples are available.
- Keep hardware refresh non-blocking and outside the UI thread.

## Sensor fusion

- The Settings route currently persists and applies the QMI8658 input
  low-pass smoothing value; it does not claim to provide orientation fusion.
- Add a canonical sensor-fusion service behind `DeviceSnapshotService` before
  exposing algorithm selection, gain, or reference-frame controls in the UI.
- Move stationary calibration to a non-blocking platform worker before adding
  a Settings calibration action.
