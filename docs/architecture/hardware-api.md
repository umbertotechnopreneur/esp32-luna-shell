# Hardware API contract

`components/qsee_platform` is the reusable hardware boundary for the
Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS variant. It has no dependency on an
application screen or navigation route.

## Ownership

- The Waveshare BSP owns display, touch, shared I2C, SDMMC pins, I2S, and board
  GPIO definitions.
- Platform services reuse those BSP handles and never install competing
  bus drivers.
- The canonical logger starts before platform initialization and receives every
  lifecycle transition and error.
- Drivers own mutable peripheral handles. Consumers read immutable copies from
  `qsee::DeviceSnapshotService`.

## Lifecycle and failure semantics

`qsee_hardware_init()` is idempotent. It initializes PMU, RTC, QMI8658, LC76G,
and the audio path in that order and returns the first failed stage; later
stages are not probed after that failure. `app_main()` explicitly treats the
hardware service, time, power inputs, boot audio, removable storage, and pre-UI
refresh as optional startup stages, logs their exact failures, and continues
to the UI. Logger, display, boot UI, persisted brightness, and UI handoff are
required. Ambiguous recovery is never attempted.

`qsee_hardware_refresh(reason)` is synchronous, uses static buffers, serializes
concurrent callers with a five-second bound, and publishes the completed
refresh as one snapshot generation.

Sensor fusion consumes only current filtered IMU samples and publishes a
bounded gravity/motion state through `DeviceSnapshotService`. It deliberately
does not expose pitch, roll, or yaw without a validated reference frame.

A component publishes numeric telemetry only after a successful sample. Failed
reads publish invalid health plus the original error; consumers must render
`UNAVAILABLE`, `NO FIX`, or `STALE` rather than zero-filled or previous values.
An otherwise valid sample is also marked `STALE` after its component freshness
TTL expires.

Subsystems with their own event loops publish state through `report*()`.
Wi-Fi and BLE therefore update the canonical snapshot from native ESP-IDF
events instead of being polled. BLE telemetry serializes an unavailable or
expired battery sample as `null`; it does not expose a one-time cached Battery
Service value.

## Public operations

| API | Contract |
| --- | --- |
| `qsee_hardware_init` | Initialize PMU, RTC, QMI8658, LC76G, and the shared audio path; take the bounded initial microphone sample |
| `qsee_hardware_refresh` | Stage and atomically publish one explicit full-device generation |
| `qsee_imu_calibrate` | Schedule a stationary 96-sample calibration on the platform worker; keep the previous calibration on rejection or while one is already pending |
| `qsee_rtc_read` / `qsee_rtc_write` | Read or write UTC calendar time |
| `qsee_audio_*` | Serialize one shared ES8311/ES7210/I2S route |
| `qsee_storage_*` | Mount asynchronously; retry only explicit transient failures; format only after an explicit API call |
| `qsee_wifi_*` | Event-driven ESP-IDF station lifecycle and asynchronous scans |
| `qsee_provisioning_*` | Security 1 BLE provisioning with explicit lifecycle state |
| `qsee_power_*` / `qsee_display_set_power_mode` | Native PM policy, input inventory, transient Active/Dimmed/DisplayOff transitions, and explicit sleep entry |

## Bounded behavior

- LC76G reads at most 4096 bytes per refresh and leaves any remaining backlog
  for the next explicit refresh.
- NMEA payload and microphone capture storage are static.
- The opt-in diagnostic monitor periodically samples only IMU and microphone;
  the full-device refresh remains explicit.
- Audio operations share one mutex and wait up to five seconds for the active
  playback or capture to finish.
- Snapshot copies and updates use a static FreeRTOS mutex.
- Internal RAM pressure is reported when free internal memory falls below
  32 KiB or its largest block falls below 16 KiB.
- Transient SD retries are delayed and never block application startup.
  `format-required` pauses until explicit authorization; internal, destructive,
  poisoned, or incompletely cleaned failures are terminal for the current
  boot. A mounted card is checked periodically and invalidated/unmounted when
  status fails.

## Non-negotiable storage rule

No initialization, retry, or generic mount failure may format a card. Format is
eligible only after card initialization and an exact `FR_NO_FILESYSTEM` result.
Only `qsee_storage_request_format()` can arm one explicit attempt after user
authorization. The permission is consumed by the immediate probe and is bound
to all observed CID fields plus capacity; a mismatch, missing card, transport
failure, or inapplicable request cannot carry it forward.

Because this board has no card-detect pin, a failed initialization is
`card/transport unconfirmed`, not proof that the card is physically absent.
