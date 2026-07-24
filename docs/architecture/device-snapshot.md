# Canonical device snapshot

`qsee::DeviceSnapshotService` is the application boundary between physical
drivers and consumers such as diagnostics, UI, telemetry, and future sensor
fusion.

## Contract

- `initialize()` configures the supported peripherals against the shared
  Waveshare BSP resources.
- `refresh(reason)` samples all initialized devices and system metrics into one
  coherent point-in-time value.
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
| IMU | acceleration, angular velocity, sensor temperature |
| GPS | pending NMEA byte count and first bounded sentence |
| Audio | speaker health, microphone health, mean and peak input level |
| UI/storage/radios | display, touch, SD/logger mirror, Wi-Fi, BLE |
| Memory | free/minimum heap, free/largest internal block, free/largest PSRAM block |
| Processor | native core count plus sampled task name, priority, and stack watermark |

Each component has `Unknown`, `Ready`, `Degraded`, or `Missing` health and the
last `esp_err_t`. A transport probe can therefore be ready while a later sample
is degraded, as currently observed with the LC76G NMEA transaction.

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
