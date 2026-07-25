# Round-device firmware

Firmware and Windows simulator for the device-console application on the GPS
variant of the [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.html) and its [wiki page](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75).

The device application uses the official Espressif ESP-Brookesia 0.8 stack:
System Super owns the system shell and services, System Core owns the native
application lifecycle, and GUI LVGL renders the declarative JSON interface.
LVGL 9 is the rendering backend, not an application-level UI API. The official
ESP-Brookesia monorepo is tracked as the `third_party/esp-brookesia` submodule,
pinned to commit `5361118b9953800e4fe710e57e5314fccec9cbcf`.

The official Waveshare board support package (BSP) remains the owner of the
display, touch panel, audio codecs, shared I2C bus, and board-specific pins.

## Product and UI contract

The AMOLED is a circular 466 x 466 display. The firmware defines a 320 x 320
safe content square at `(73, 73)`. All information and controls required to finish a
screen must fit in that square. The curved perimeter is reserved for stable,
easy-to-learn shell actions:

Route containers and the title bar inside the safe square must remain
transparent: they do not own a background, card, pill, or gradient. The
full-screen application background is the single visual atmosphere and uses a
dark radial spotlight gradient. Local cards may add only subtle contrast when
they represent actual information or an action.

| Edge | Default action |
| --- | --- |
| Left | Back or Cancel |
| Right | Next, Confirm, or OK |
| Top | Close, menu, or status |
| Bottom | Contextual primary action |

The minimal boot screen is the first application frame. It shows only the
canonical artwork and the `INITIALIZING` / `OPENING WELCOME` handoff state;
logs remain off-screen. See
[round-display-guidelines.md](docs/ui/round-display-guidelines.md) for the
complete geometry.

## Canonical startup order

1. install the canonical logger;
2. start display and touch through the BSP;
3. show the minimal boot screen;
4. initialize PMU, RTC, IMU, GPS, and the shared ES7210 microphone path, take
   the bounded initial microphone sample, then restore persisted brightness;
5. play the boot SFX through the ES8311 speaker path;
6. start the non-blocking SD service; retry only explicit transient
   card/transport or filesystem failures, without automatic formatting;
7. create an explicit pre-UI hardware snapshot while Wi-Fi and BLE remain off;
8. change the boot state to `OPENING WELCOME`, repaint the full panel black,
   and hand off immediately to Brookesia System Super;
9. let the official HAL adaptor mount internal LittleFS and initialize its NVS
   key-value backend, with mount-time formatting disabled;
10. launch the registered System Core application. Wi-Fi and BLE start only
    after the explicit action on the Settings screen.

`qsee_hardware_init()` runs its PMU, RTC, IMU, GPS, and audio stages in a fixed
order and stops that service at the first failure. The startup orchestrator
explicitly treats the hardware inventory, time service, power inputs, boot
audio, removable storage, and pre-UI refresh as optional; their exact failures
remain visible while the UI continues. Logger, display, boot UI, persisted
brightness, and the UI handoff are required stages. No failure is rewritten as
success, and numeric telemetry is rendered only from a valid, fresh sample.

## Canonical logger

Serial output is always authoritative. The logger also keeps a bounded RAM
ring for the System Core Logs screen and mirrors queued lines to
`/sdcard/qsee/logs/qsee.log` after a successful mount. The SD writer rotates at
2 MB to `qsee.log.1`. Logger formatting and snapshots use static bounded
storage so logging cannot exhaust the `esp_timer` task stack.

See [startup-and-logger.md](docs/architecture/startup-and-logger.md).

## Hardware snapshot

`qsee::DeviceSnapshotService` is the single published view of:

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

Each full refresh is staged and committed as one snapshot generation;
asynchronous subsystem reports commit their own generations. Component
timestamps are aged against freshness TTLs, so expired numeric telemetry is
marked `STALE` and hidden from consumers.

The full-device refresh remains explicit. An opt-in diagnostic monitor samples
only the IMU and microphone periodically at the configured diagnostic cadence;
SD status polling and native Wi-Fi/BLE events may also publish asynchronous
state changes. Every UI telemetry view consumes the immutable published
snapshot instead of polling hardware drivers independently. The log viewer
reads the canonical logger ring, never a hardware driver. The memory and
processor readings use native ESP-IDF and FreeRTOS APIs. See
[device-snapshot.md](docs/architecture/device-snapshot.md) and
[hardware-api.md](docs/architecture/hardware-api.md).

## Navigation

The native app implements the System Core `IApp` lifecycle. GUI LVGL
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
| `Settings` | Implemented | `SETTINGS` | Diagnostic cadence, IMU smoothing, SD log mirroring, deferred radio startup, and persisted brightness |
| `About` | Implemented | `ABOUT` | Device identity and support information |
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

The board has no SD card-detect pin. A failed SDMMC initialization therefore
means `card/transport unconfirmed`, never a confirmed physical absence. Firmware
never formats implicitly: `format-required` is published only after a card
initializes and FatFs returns exactly `FR_NO_FILESYSTEM`. Destructive consent is
single-use and bound to the observed CID fields plus capacity for the immediate
retry. A different card or failed retry consumes the permission without
carrying it forward. A verified `format-required` result pauses probing until
that explicit permission arrives. Internal, destructive, poisoned, or
incompletely cleaned failures are terminal for the current boot. While mounted,
the storage service polls card status and invalidates the mount on removal or
transport failure.

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
through `scripts/device.ps1`; bounded serial capture uses the same entrypoint.
Do not bypass it.

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
pwsh -NoProfile -File .\scripts\device.ps1 capture -Port COM12 -DurationSeconds 30 -OutputPath .\runtime-logs\device.log
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

Always pass the port explicitly for every device action. The helper terminates
stale monitor and capture processes scoped to that port before checking whether
it is openable, then fails the command if flashing, erasing, or capture fails.
Build success is not hardware proof; keep serial evidence separate from
compiler evidence.

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

Owned C and C++ sources use the canonical source banner.
Preview or apply it with:

```powershell
pwsh -NoProfile -File .\scripts\add-source-banner.ps1
pwsh -NoProfile -File .\scripts\add-source-banner.ps1 -Apply
```

The script is idempotent, supports `-Remove -Apply`, and intentionally excludes
the Waveshare BSP, ESP-Brookesia, managed components, build output, generated
files, and other third-party code. Personal contact details are omitted from the public tree.

## Repository map

| Path | Responsibility |
| --- | --- |
| `main/` | Ordered application startup |
| `components/qsee_logger/` | Serial, bounded RAM, and SD logging |
| `components/qsee_boot/` | Minimal boot screen and ES8311 playback |
| `components/qsee_platform/` | Hardware drivers, radios, and snapshots |
| `components/qsee_app/` | Native System Core app, lifecycle/data binding, and JSON screen package |
| `components/esp32_s3_touch_amoled_1_75/` | Local Waveshare BSP |
| `third_party/esp-brookesia/` | Official pinned ESP-Brookesia monorepo submodule |
| `assets/` | Canonical artwork and SFX inputs/derivatives |
| `simulator/windows/` | Synthetic Windows debug build |
| `scripts/` | Build, flash, serial capture, and asset generation |

## Rules for humans and AI agents

[`AGENTS.md`](AGENTS.md) is the repository-wide engineering contract. This
firmware targets ESP-IDF 6.0.2 and Brookesia 0.8 only, fails fast when recovery
is ambiguous, and never silently repairs settings or manufactures sensor
values.

- Preserve logger-first startup; all device state must flow through it.
- Run every ESP-IDF workflow through `scripts/device.ps1`.
- Keep System Super, System Core, and GUI LVGL JSON as the end-to-end UI stack;
  LVGL remains only the rendering backend.
- Keep telemetry reads behind `DeviceSnapshotService`; the log viewer
  may read only the canonical logger.
- Keep internal `/littlefs` application resources separate from the removable
  `/sdcard` data and logger volume. The Brookesia HAL adaptor owns internal
  LittleFS/NVS; the firmware owns the removable SD lifecycle.
- Never enable automatic SD formatting.
- Reuse the BSP I2C handle; do not initialize a competing bus driver.
- Prefer official ESP-IDF, FreeRTOS, BSP, and managed-component APIs.
- Keep the full-device refresh explicit; periodic sampling remains limited to
  the opt-in diagnostic monitor until the sensor-fusion design exists.
- Treat serial logs as runtime evidence and report degraded devices honestly.
- Keep generated builds, `sdkconfig`, runtime logs, managed components, caches,
  and local virtual environments out of Git.
- Do not replace or rewrite original brand and audio source files.

## Hardware validation status

This migration does not claim physical-device validation. A later hardware
session must verify display and touch startup, System Super resource loading,
all seven JSON screens, keyboard and dialog services, Wi-Fi/BLE provisioning,
audio, IMU, GPS, sleep/wake behavior, and the removable-SD states.
The earlier `COM12` observations recorded in the hardware inventory are
historical context only; they do not validate this commit.

Any SD-format test must use a disposable card, begin from a confirmed
format-required state, display the destructive System Super confirmation, and
verify that cancellation performs no write.
