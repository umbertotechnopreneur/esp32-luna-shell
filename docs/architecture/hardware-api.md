# Hardware API contract

`components/LUNASHELL_platform` is the reusable hardware boundary for the
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
  `LUNASHELL::DeviceSnapshotService`.

## Lifecycle and failure semantics

`LUNASHELL_hardware_init()` is idempotent. It initializes PMU, RTC, QMI8658, LC76G,
and the audio path in that order and returns the first failed stage; later
stages are not probed after that failure. `app_main()` explicitly treats the
hardware service, time, power inputs, boot audio, removable storage, and pre-UI
refresh as optional startup stages, logs their exact failures, and continues
to the UI. Logger, display, boot UI, persisted brightness, and UI handoff are
required. Ambiguous recovery is never attempted.

`LUNASHELL_hardware_refresh(reason)` is synchronous, uses static buffers, serializes
concurrent callers with a five-second bound, and publishes the completed
refresh as one snapshot generation. Explicit boot, manual, BLE-command, and
headless calls always read available hardware. Only the periodic Full worker
may supply a non-zero Diagnostic reuse window.

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
| `LUNASHELL_hardware_init` | Initialize PMU, RTC, QMI8658, LC76G, and the shared audio path; take the bounded initial microphone sample |
| `LUNASHELL_hardware_refresh` | Stage and atomically publish one explicit full-device generation |
| `LUNASHELL_hardware_take_power_key_events` | Under the dedicated PMU mutex, use bounded IDF I2C transactions to read and decode AXP2101 IRQ status, queue PKEY events before a verified clear, then atomically deliver the queued short/long counts without depending on a Full Snapshot or display operation |
| `LUNASHELL_hardware_set_diagnostic_streams` | Independently enable measured IMU and microphone streams for the active live page; disabling one stream cannot leave it implicitly sampled by the other |
| `LUNASHELL_imu_calibrate` | Schedule a stationary 96-sample calibration on the platform worker; keep the previous calibration on rejection or while one is already pending |
| `LUNASHELL_rtc_read` / `LUNASHELL_rtc_write` | Read or write UTC calendar time |
| `LUNASHELL_audio_*` | Serialize one shared ES8311/ES7210/I2S route |
| `LUNASHELL_storage_*` | Mount asynchronously; retry only explicit transient failures; format only after an explicit API call |
| `LUNASHELL_wifi_*` | Event-driven ESP-IDF station lifecycle and asynchronous scans |
| `LUNASHELL_provisioning_*` | Security 1 BLE provisioning with explicit lifecycle state |
| `LUNASHELL_power_*` / `LUNASHELL_display_set_power_mode` | Native inactivity policy, guarded BOOT Fullscreen/reset input, PKEY input inventory, transient Active/Dimmed/DisplayOff transitions, software Device Standby, and separately callable explicit SoC sleep entry |

## Bounded behavior

- LC76G reads at most 4096 bytes per refresh and leaves any remaining backlog
  for the next explicit refresh.
- NMEA payload and microphone capture storage are static.
- The opt-in IMU and microphone Diagnostic streams have separate enable state.
  Audio pages never request an otherwise unused IMU sample, and the IMU page
  never requests microphone ownership.
- Disabling microphone diagnostics is a quiescence barrier: stream selection
  changes under the refresh mutex and the monitor rechecks it after acquiring
  that mutex, before the voice AFE may reclaim capture.
- A successful Diagnostic commit records the measured component timestamp.
  The next periodic Full generation may consume that exact timestamp once when
  it remains younger than both cadences. Reuse does not advance the component
  timestamp. A failed/stale sample, source mismatch, clock regression, failed
  commit, or second consumption forces a physical read.
- Audio operations share one mutex and wait up to five seconds for the active
  playback or capture to finish.
- Snapshot copies and updates use a static FreeRTOS mutex.
- Internal RAM pressure is reported when free internal memory falls below
  32 KiB or its largest block falls below 16 KiB. Raw allocator telemetry also
  reports internal/PSRAM total, free, lifetime minimum, largest block, and
  allocated/free/total block counts.
- Allocation failures are counted from before logger/display startup. The
  global ESP-IDF callback is IRAM-only and updates lock-free 32-bit aggregates;
  it never logs, locks, allocates, formats strings, or reads a clock.
- Transient SD retries are delayed and never block application startup.
  `format-required` pauses until explicit authorization; internal, destructive,
  poisoned, or incompletely cleaned failures are terminal for the current
  boot. A mounted card is checked periodically and invalidated/unmounted when
  status fails.

## Non-negotiable storage rule

No initialization, retry, or generic mount failure may format a card. Format is
eligible only after card initialization and an exact `FR_NO_FILESYSTEM` result.
Only `LUNASHELL_storage_request_format()` can arm one explicit attempt after user
authorization. The permission is consumed by the immediate probe and is bound
to all observed CID fields plus capacity; a mismatch, missing card, transport
failure, or inapplicable request cannot carry it forward.

Because this board has no card-detect pin, a failed initialization is
`card/transport unconfirmed`, not proof that the card is physically absent.
