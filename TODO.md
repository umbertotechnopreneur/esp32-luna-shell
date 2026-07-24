# TODO

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
