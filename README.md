# QSee.ai round-device firmware

Firmware and Windows simulator for the QSee.ai application on the GPS variant
of the Waveshare ESP32-S3-Touch-AMOLED-1.75.

The device application uses the official Espressif ESP-Brookesia 0.8 stack:
System Super owns the system shell and services, System Core owns the native
application lifecycle, and GUI LVGL renders the declarative JSON interface.
LVGL 9 is the rendering backend, not an application-level UI API. The official
ESP-Brookesia monorepo is tracked as the `third_party/esp-brookesia` submodule,
pinned to commit `5361118b9953800e4fe710e57e5314fccec9cbcf`.

The official Waveshare board support package (BSP) remains the owner of the
display, touch panel, audio codecs, shared I2C bus, and board-specific pins.

## Product and UI contract

The AMOLED is a circular 466 x 466 display. QSee.ai defines a 320 x 320 safe
content square at `(73, 73)`. All information and controls required to finish a
screen must fit in that square. The curved perimeter is reserved for stable,
easy-to-learn shell actions:

| Edge | Default action |
| --- | --- |
| Left | Back or Cancel |
| Right | Next, Confirm, or OK |
| Top | Close, menu, or status |
| Bottom | Contextual primary action |

The QSee.ai branded boot console is the first application frame. It shows the
same bounded logger stream sent to serial and, when available, the SD log. See
[round-display-guidelines.md](docs/ui/round-display-guidelines.md) for the
complete geometry.

## Canonical startup order

1. install the canonical logger;
2. start display and touch through the BSP;
3. show the QSee.ai boot console and live logger messages;
4. mount the internal `littlefs_data` resource image at `/littlefs`, without
   formatting on mount failure;
5. initialize PMU, RTC, IMU, GPS, and the delayed ES7210 microphone worker;
6. play the boot SFX through the ES8311 speaker path;
7. start non-blocking SD detection and mount retries without automatic
   formatting;
8. initialize Wi-Fi and BLE, then request the boot Wi-Fi scan;
9. create an explicit hardware snapshot and log memory/core state;
10. hide the boot console, start Brookesia System Super, and launch the
    registered QSee.ai System Core application.

Every optional device is fail-open: a missing or degraded sensor is visible in
the log and snapshot but does not make the application reboot.

## Canonical logger

Serial output is always authoritative. The logger also keeps a bounded RAM
ring for the boot console and mirrors queued lines to
`/sdcard/qsee/logs/qsee.log` after a successful mount. The SD writer rotates at
2 MB to `qsee.log.1`. Logger formatting and snapshots use static bounded
storage so logging cannot exhaust the `esp_timer` task stack.

See [startup-and-logger.md](docs/architecture/startup-and-logger.md).

## Hardware snapshot

`qsee::DeviceSnapshotService` is the single coherent view of:

- battery, USB power, AXP2101 charge policy, ADC telemetry, and latched events;
- PCF85063 RTC;
- QMI8658 accelerometer, gyroscope, and temperature;
- LC76G transport plus parsed GGA/RMC fix, position, altitude, speed, course,
  satellite count, and HDOP;
- ES8311 speaker and ES7210 microphone levels;
- SD card capacity, filesystem free space, and logger mirroring;
- display, touch, event-driven Wi-Fi lifecycle/scan/IP state, and BLE health;
- physical flash capacity, running firmware image size, and app partition size;
- free heap, minimum free heap, internal RAM, and PSRAM
  capacity/total/used/free/largest-block values;
- ESP32 core count and the current task, priority, and stack watermark sampled
  for each core.

The service refreshes only on explicit request. Every QSee.ai UI telemetry
view consumes this immutable snapshot instead of polling drivers
independently. The log viewer reads the canonical logger ring, never a hardware
driver. The memory and processor readings use native ESP-IDF and FreeRTOS APIs.
See
[device-snapshot.md](docs/architecture/device-snapshot.md) and
[hardware-api.md](docs/architecture/hardware-api.md).

## Navigation

The QSee.ai native app implements the System Core `IApp` lifecycle. GUI LVGL
loads `components/qsee_app/package/res/root.json`, while its `qsee_content`
`screenFlow` maps named actions to screens. `qsee_app.cpp` subscribes to those
actions, refreshes the active screen from `DeviceSnapshotService`, and uses
System Super services for the log-search keyboard and the destructive SD
confirmation dialog.

The JSON document owns view structure, layout, styles, and action names. C++
owns lifecycle and data binding. App code does not construct a second widget
framework or call LVGL directly.

### Route index

The route registry lives in
[`components/qsee_app/package/res/root.json`](components/qsee_app/package/res/root.json);
action handling lives in
[`components/qsee_app/qsee_app.cpp`](components/qsee_app/qsee_app.cpp). This is
the current code-grounded inventory:

| Route | Status | Entry point | Purpose |
| --- | --- | --- | --- |
| `Welcome` | Implemented | Initial app screen | Device console home with memory, storage, CPU, radio status, and links to diagnostics |
| `Logs` | Implemented | `LIVE LOGS` | Searchable, filterable bounded RAM log with pause, clear, and follow controls |
| `Hardware` | Implemented | `HARDWARE` or storage card | Component health inventory, per-component details, and SD action dialog |
| `Audio` | Implemented, diagnostic | `AUDIO` | Microphone channel levels and PCM capture status; this is not yet a waveform/spectrogram view |
| `Imu` | Implemented, diagnostic | `IMU LEVEL` | Accelerometer, gyroscope, temperature, and calibration status |
| `About` | Implemented | `ABOUT` | Device identity, branding, and contact information |
| `Settings` | Planned | Not currently exposed | User and device settings page |
| `AudioSpectrogram` | Planned | Not currently exposed | Test-audio spectrogram page; separate from the current audio-level diagnostics |
| `ImuCalibration` | Planned | Not currently exposed | Dedicated calibration workflow and live IMU state page; the current `Imu` route only displays status |

The index is part of the screen-flow contract. Whenever a route is created,
renamed, removed, or its entry point changes, update this table in the same
change as the JSON screen, flow transition, C++ action subscription, refresh
logic, and entry button.

## Hardware

The target includes:

- ESP32-S3R8, 16 MB flash, and 8 MB octal PSRAM;
- 466 x 466 CO5300 AMOLED and CST9217 capacitive touch;
- AXP2101 PMU, PCF85063 RTC, and QMI8658 IMU;
- LC76G GNSS with external ceramic antenna;
- user-added 8 GB microSD;
- ES8311 speaker codec/amplifier and ES7210 dual-microphone ADC;
- Wi-Fi and Bluetooth LE through NimBLE.

The complete pin and integration inventory is in
[hardware-inventory.md](docs/hardware/hardware-inventory.md) and its
machine-readable counterpart is [inventory.json](hardware/inventory.json).

The SD card must be prepared as FAT32 with an MBR partition. Firmware never
formats it implicitly. An SDMMC OCR timeout occurs before filesystem parsing
and therefore is not proof that formatting alone will fix the card.

The PMU integration uses XPowersLib on the BSP's shared ESP-IDF I2C master bus,
matching the Waveshare AXP2101 example. Charging is configured for 50 mA
precharge, 400 mA constant current, 25 mA termination, and a 4.20 V target.
TS-pin measurement is disabled because this board does not expose a battery
NTC on that input.

The audio layer owns one shared ES8311/ES7210/I2S route. Both boot playback and
microphone snapshots use that layer, which serializes format changes and never
creates a competing I2S driver. Successful PCM submission is logged separately
from physical audibility.

## Firmware toolchain

This checkout is configured for ESP-IDF 6.0.2 on Windows. PowerShell 7, Python,
FFmpeg, and `winget` are available locally. Canonical configuration belongs in
`sdkconfig.defaults`; the generated `sdkconfig` is intentionally ignored.
See [the ESP-IDF 6 migration record](docs/esp-idf-6-migration.md) for the
dependency changes, validation boundary, and known limits.

Initialize the official ESP-Brookesia source after cloning:

```powershell
git submodule update --init --recursive
```

Every ESP-IDF configure, build, size, flash, erase, and monitor action must go
through `scripts/device.ps1`. Do not bypass this project entrypoint.

Select the target and generate a clean configuration:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 set-target -BuildDir build-idf6
pwsh -NoProfile -File .\scripts\device.ps1 reconfigure -BuildDir build-idf6
```

Build:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-idf6
```

Show the application and static memory size report:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 size -BuildDir build-idf6
```

Flash the currently dedicated device:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 flash -Port COM12 -BuildDir build-idf6 -Baud 921600
```

Monitor it:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 monitor -Port COM12 -BuildDir build-idf6
```

Capture a bounded diagnostic log:

```powershell
pwsh -NoProfile -File .\scripts\capture-serial.ps1 -Port COM12 -DurationSeconds 30 -OutputPath .\runtime-logs\device.log
```

Always pass the port explicitly before flashing. The device helper terminates
stale monitor processes scoped to that port and fails the command if flashing
or erasing fails. Build success is not hardware proof; keep serial evidence
separate from compiler evidence.

## Windows simulator

The Windows build uses deterministic synthetic hardware data, so navigation,
boot status, and failure paths can be debugged without the physical device.

```powershell
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted
pwsh -NoProfile -File .\simulator\windows\build-exe.ps1
```

See [simulator/windows/README.md](simulator/windows/README.md) for controls and
available scenarios.

## Canonical assets

Brand, display, and original SFX inputs live under `assets/`. Regenerate all
device derivatives with:

```powershell
pwsh -NoProfile -File .\scripts\generate-device-assets.ps1
```

Generated outputs include the 466 x 466 PNG, a headerless little-endian RGB565
frame, a 22.05 kHz mono PCM16 boot sound, and SHA-256 manifests. Never modify
the preserved source assets to create a derivative.

## Source ownership banner

Owned C and C++ sources use the canonical QSee.ai copyright/contact banner.
Preview or apply it with:

```powershell
pwsh -NoProfile -File .\scripts\add-source-banner.ps1
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Apply
```

The script is idempotent, supports `-Remove -Apply`, and intentionally excludes
the Waveshare BSP, ESP-Brookesia, managed components, build output, generated
files, and other third-party code. Contact: `hello@qsee.ai`.

## Repository map

| Path | Responsibility |
| --- | --- |
| `main/` | Ordered application startup |
| `components/qsee_logger/` | Serial, RAM, boot-console, and SD logging |
| `components/qsee_boot/` | Branded boot UI and ES8311 playback |
| `components/qsee_platform/` | Hardware drivers, radios, and snapshots |
| `components/qsee_app/` | Native System Core app, lifecycle/data binding, and JSON screen package |
| `components/esp32_s3_touch_amoled_1_75/` | Local Waveshare BSP |
| `third_party/esp-brookesia/` | Official pinned ESP-Brookesia monorepo submodule |
| `assets/` | Canonical branding and SFX inputs/derivatives |
| `simulator/windows/` | Synthetic Windows debug build |
| `scripts/` | Build, flash, serial capture, and asset generation |

## Rules for humans and AI agents

- Preserve logger-first startup; all device state must flow through it.
- Run every ESP-IDF workflow through `scripts/device.ps1`.
- Keep System Super, System Core, and GUI LVGL JSON as the end-to-end UI stack;
  LVGL remains only the rendering backend.
- Keep QSee.ai telemetry reads behind `DeviceSnapshotService`; the log viewer
  may read only the canonical logger.
- Keep internal `/littlefs` application resources separate from the removable
  `/sdcard` data and logger volume.
- Never enable automatic SD formatting.
- Reuse the BSP I2C handle; do not initialize a competing bus driver.
- Prefer official ESP-IDF, FreeRTOS, BSP, and managed-component APIs.
- Keep snapshot refresh explicit until the sensor-fusion design exists.
- Treat serial logs as runtime evidence and report degraded devices honestly.
- Keep generated builds, `sdkconfig`, runtime logs, managed components, caches,
  and local virtual environments out of Git.
- Do not replace or rewrite original brand and audio source files.

## Hardware validation status

This migration does not claim physical-device validation. A later hardware
session must verify display and touch startup, System Super resource loading,
all six JSON screens, keyboard and dialog services, Wi-Fi/BLE provisioning,
audio, IMU, GPS, sleep/wake behavior, and the removable-SD states.

Any SD-format test must use a disposable card, begin from a confirmed
format-required state, display the destructive System Super confirmation, and
verify that cancellation performs no write.
