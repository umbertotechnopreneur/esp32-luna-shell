# QSee.ai round-device firmware

Firmware and Windows simulator for the QSee.ai application on the GPS variant
of the Waveshare ESP32-S3-Touch-AMOLED-1.75.

The device application uses Espressif ESP-Brookesia on LVGL 9 and keeps the
official Waveshare board support package (BSP) as the owner of the display,
touch panel, audio codecs, shared I2C bus, and board-specific pins.

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

1. install the logger;
2. start display and touch through the BSP;
3. show the QSee.ai boot console and live logger messages;
4. initialize PMU, RTC, IMU, GPS, and the delayed ES7210 microphone worker;
5. play the boot SFX through the ES8311 speaker path;
6. mount the SD card without automatic formatting;
7. initialize Wi-Fi and BLE, then request the boot Wi-Fi scan;
8. create an explicit hardware snapshot and log memory/core state;
9. wait for touch, then launch ESP-Brookesia and the QSee.ai application.

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

The service refreshes only on explicit request. Future sensor fusion should
consume one immutable snapshot instead of polling drivers independently. The
memory and processor readings use native ESP-IDF and FreeRTOS APIs. See
[device-snapshot.md](docs/architecture/device-snapshot.md) and
[hardware-api.md](docs/architecture/hardware-api.md).

## Navigation

The application shell owns a route history. Opening a confirmation pushes the
current route; Confirm replaces it with the result, while Cancel or Back pops
the previous route. Every transition is written to the canonical logger.
Cardinal controls remain shell-owned so individual screens cannot redefine the
round-display interaction model.

### Route index

The route registry currently lives in
[`components/qsee_app/include/qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp)
and is rendered by
[`components/qsee_app/qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp).
This is the current code-grounded inventory:

| Route | Status | Entry point | Declaration / implementation | Purpose |
| --- | --- | --- | --- | --- |
| `Welcome` | Implemented | Initial app screen after successful boot | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Device console home with memory, storage, CPU, radio status, and links to diagnostics |
| `Logs` | Implemented | `LIVE LOGS` | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Searchable, filterable bounded RAM log with pause, clear, and follow controls |
| `Hardware` | Implemented | `HARDWARE` or storage card | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Component health inventory, per-component details, and SD action dialog |
| `Audio` | Implemented diagnostic | `AUDIO SCOPE` | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Microphone channel levels and PCM capture status; this is not yet a waveform or spectrogram |
| `Imu` | Implemented diagnostic | `IMU LEVEL` | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Accelerometer, gyroscope, temperature, and calibration status |
| `About` | Implemented | `ABOUT` | [`qsee_navigation.hpp`](components/qsee_app/include/qsee_navigation.hpp) / [`qsee_navigation.cpp`](components/qsee_app/qsee_navigation.cpp) | Device identity, branding, and contact information |
| `Settings` | Planned | Not currently exposed | No route or screen implementation yet | User and device settings, including deferred Wi-Fi/BLE activation |
| `AudioSpectrogram` | Planned | Not currently exposed | No route or screen implementation yet | Test-audio waveform/spectrogram page, separate from the current audio-level diagnostics |
| `ImuCalibration` | Planned | Not currently exposed | No route or screen implementation yet | Dedicated calibration workflow and live IMU state page; the current `Imu` route only displays status |

The index is part of the route contract. Every creation, rename, removal, or
entry-point change must update this table in the same change as the route
declaration, screen construction, visibility logic, and entry control. The
change is incomplete unless the index contains the resulting route name,
implemented/planned status, actual entry point, and declaration/implementation
file, and those fields agree with the code review diff.

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

The PMU integration uses XPowersLib on the BSP's shared ESP-IDF 5 I2C bus,
matching the Waveshare AXP2101 example. Charging is configured for 50 mA
precharge, 400 mA constant current, 25 mA termination, and a 4.20 V target.
TS-pin measurement is disabled because this board does not expose a battery
NTC on that input.

The audio layer owns one shared ES8311/ES7210/I2S route. Both boot playback and
microphone snapshots use that layer, which serializes format changes and never
creates a competing I2S driver. Successful PCM submission is logged separately
from physical audibility.

## Firmware toolchain

This checkout is configured for ESP-IDF 5.5.4 on Windows. PowerShell 7, Python,
FFmpeg, and `winget` are available locally. Canonical configuration belongs in
`sdkconfig.defaults`; the generated `sdkconfig` is intentionally ignored.

Build:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-brookesia
```

Flash the currently dedicated device:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 flash -Port COM12 -BuildDir build-brookesia -Baud 921600
```

Capture a bounded diagnostic log:

```powershell
pwsh -NoProfile -File .\scripts\capture-serial.ps1 -Port COM12 -DurationSeconds 30 -OutputPath .\runtime-logs\device.log
```

### PowerShell quoting on Windows

Run every PowerShell command with `pwsh -NoProfile`. Prefer `-File` for
repository scripts because it avoids an unnecessary parsing layer:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 build -BuildDir build-brookesia
```

When `-Command` is required, enclose the complete inner PowerShell program in
single quotes. This prevents the invoking shell from expanding `$PID`, `$_`,
and `$env:` before the inner PowerShell process receives them:

```powershell
pwsh -NoProfile -Command '$selfPid = $PID; Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $selfPid -and $_.CommandLine -match $env:QSEE_PORT }'
```

Avoid nested `-Command "..."` invocations containing PowerShell variables.
If the program becomes long or needs another quoting layer, put it in a `.ps1`
file and invoke it with `pwsh -NoProfile -File`.

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
| `components/qsee_app/` | QSee.ai screens and navigation history |
| `components/esp32_s3_touch_amoled_1_75/` | Local Waveshare BSP |
| `components/brookesia_core/` | Espressif ESP-Brookesia source |
| `assets/` | Canonical branding and SFX inputs/derivatives |
| `simulator/windows/` | Synthetic Windows debug build |
| `scripts/` | Build, flash, serial capture, and asset generation |

## Rules for humans and AI agents

- Preserve logger-first startup; all device state must flow through it.
- Never enable automatic SD formatting.
- Reuse the BSP I2C handle; do not initialize a competing bus driver.
- Prefer official ESP-IDF, FreeRTOS, BSP, and managed-component APIs.
- Keep snapshot refresh explicit until the sensor-fusion design exists.
- Treat serial logs as runtime evidence and report degraded devices honestly.
- Keep generated builds, `sdkconfig`, runtime logs, managed components, caches,
  and local virtual environments out of Git.
- Do not replace or rewrite original brand and audio source files.

## Last physical-device validation

The 2026-07-25 runtime pass on the dedicated `COM12` device confirmed a stable
boot without the former `esp_timer` stack overflow. Boot now uses a minimal UI:
the RichText log console remains off-screen while canonical logs continue over
serial and in bounded RAM. After successful boot the application opens
`Welcome`. Touch and scrolling are responsive, and the boot SFX completes
before application startup.

The 466 x 466 display uses two PSRAM draw buffers, each 50 rows high. This
strip-based double-buffer configuration removes the former visible
line-by-line composition without requiring full-frame DMA-capable internal
memory. The latest validation did not reproduce the previous draw or SPI
DMA-allocation failures.

Wi-Fi and BLE initialization are intentionally deferred from boot to the
planned `Settings` route, so earlier observations of startup scanning or BLE
advertising are not claims about the current boot path. PMU, RTC, IMU, ES8311
playback, and ES7210 microphone sampling have been observed on the device.

The microSD is currently removed; firmware reports the SDMMC OCR timeout,
keeps retrying asynchronously, and never formats automatically. LC76G bridge
transport and bounded NMEA reads are physically verified: the device drained
two 4096-byte chunks and parsed the module version and GSV sentences. The
indoor pass had no satellite fix, so positioning remains environment-dependent.
QMI8658 warm-up and stationary bias calibration are verified, including the
board-specific gyro offset. Flash/PSRAM metrics and internal-memory pressure
are captured through native ESP-IDF APIs in every explicit hardware snapshot.
