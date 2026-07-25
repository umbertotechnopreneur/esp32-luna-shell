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

Full hardware refreshes remain explicit. One existing monitor task performs
only the bounded periodic work requested by an active diagnostic page, the
Welcome metrics, or cardinal auto-rotation. Auto-rotation samples raw gravity
at 100 ms without creating a second task; all consumers still read the
published snapshot rather than a driver directly.

## Snapshot content

| Group | Values |
| --- | --- |
| Power | battery/USB presence, charge state, percentage, rails, PMU temperature |
| RTC | date/time and clock-integrity flag |
| IMU / sensor input | filtered acceleration and angular velocity, sensor temperature, calibration state/biases, availability, and `sampled_at_us` |
| Cardinal orientation | persisted auto-rotation state, gravity validity, physical orientation, applied rendering rotation, pending state, and change timestamps |
| GPS | persisted enable state, pending NMEA byte count, parsed GGA/RMC fix data, availability, and `sampled_at_us` |
| Audio | speaker health, microphone health, mean and peak input level |
| UI/storage/radios | display, touch, SD/logger mirror, Wi-Fi, BLE |
| Memory | generic free/minimum heap plus internal and PSRAM heap total/used/free/percentage/largest block; physical flash and running firmware image usage |
| Processor | native core count plus sampled task name, priority, stack watermark, and per-core utilization |

Each component has `Unknown`, `Ready`, `Degraded`, or `Missing` health, an
explicit `available` flag, and the last `esp_err_t`. BLE provisioning and
telemetry advertising are represented as separate states rather than
collapsing provisioning to `Unknown`; provisioning does not claim an
advertising state that its API does not expose.

The radio snapshot also carries the normal BLE enable preference. Wi-Fi scan
state and up to 16 bounded results live in `WifiSnapshot`; UI consumers issue
`qsee_wifi_start_scan()` and then read `scanning`, `scan_completed`,
`scan_sequence`, `network_count`, and `networks` only from
`DeviceSnapshotService`.

CPU utilization is available only after two runtime samples. With
`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` enabled, each core is calculated as
`100 - (idle_runtime_delta * 100 / runtime_counter_delta)`. The deltas are
taken from the previous snapshot, are clamped to 0..100, and are not smoothed.
If the runtime-stat configuration is unavailable or a counter wraps, the
corresponding `utilization_available` flag remains false.
The tracked `sdkconfig.defaults` enables the required options for the next
configuration pass; an already-generated `sdkconfig`/build cache must be
reconfigured before those counters exist.

Internal memory uses `heap_caps_get_total_size/free_size` with
`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`; PSRAM uses the same APIs with
`MALLOC_CAP_SPIRAM`. Used bytes are `total - free`, never a free-heap estimate.
The firmware percentage is image bytes from `esp_image_get_metadata()` over
the size of the running app partition. It is not a percentage of generic
storage; physical flash capacity is reported separately by
`esp_flash_get_size()`.

The current checkpoint has no separate orientation-fusion provider. A guarded
raw-gravity classifier supplies only stable `0/90/180/270` physical orientation
for the CO5300 display; it does not claim pitch, roll, yaw, heading, or fusion
quality. The canonical diagnostic IMU path continues to expose the real
filtered/calibrated QMI8658 values.

The LC76G bridge is command-gated: address `0x54` is read only after a command
to `0x50`. Backlogs are drained in fixed 4096-byte chunks into static storage,
so a large valid NMEA queue cannot trigger an unbounded allocation.

## Native system APIs

The implementation deliberately uses the SDK instead of estimating system
state:

- `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`;
- `heap_caps_get_total_size()`, `heap_caps_get_free_size()`, and
  `heap_caps_get_largest_free_block()`;
- `esp_chip_info()` and `esp_cpu_get_core_id()`;
- `ulTaskGetIdleRunTimeCounterForCore()` and the FreeRTOS runtime counter;
- `xTaskGetCurrentTaskHandleForCore()`, `pcTaskGetName()`,
  `uxTaskPriorityGet()`, and `uxTaskGetStackHighWaterMark()`.

Snapshot logs include a sequence number and reason so a runtime trace can show
exactly when the state changed.
