# TODO

## BLE Wi-Fi pairing task

- [x] BLE-only Espressif provisioning flow; no SoftAP and no PIN/PoP.
- [x] Provisioning lifecycle bridge for the hardware foundation.
- [x] Defer Wi-Fi/BLE allocation until the explicit Settings action.
- [x] Hardware-free backend and System Core UI verification:
  `scripts/verify_pairing.py`.
- [x] Restore the on-device Espressif provisioning wizard, dynamic QR route,
  and shared singleton Back/Next edge navigation under System Core.
- [ ] Validate the BLE credential flow with the physical radio and the
  Espressif mobile app.
- [ ] Verify reboot persistence, repeated pairing, timeout and BLE shutdown on hardware.
- [ ] Re-run Settings -> Start Wireless under serial capture and confirm that
  the adaptive DMA path keeps the UI responsive while Wi-Fi and BLE remain
  ready.

## Hardware status: immediate UI improvements

- [x] In the existing Hardware page, render a clear boot-state label for RTC,
  GPS, Wi-Fi, and BLE from `DeviceSnapshotService`: `READY`, `NO FIX`,
  `OFF BY POLICY`, or the real initialization error. Do not use an ambiguous
  generic "unavailable" state.
- [x] Show the distinction between a completed boot check and live use: RTC
  time restored versus unavailable; GPS receiver initialized versus current
  fix; Wi-Fi/BLE deliberately deferred until the user asks for them versus
  failed startup.
- [x] Add a single `Refresh hardware status` action that re-renders the latest
  snapshot without initializing radios, polling sensors directly from the UI,
  or changing device state.
- [x] On the Settings radios row, state the current policy explicitly: Wi-Fi
  and BLE stay off after boot and are started only by the existing user-driven
  actions. Surface the real startup error if either action fails.
- [x] Keep the legacy inventory names and ordering where useful, but derive all
  displayed status and timestamps from the current snapshot rather than a
  parallel inventory/probe path.

## ESP-Brookesia integration

- [x] Run the application as the only native `IApp` on System Core, without
  installing or starting the optional System Super shell.
- [x] Express the seven current screens and `qsee_content` flow in the GUI LVGL
  JSON package.
- [x] Expose product Settings for diagnostic cadence, IMU smoothing, SD log
  mirroring, deferred radio startup, and persisted brightness.
- [x] Stage only the QSee application resources in the internal LittleFS
  image; do not ship the unused System Super shell resources.
- [ ] Define shared JSON design tokens for the circular 466 x 466 display and
  320 x 320 safe area.
- [ ] Complete hardware validation of the QSee-owned Bluetooth, GPS, and CPU
  status bar under the single-app System Core runtime.
- [ ] Replace the removed Shell keyboard and message-dialog services with
  QSee-owned declarative components before exposing log search or destructive
  confirmation actions on the System Core-only build.
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
- [x] Arm `qsee_storage_request_format()` only after an explicit destructive
  confirmation is accepted, bind that one-shot permission to the
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
- [x] Keep both CPU bars in a visible `MEASURING...` state until the first valid
  previous-sample delta exists; never invent a utilization value.
- [x] Keep hardware refresh non-blocking and outside the UI thread.

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
- [x] Keep stationary calibration on a non-blocking platform worker.
- [ ] Wire a Settings calibration action on top of the worker when the UI work
  lands.

## Power and performance profiles

- [x] Validate the AXP2101 PKEY standby/wake cycle, independent arc timeouts,
  exact 50% dim stage, and real AMOLED panel-off transition on the physical
  device.
- [ ] Validate qualified IMU wake from full standby on the physical device.
- [x] Add the backend display-power API with transient `Active`, `Dimmed`, and
  `DisplayOff` transitions, preserving the user's configured brightness and
  publishing both configured and applied values in `DeviceSnapshotService`.
- [x] Add an explicit UI/display power mode flow with `Active`, `Dimmed`, and
  `DisplayOff` states tied to real inactivity/sleep transitions.
- [x] When the main sleep flow dims the display, reduce AMOLED brightness to 50% of
  the user's configured value rather than a fixed percentage.
- [x] While dimmed, slow non-critical UI refresh work from the normal 1 second
  cadence to about 5 seconds where the current screen allows it.
- [x] When the display is fully off, stop GUI refresh timers entirely and resume
  them only after wake.
- [x] Keep event-driven platform state, critical logs, and fail-fast errors
  truthful; do not keep periodic UI timers alive just to manufacture stale or
  placeholder values.

## Deferred HTTP API activation

- [x] Persist and validate `ApiKey` and `http_enabled` through the non-UI
  backend; keep the server opt-in and prevent key updates from changing its
  lifecycle implicitly.
- [ ] Add an explicit System Core Settings action for the read-only HTTP API;
  do not start the server automatically during boot or invent missing settings.

## Audio scope visualization

- [x] Add the "FFT scope" action to the Brookesia JSON/System Core audio page.
- [ ] Redesign FFT bar layout: reduce margins, apply color gradients instead of flat colors for better visual appeal.
- [ ] ? Investigate microphone gain/volume control: should we apply amplification via hardware (pre-processing) or in software (post-processing)?

## Open-source publication prep

- [x] Scan the non-UI release surface for proprietary identifiers and internal
  contact/footer signatures with `scripts/verify_public_branding.py`.
- [x] Remove or generalize internal/legal notice text from non-UI scripts and
  architecture/release docs.
- [ ] Complete the branding and legal sweep for UI files and startup assets
  when UI work is authorized.
- [x] Add a pre-release checklist that includes branding sweep, placeholder
  replacements, and export-safe defaults via `docs/release/open-source-pre-release-checklist.md`.
