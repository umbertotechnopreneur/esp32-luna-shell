# Canonical device snapshot

`qsee::DeviceSnapshotService` is the application boundary between physical
drivers and consumers such as diagnostics, UI, telemetry, and future sensor
fusion.

## Contract

- `initialize()` configures the supported peripherals against the shared
  Waveshare BSP resources in a fixed order and returns the first failed stage.
- `refresh(reason)` stages all initialized-device and system samples, then
  commits them as one generation. Concurrent refreshes are serialized with a
  bounded wait.
- `snapshot()` returns a copy protected by a static FreeRTOS mutex.
- `report*()` methods let subsystem owners publish state that cannot be sampled
  by the service directly, including display, SD, speaker, Wi-Fi, and BLE.
  Each report is a separate committed generation. Failure to publish a
  generation is a fatal synchronization invariant, never a log-only event.
- Physical touch activity uses a lock-free monotonic sequence and timestamp.
  `snapshot()` folds that latch into each returned immutable copy, so the LVGL
  input callback never waits for a hardware refresh or reads the touch driver.
- `PowerManagementSnapshot` is the canonical active/standby and inactivity
  state. It includes the transition reason, audio suspension, both arc
  visibility flags, display idle stage, persisted timeouts, last activity, and
  a monotonic transition sequence.

The full-device refresh has one explicit serialized entry point. A dedicated
platform worker invokes that entry point at the persisted user cadence: 300 ms,
500 ms, 1 Hz, 2 s, 3 s, 5 s, or 10 s. The worker never overlaps two full
refreshes and publishes the real acquisition result.

The Full Snapshot worker is independent from the Diagnostic Stream. When the
diagnostic monitor is enabled, it samples only the IMU and microphone at its
own persisted cadence for live pages. Native Wi-Fi/BLE events and mounted-SD
status polling may publish their own generations independently. UI refresh
binds the latest immutable snapshot at 1 Hz while active, 0.2 Hz while dimmed,
and stops while the display is off; it never initiates hardware sampling.

## Snapshot content

| Group | Values |
| --- | --- |
| Power | battery/USB presence, charge state, percentage, rails, PMU temperature |
| RTC | date/time and clock-integrity flag |
| IMU | acceleration, angular velocity, sensor temperature, calibration quality and biases |
| Sensor fusion | gravity-dominant motion state, stationary gate, acceleration magnitude, and angular speed; no unvalidated pitch/roll/yaw |
| GPS | pending NMEA byte count, parsed GGA/RMC fix data, and first bounded sentence |
| Audio | speaker health, microphone health, mean and peak input level |
| Diagnostics | Full Snapshot cadence/worker state, Diagnostic Stream cadence/state, and IMU smoothing |
| Power management | active/standby transition, PWR/BOOT/IMU/touch reason, audio suspension, Status Arc and Command Arc visibility, display idle stage, and all inactivity timeouts |
| UI/storage/radios | display, touch health, monotonic touch-activity sequence/timestamp, display power mode, applied/configured brightness, SD/logger mirror and card/transport state, Wi-Fi, BLE |
| Memory | free/minimum heap, free/largest internal block, pressure flag, free/largest PSRAM block |
| Processor | native core count plus sampled task name, priority, and stack watermark |

Each component has `Unknown`, `Ready`, `Degraded`, or `Missing` health and the
last `esp_err_t`. BLE provisioning and telemetry advertising are represented
as separate states rather than collapsing provisioning to `Unknown`.

Numeric telemetry is consumable only when health is `Ready`, the last error is
`ESP_OK`, data is valid, and its freshness TTL has not expired. Every component
records its last attempt and last successful sample. TTL expiry marks an older
sample `STALE`; the timestamp remains diagnostic evidence, but consumers do not
render its numeric value.

The SD board wiring has no card-detect pin. A failed SDMMC probe therefore
publishes `card/transport unconfirmed`, not physical absence. `format-required`
is possible only after card initialization and an exact FatFs
`FR_NO_FILESYSTEM` result. Poisoned or incomplete cleanup is published
separately with its cleanup stage and error and is terminal for the current
boot.

The LC76G bridge is command-gated: address `0x54` is read only after a command
to `0x50`. Backlogs are drained in fixed 4096-byte chunks into static storage,
so a large valid NMEA queue cannot trigger an unbounded allocation.
A published fix requires coherent valid GGA quality and RMC active status in
the same payload. A malformed supported sentence, a missing counterpart, or
contradictory fix states rejects the complete sample; no partial coordinates
or zero-filled fix fields are published.

## Native system APIs

The implementation deliberately uses the SDK instead of estimating system
state:

- `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`;
- `heap_caps_get_free_size()` and `heap_caps_get_largest_free_block()`;
- `esp_chip_info()` and `esp_cpu_get_core_id()`;
- `xTaskGetCurrentTaskHandleForCore()`, `pcTaskGetName()`,
  `uxTaskPriorityGet()`, and `uxTaskGetStackHighWaterMark()`.

Snapshot logs include a sequence number and reason. The sequence advances once
per committed full refresh, diagnostic transaction, or asynchronous subsystem
report, so a runtime trace can identify the published generation that changed.

The Power Management Service is the only inactivity decision maker. Its one
worker debounces BOOT and PWR, consumes lock-free touch activity, evaluates IMU
sample deltas, controls the shared display fade, and pauses or resumes the
voice AFE. PWR short press toggles standby; BOOT held for three seconds requests
`esp_restart()`. The Brookesia timer only synchronizes the published power
snapshot into GUI animations and never owns power-policy timing.

## Verification

`scripts/verify_snapshot_integrity.py` runs deterministic, hardware-free checks
for transaction visibility, TTL expiry, and recovery after a later successful
sample.
