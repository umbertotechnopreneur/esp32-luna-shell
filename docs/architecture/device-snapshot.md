# Canonical device snapshot

`qsee::DeviceSnapshotService` is the application boundary between physical
drivers and consumers such as diagnostics, UI, telemetry, and future sensor
fusion.

## Contract

- `initialize()` configures the supported peripherals against the shared
  Waveshare BSP resources.
- `refresh(reason)` samples all initialized devices and system metrics into one
  coherent point-in-time value. Concurrent refreshes are serialized with a
  bounded wait.
- `snapshot()` returns a copy protected by a static FreeRTOS mutex.
- `report*()` methods let subsystem owners publish state that cannot be sampled
  by the service directly, including display, touch, SD, speaker, Wi-Fi, and
  BLE.

No periodic sensor loop exists. A caller requests a refresh when a coherent
update is needed. This prevents hidden bus traffic and gives future sensor
fusion an explicit sample boundary.

## Snapshot content

| Group | Values |
| --- | --- |
| Power | battery/USB presence, charge state, percentage, rails, PMU temperature |
| RTC | date/time and clock-integrity flag |
| IMU | acceleration, angular velocity, sensor temperature, calibration quality and biases |
| GPS | pending NMEA byte count, parsed GGA/RMC fix data, and first bounded sentence |
| Audio | speaker health, microphone health, mean and peak input level |
| UI/storage/radios | display, touch, SD/logger mirror, Wi-Fi, BLE |
| Memory | free/minimum heap, free/largest internal block, pressure flag, free/largest PSRAM block |
| Processor | native core count plus sampled task name, priority, and stack watermark |

Each component has `Unknown`, `Ready`, `Degraded`, or `Missing` health and the
last `esp_err_t`. BLE provisioning and telemetry advertising are represented
as separate states rather than collapsing provisioning to `Unknown`.

The LC76G bridge is command-gated: address `0x54` is read only after a command
to `0x50`. Backlogs are drained in fixed 4096-byte chunks into static storage,
so a large valid NMEA queue cannot trigger an unbounded allocation.

## Native system APIs

The implementation deliberately uses the SDK instead of estimating system
state:

- `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`;
- `heap_caps_get_free_size()` and `heap_caps_get_largest_free_block()`;
- `esp_chip_info()` and `esp_cpu_get_core_id()`;
- `xTaskGetCurrentTaskHandleForCore()`, `pcTaskGetName()`,
  `uxTaskPriorityGet()`, and `uxTaskGetStackHighWaterMark()`.

Snapshot logs include a sequence number and reason so a runtime trace can show
exactly when the state changed.
