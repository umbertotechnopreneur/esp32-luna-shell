# QSee hardware API contract

`components/qsee_platform` is the reusable hardware boundary for the
Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS variant. It has no dependency on an
application screen or navigation route.

## Ownership

- The Waveshare BSP owns display, touch, shared I2C, SDMMC pins, I2S, and board
  GPIO definitions.
- QSee platform services reuse those BSP handles and never install competing
  bus drivers.
- The canonical logger starts before platform initialization and receives every
  lifecycle transition and error.
- Drivers own mutable peripheral handles. Consumers read immutable copies from
  `qsee::DeviceSnapshotService`.

## Lifecycle and failure semantics

`qsee_hardware_init()` is idempotent. Optional hardware is fail-open: one
missing or degraded peripheral does not abort the rest of initialization.
`qsee_hardware_refresh(reason)` is synchronous, uses static buffers, and
serializes concurrent callers with a five-second bound.

Subsystems with their own event loops publish state through `report*()`.
Wi-Fi and BLE therefore update the canonical snapshot from native ESP-IDF
events instead of being polled.

## Public operations

| API | Contract |
| --- | --- |
| `qsee_hardware_init` | Initialize PMU, RTC, QMI8658, LC76G, and delayed microphone sampling |
| `qsee_hardware_refresh` | Take one coherent explicit snapshot |
| `qsee_imu_calibrate` | Accept only a stationary 96-sample set and retain the previous calibration on rejection |
| `qsee_rtc_read` / `qsee_rtc_write` | Read or write UTC calendar time |
| `qsee_audio_*` | Serialize one shared ES8311/ES7210/I2S route |
| `qsee_storage_*` | Mount/retry asynchronously; format only after an explicit API call |
| `qsee_wifi_*` | Event-driven ESP-IDF station lifecycle and asynchronous scans |
| `qsee_provisioning_*` | Security 1 BLE provisioning with explicit lifecycle state |
| `qsee_display_*` | Persist brightness/auto-rotation, apply synchronized CO5300+CST9217 cardinal rotation, run non-persistent fades, and control CO5300 display-on/off |
| `qsee_power_*` | Native PM policy and input inventory; persist staged dim/display-off timers and coordinate BOOT wake for light/deep sleep |

The reusable fade path keeps persisted brightness separate from the actual
`0..100` panel output. Intermediate fade steps never write NVS. The inactivity
policy dims to 50% of that preference and later sends CO5300 display-off;
touch or BOOT activity restores the preference. Light sleep pauses the LVGL
adapter, fades to black, uses GPIO0 as its digital wake source, restores the
preferred brightness, then resumes rendering. The
BOOT wake press is consumed until release so it cannot immediately put the
device back to sleep.

Deep sleep uses the same BOOT RTC-domain input but restarts the firmware after
wake, while light sleep resumes the retained runtime.

## Bounded behavior

- LC76G reads at most 4096 bytes per refresh and leaves any remaining backlog
  for the next explicit refresh.
- NMEA payload and microphone capture storage are static.
- Audio operations share one mutex and wait up to five seconds for the active
  playback or capture to finish.
- Snapshot copies and updates use a static FreeRTOS mutex.
- Internal RAM pressure is reported when free internal memory falls below
  32 KiB or its largest block falls below 16 KiB.
- SD retries are delayed and never block application startup.

## Non-negotiable storage rule

No initialization, retry, or mount failure may format a card. Only
`qsee_storage_request_format()` can arm one explicit format attempt, and the
caller is responsible for obtaining user authorization first.
