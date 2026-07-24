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
2. mount the SD card without automatic formatting;
3. start display and touch through the BSP;
4. show the QSee.ai boot console and live logger messages;
5. play the boot SFX through the ES8311 speaker path;
6. initialize PMU, RTC, IMU, GPS, Wi-Fi, BLE, and the delayed ES7210 microphone
   worker;
7. create an explicit hardware snapshot and log memory/core state;
8. wait for touch, then launch ESP-Brookesia and the QSee.ai application.

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

- battery, USB power, and AXP2101 state;
- PCF85063 RTC;
- QMI8658 accelerometer, gyroscope, and temperature;
- LC76G transport and pending NMEA data;
- ES8311 speaker and ES7210 microphone levels;
- SD capacity and logger mirroring;
- display, touch, Wi-Fi, and BLE health;
- free heap, minimum free heap, internal RAM, PSRAM, and largest blocks;
- ESP32 core count and the current task, priority, and stack watermark sampled
  for each core.

The service refreshes only on explicit request. Future sensor fusion should
consume one immutable snapshot instead of polling drivers independently. The
memory and processor readings use native ESP-IDF and FreeRTOS APIs. See
[device-snapshot.md](docs/architecture/device-snapshot.md).

## Navigation

The application shell owns a route history. Opening a confirmation pushes the
current route; Confirm replaces it with the result, while Cancel or Back pops
the previous route. Every transition is written to the canonical logger.
Cardinal controls remain shell-owned so individual screens cannot redefine the
round-display interaction model.

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

Always pass the port explicitly before flashing. Build success is not hardware
proof; keep serial evidence separate from compiler evidence.

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

The dedicated board is visible on `COM12`. The 2026-07-25 runtime pass confirmed
a stable boot without the former `esp_timer` stack overflow. The LVGL display
uses a 10-row, single internal DMA-capable buffer; the validation capture had
zero draw or SPI DMA-allocation failures. PMU, RTC, IMU, display, touch, Wi-Fi,
BLE advertising, ES8311 playback, and ES7210 initialization were ready.

The card still returned an SDMMC OCR timeout. The LC76G acknowledged its I2C
transport addresses but its NMEA read remained degraded. Consult the newest
ignored file under `runtime-logs/` rather than treating this dated note as
permanent truth.
