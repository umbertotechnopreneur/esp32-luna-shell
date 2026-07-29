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
4. initialize PMU, RTC, IMU, GPS, and the selected microphone path, take
   the bounded initial microphone sample, then restore persisted brightness;
5. play the boot SFX through the ES8311 speaker path;
6. when enabled, start the 16 kHz ESP-SR speech-recognition AFE after boot
   audio has completed;
7. start the non-blocking SD service; retry only explicit transient
   card/transport or filesystem failures, without automatic formatting;
8. create an explicit pre-UI hardware snapshot while Wi-Fi and BLE remain off;
9. change the boot state to `OPENING WELCOME`, repaint the full panel black,
   and hand off immediately to Brookesia System Super;
10. let the official HAL adaptor mount internal LittleFS and initialize its NVS
   key-value backend, with mount-time formatting disabled;
11. launch the registered System Core application. Wi-Fi and BLE start only
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
- total/free/minimum heap, internal RAM, and PSRAM capacity, minimum,
  largest-block, raw block-count, and allocation-failure values;
- ESP32 core count and the current task, priority, and stack watermark sampled
  for each core.

Each full refresh is staged and committed as one snapshot generation;
asynchronous subsystem reports commit their own generations. Component
timestamps are aged against freshness TTLs, so expired numeric telemetry is
marked `STALE` and hidden from consumers.

One serial platform worker requests the complete snapshot at the persisted
user cadence: 300 ms, 500 ms, 1 Hz, 2 s, 3 s, 5 s, or 10 s. This Full Snapshot
cadence is independent from both the 1 Hz UI binding refresh and the opt-in
Diagnostic Stream. The active live page independently requests either measured
IMU or microphone samples; a fresh committed diagnostic sample may satisfy one
overlapping Full Snapshot without changing its measurement timestamp. SD status
polling and native Wi-Fi/BLE events may also publish
asynchronous state changes. Every UI telemetry view consumes the immutable
published snapshot instead of polling hardware drivers independently. The log
viewer reads the canonical logger ring, never a hardware driver. The memory
and processor readings use native ESP-IDF and FreeRTOS APIs. See
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

### UI glossary

| Name | Meaning |
| --- | --- |
| `App Background` | The single full-screen dark radial atmosphere behind every route and shell component |
| `Safe Content Surface` | The transparent 320 x 320 central area reserved for route content |
| `Status Arc` | The fixed-size upper arc of six semantic device-state indicators, rendered from one immutable snapshot |
| `Command Arc` | The lower curved row of up to five route actions |
| `Main Command` | The single Command Arc action centered on the horizontal axis; it is largest and defines the route's primary intent |
| `Edge Navigation` | Contextual Back, Next, Confirm, or Cancel control placed on the circular edge |
| `Wake Surface` | The transparent full-display touch target used while automatic Adaptive Chrome is hidden or the display is dimmed/off; manual Fullscreen leaves page controls interactive |
| `Adaptive Chrome` | The reusable Status Arc and Command Arc renderer; it animates the canonical power state but owns no inactivity policy |
| `Content Viewport` | The route surface between the currently visible arcs; it expands from the 320 x 320 safe square to 393 or 466 pixels as either or both arcs disappear |
| `Power Management Service` | The single worker that owns BOOT input, AXP2101 PKEY events, inactivity timing, arc visibility, AMOLED fade/dim/off, audio standby, and IMU wake |
| `Screen Sleep` | The inactivity stage reached after dimming; AMOLED output and periodic UI refresh stop, while touch and IMU wake remain available |
| `Device Standby` | The explicit `PWR`-controlled state with the display off and voice capture suspended while the IMU wake monitor remains active |
| `Full Snapshot` | One serialized complete hardware and system acquisition, scheduled at the user-selected 300 ms to 10 s cadence |
| `Diagnostic Stream` | The separate IMU/microphone live sampler enabled only by diagnostic routes |
| `UI Refresh` | The snapshot-to-view binding pass: 1 Hz while active, 0.2 Hz while dimmed, and stopped while the display is off; it never samples hardware |

The standard page names are `Welcome`, `Live Logs`, `Hardware`, `Audio Scope`,
`Audio FFT`, `Audio Detail`, `IMU Level`, `Settings`, `About`, and
`Pairing Flow`. Use these names in code reviews, issues, screenshots, and
documentation.

The Power Management Service owns one inactivity timeline and four independent
Settings dropdowns. The `Command Arc` and `Status Arc` have separate hide
timeouts. The display-dim timeout starts from the same last-activity instant
and a shared fade reduces brightness to 50% of the user's configured value.
The Screen Sleep delay starts when dimming begins; its fade then reaches
`DisplayOff`. Defaults are 10 seconds, 2 minutes, 5 minutes, and 5 minutes
after dimming respectively. Every dropdown also offers `Never`. Touch, a
command, or a qualified IMU movement restores automatically hidden chrome and
the active display policy. Manual Fullscreen is different: one short BOOT press
hides both arcs, a second restores them, and normal page touches remain
interactive without leaving Fullscreen. If the display is already dimmed or
off, BOOT wakes it first and restores both arcs. BOOT input is enabled only
after a stable 500 ms release, including after light sleep, so a held or noisy
startup level cannot become a false command. IMU wake requires a quiet dwell
followed by three consecutive movement samples and uses a two-second
grace/cooldown. `PWR` is the AXP2101 PKEY, not a GPIO: a short event toggles
Device Standby and another short event or qualified IMU movement wakes it. A
long event is recorded and requests standby without toggling an already
sleeping device. BOOT held for three seconds performs a soft reset.

Every route consumes the same canonical arc visibility when calculating its
viewport. With both arcs visible the content surface is `y=73`, `height=320`;
hiding only one arc yields 393 pixels, and hiding both yields the full 466
pixels. Scrollable pages grow inside circular-safe 55-pixel edge insets, while
fixed diagnostic canvases remain centered instead of being stretched or
clipped.

Device Standby is deliberately a software-managed state: the AMOLED controller
output is disabled, the voice path is suspended, and both arcs and periodic UI
work stop while the CPU keeps the PMU-isolated PKEY poll and IMU wake monitor
alive. Panel power commands and rotation replay share the display lock, so
rotation is deferred while output is disabled. The
board does not provide a verified ESP32 wake GPIO for the AXP2101 IRQ, so this
flow does not claim that PWR can wake the SoC from ESP light sleep. Every policy
state and transition is published in `DeviceSnapshotService`; the UI only
animates that immutable state.

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
| `Audio` | Implemented, diagnostic | `AUDIO SCOPE` | Microphone channel levels and PCM capture status |
| `AudioFft` | Implemented, diagnostic | `AUDIO FFT` | Frequency-domain microphone spectrum |
| `AudioDetail` | Implemented, diagnostic | `AUDIO DETAIL` | Expanded live spectrum detail |
| `Imu` | Implemented, diagnostic | `IMU LEVEL` | Accelerometer, gyroscope, temperature, and calibration status |
| `Settings` | Implemented | `SETTINGS` | Full Snapshot cadence, Diagnostic Stream, Adaptive Chrome, display power, IMU smoothing, storage, radios, and brightness |
| `About` | Implemented | `ABOUT` | Device identity and support information |
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

## Build profiles and feature flags

Build-time feature switches live in
[`main/Kconfig.projbuild`](main/Kconfig.projbuild). They are exposed as
`CONFIG_QSEE_BUILD_*` options and are intended to remove whole runtime paths
from the firmware image, not just hide them at runtime.

Persist a profile in [`sdkconfig.defaults`](sdkconfig.defaults), then run
`reconfigure` or `build` again through `scripts/device.ps1`. Do not rely on the
generated `sdkconfig` file as a source-controlled configuration surface.

The available profile selectors are:

| Flag | Default | Effect |
| --- | --- | --- |
| `CONFIG_QSEE_BUILD_PROFILE_FULL` | `y` | Full Waveshare device runtime with the normal UI-first experience |
| `CONFIG_QSEE_BUILD_PROFILE_HEADLESS` | `n` | Headless runtime that boots without the Brookesia UI stack and uses `main_headless.cpp` |

The available subsystem flags are:

| Flag | Default | Effect |
| --- | --- | --- |
| `CONFIG_QSEE_BUILD_ENABLE_UI` | `y` | Includes the display and touch UI stack; disabling it removes the Brookesia app runtime and current BSP touch path from the build |
| `CONFIG_QSEE_BUILD_ENABLE_BOOT_UI` | `y` | Keeps the boot splash screen; depends on the UI stack |
| `CONFIG_QSEE_BUILD_ENABLE_AUDIO` | `y` | Includes the selected speaker and microphone paths plus related audio runtime |
| `CONFIG_QSEE_BUILD_ENABLE_BOOT_AUDIO` | `y` | Keeps the boot sound effect; depends on the selected ES8311 output path |
| `CONFIG_QSEE_BUILD_ENABLE_VOICE_AFE` | `n` | Starts the optional ESP-SR 16 kHz speech-recognition AFE; requires a selected ES8311 output and ES7210 input topology |
| `CONFIG_QSEE_BUILD_ENABLE_WIFI` | `y` | Includes the Wi-Fi station stack |
| `CONFIG_QSEE_BUILD_ENABLE_BLUETOOTH` | `y` | Includes the Bluetooth telemetry stack |
| `CONFIG_QSEE_BUILD_ENABLE_PROVISIONING` | `y` | Includes the BLE Wi-Fi pairing flow used to provision Wi-Fi credentials; depends on Wi-Fi and Bluetooth |
| `CONFIG_QSEE_BUILD_ENABLE_IMU` | `y` | Includes the QMI8658 motion pipeline and calibration path |
| `CONFIG_QSEE_BUILD_ENABLE_POWER_INPUTS` | `y` | Includes the button and power-management pipeline |
| `CONFIG_QSEE_BUILD_ENABLE_RTC` | `y` | Includes the RTC time pipeline |
| `CONFIG_QSEE_BUILD_ENABLE_GNSS` | `y` | Includes the GNSS position pipeline |

When a subsystem is disabled, the firmware now compiles the corresponding stub
entry points so higher layers receive explicit `ESP_ERR_NOT_SUPPORTED` results
instead of fake data. UI-disabled builds also switch the application entrypoint
from `main.cpp` to `main_headless.cpp`.

### Enabling a subsystem vs selecting board hardware

The flags above answer **whether a capability is present in this firmware
image**. The board-driver choices below answer which physical implementation is
compiled for it. Selecting `NONE` prevents that driver from being initialized;
the corresponding snapshot component is explicitly `UNAVAILABLE` rather than
showing synthetic data.

| Choice | Current implementation | Explicit off state |
| --- | --- | --- |
| `QSEE_BUILD_DISPLAY_DRIVER` | Waveshare AMOLED with CST9217 touch | `QSEE_BUILD_DISPLAY_DRIVER_NONE` |
| `QSEE_BUILD_AUDIO_DRIVER` | ES8311 speaker codec with ES7210 microphone ADC | `QSEE_BUILD_AUDIO_DRIVER_NONE` |
| `QSEE_BUILD_PMU_DRIVER` | AXP2101 | `QSEE_BUILD_PMU_DRIVER_NONE` |
| `QSEE_BUILD_RTC_DRIVER` | PCF85063 | `QSEE_BUILD_RTC_DRIVER_NONE` |
| `QSEE_BUILD_IMU_DRIVER` | QMI8658 | `QSEE_BUILD_IMU_DRIVER_NONE` |
| `QSEE_BUILD_GNSS_DRIVER` | LC76G | `QSEE_BUILD_GNSS_DRIVER_NONE` |

Audio has an additional directional selection so one board definition can
describe speaker-only, microphone-only, mono-microphone, and dual-microphone
variants without compiling an imaginary second channel:

| Choice | Current selections | Explicit off state |
| --- | --- | --- |
| `QSEE_BUILD_AUDIO_OUTPUT_DRIVER` | ES8311 speaker output | `QSEE_BUILD_AUDIO_OUTPUT_DRIVER_NONE` |
| `QSEE_BUILD_AUDIO_INPUT_DRIVER` | ES7210 dual microphone or ES7210 mono microphone | `QSEE_BUILD_AUDIO_INPUT_DRIVER_NONE` |

### ESP-SR voice AFE

`CONFIG_QSEE_BUILD_ENABLE_VOICE_AFE=y` adds a dedicated 16 kHz, signed-PCM16
voice pipeline. It is intentionally separate from the 22.05 kHz diagnostic
sampling path. On the current board, the ES7210 adapter provides interleaved
microphone frames and the selected topology determines the ESP-SR input layout:

| Selected input | ESP-SR layout | Meaning |
| --- | --- | --- |
| `QSEE_BUILD_AUDIO_INPUT_DRIVER_ES7210_DUAL` | `MMNR` | microphone 0, microphone 1, unused slot, ES8311 speaker reference |
| `QSEE_BUILD_AUDIO_INPUT_DRIVER_ES7210_MONO` | `MR` | one microphone and the ES8311 speaker reference |

Every 16 kHz PCM block accepted by the ES8311 output is copied into a bounded
reference ring immediately before the corresponding speaker write. The AFE
worker reads that reference together with the selected microphone frames, so it
never fabricates a second microphone channel. If the reference ring cannot
accept a block, playback returns the real error and logs it instead of silently
losing AEC alignment.

The available profiles are
`CONFIG_QSEE_BUILD_VOICE_AFE_SR_PROFILE_LOW_COST` and
`CONFIG_QSEE_BUILD_VOICE_AFE_SR_PROFILE_HIGH_PERF`. Both use the ESP-SR
speech-recognition AEC path. No `AEC_MODE_FD_*` full-duplex dialogue mode is
compiled or selected yet. That leaves the future path open without enabling
two-way conversational behavior prematurely.

The first integration is deliberately AEC-only and uses ESP-SR's built-in
WebRTC noise suppression and VAD defaults, so it does not add a `model`
partition or a wake-word model to flash. WakeNet and command recognition attach
to the already-running fetch worker in a later, explicitly sized model-partition
change; that decision must include the selected language, commands, flash budget,
and model update path.

While the AFE is running it owns 16 kHz microphone capture. The normal audio
diagnostic sampler does not reconfigure the codec underneath it; its previous
measurement ages naturally to `STALE` until the AFE is stopped. Boot audio
remains at 22.05 kHz, so the AFE starts only after boot playback.

For a new audio chip, add one mutually-exclusive input or output choice and a
small adapter that implements the existing normalized PCM contracts: selected
channel count, interleaved PCM16 capture at 16 kHz, and mono 16 kHz speaker
reference submission. Do not probe several codecs or duplicate a mono channel
to simulate a two-microphone board. A selected codec that is absent or fails
must log and return its original error; startup reports the degraded component
and continues with unrelated optional hardware.

For example, `CONFIG_QSEE_BUILD_ENABLE_IMU=n` removes the motion pipeline,
while `CONFIG_QSEE_BUILD_IMU_DRIVER_NONE=y` declares that the selected board
has no IMU. The default full profile selects the current Waveshare hardware.

If a future board has another IMU, do not make the firmware probe several
drivers and silently choose one. Add one explicit board/IMU selection in
`main/Kconfig.projbuild`, choose exactly one driver at build time, and make the
selected driver fail with its real error if that expected hardware is absent.
The existing shape is:

```text
QSEE_BUILD_ENABLE_IMU = n             no IMU code
QSEE_BUILD_ENABLE_IMU = y
  QSEE_BUILD_IMU_DRIVER_QMI8658 = y   current Waveshare board
  QSEE_BUILD_IMU_DRIVER_<NEW_CHIP> = y future board
  QSEE_BUILD_IMU_DRIVER_NONE = y      explicit board with no IMU
```

The IMU selector is a mutually exclusive Kconfig `choice`, with the board
profile selecting its default. Each selected implementation must provide the
same internal, normalized contract: initialise, read acceleration and
gyroscope in SI units, expose temperature where the chip supports it, perform
calibration where supported, and publish validity, timestamp, and the original
error. UI, sensor fusion, HTTP, and diagnostic routes then remain independent
of the chip model. The QMI8658 adapter now translates its native sample into
that normalized contract before it reaches the shared pipeline.

Adding a new IMU therefore requires four contained changes: register only its
driver dependency for the selected option; add a small driver adapter instead
of including the chip API throughout `qsee_hardware.cpp`; put its I2C/SPI bus,
address, interrupt pin, axis orientation, and supported ranges in the board
configuration; and validate both the selected build and the real board. The
new adapter must be the only driver selected by its board profile. The current
QMI8658 adapter is the reference implementation in
`components/qsee_platform/qsee_hardware.cpp`.

Use this same split for display, touch, audio codecs, PMU, GNSS, and storage:
one flag controls whether the subsystem exists, while a board selector chooses
the one physical driver implementation. This keeps smaller product variants
small, makes board swaps reviewable, and prevents a wrong driver from being
treated as a working sensor.

At boot, PMU, RTC, IMU, GNSS, and audio are now inventoried independently. A
chip that is selected but missing or faulty records its original error in the
snapshot and log, while initialization continues with the remaining chips. The
hardware service returns the first real optional-chip error to its caller, so
the startup orchestrator can report degradation without freezing the app.

IPv6 remains enabled because the ESP-Boost Asio surface used by Brookesia
compiles the IPv6 socket types. The dependency stays registry-managed; no local
Boost fork or compatibility layer is required.

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
- Keep one serialized Full Snapshot worker at the persisted user cadence;
  never add a second acquisition loop in UI, HTTP, or a subsystem.
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
