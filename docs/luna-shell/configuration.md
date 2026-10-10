# Board build configuration

For mandatory display and touch without the Luna UI, use the dedicated
[`display-touch` runtime](../bsp/core/display-touch-runtime.md). The UI shell described
below remains the Brookesia/LVGL consumer of the same physical devices.

The Waveshare board YAML is
[`ui-bootstrap.yaml`](../../config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml).
[`ui-services.yaml`](../../config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-services.yaml)
shows all supported services enabled on the same BSP.
It selects the `ui-minimal` shell on the Waveshare ESP32-S3 Touch AMOLED 1.75.
Display, touch, shared I2C, and internal LittleFS/NVS remain required. Set any
of `pmu`, `battery`, `imu`, `gnss`, `audio_output`, `microphones`, or `sd` to
`service` to compile and start that device without adding a UI page. `off`
excludes its driver path and service from the firmware. Battery service needs
PMU service; PMU keeps PWR button events, the board's charging policy, and
unused TS-pin measurement disabled while battery telemetry is off.
ES8311 audio output and ES7210 microphones are independent. Only this BSP is
accepted by the YAML validator.

The validated YAML adapter owns the module-to-library mapping in
`scripts/common/board_config.py`. Setting `sd: {driver: sdmmc, mode: service}`
automatically admits `fatfs`, `sdmmc`, and `esp_driver_sdmmc` before Kconfig
discovery; no extra library switches are required in the YAML. The wrapper
merges those roots for every supported YAML profile, not just `ui-minimal`.
The BSP exports the SD/FAT headers and rejects a missing required component
during configuration, before compilation. Audio, IMU, and PMU use the same
dependency mapping. `off` adds no module-specific roots, but another required
component can still bring a library into the graph transitively.

Run the selected YAML through the repository wrapper:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 set-target -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
pwsh -NoProfile -File .\scripts\device.ps1 build -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
pwsh -NoProfile -File .\scripts\device.ps1 size -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
```

`set-target` is required after changing the YAML. The wrapper validates the
board and module modes before ESP-IDF runs. The seven service switches select
an isolated generated `sdkconfig` and build tree under `artifacts/`; audio,
IMU, and PMU select their managed dependency graph. If a graph's canonical
lock is absent, run `device.ps1 update-lock -BoardConfig <path>`, review and
commit the resulting lock, then build. The YAML hash is part of the build
state, so edited settings cannot silently reuse an old configuration.
Reconfiguration cleans only the selected build directory. The component-manager
extension is disabled temporarily for `fullclean` so it cannot delete the shared
managed components and their reviewed local patches. Dependency resolution and
lock verification remain enabled for configuration and compilation.

## Minimum physical set

| Purpose | Required part | Build role |
| --- | --- | --- |
| Execution | ESP32-S3R8, flash, PSRAM | ESP-IDF target, code, LVGL frame buffers |
| Display | CO5300 AMOLED on QSPI | Visual output and brightness |
| Input | CST9217 touch on shared I2C | Touch input and visible tap counter |
| Internal state | NVS and LittleFS in flash | Saved brightness and the one-screen app package |

The build retains the logger, liveness monitor, snapshot publication, LVGL,
Brookesia GUI LVGL, and System Core needed to launch a native app. It compiles
`LUNASHELL_app_shell.cpp` instead of the full app sources and stages only
`package_shell/res/root.json`. The shell displays the idle mascot and logs a
touch counter, so display and input can be checked without sensor pages.
The default YAML leaves optional services off. Boot splash, radios, RTC,
distance, provisioning, and HTTP remain off for every shell configuration.
Their runtime snapshot states stay explicitly unavailable where applicable.

## Service lifecycle

For the shell profile, boot starts the periodic snapshot monitor after hardware
initialization and application readiness whenever battery, GNSS, IMU, or
microphones are selected. Sampling therefore does not depend on a device UI page.
The existing power manager suspends and resumes this monitor around standby.
Monitor startup errors stop the startup stage and remain visible in diagnostics.

With PMU selected, the shell polls power state and acknowledges standby only
after Brookesia has quiesced input and drained pending GUI work. It reopens input
after the power manager reports Active. Failed drains propagate an error; stale
callbacks from cancelled transitions or stopped app instances cannot acknowledge
a newer transition. This does not add any device UI page.

## Current compile boundary

The Waveshare BSP remains one C component, with preprocessor gates for audio
and removable SD. The individual audio codec routes are also gated, so an
input-only build does not construct ES8311 and an output-only build does not
construct ES7210. The generated SDK configuration also disables that codec's
source files and the codec component's unrelated default drivers. When both are
off, `esp_codec_dev` leaves the managed graph.
QMI8658 and XPowers leave the graph when IMU and PMU are off, respectively.
Battery readings and the battery snapshot sampling path compile only when
`battery: service`; battery detector and ADC setup belong to that module. The
PMU retains PWR IRQ handling, TS disablement, and the same 50/400/25 mA charge
policy with a 4.20 V target even when battery telemetry is off.
The enabled SD service now declares its own FatFS/SDMMC dependencies; it does
not depend on an unrelated component admitting them accidentally. When
`sd: off`, this BSP does not compile SD mount, format, or status functions,
although Brookesia or another required component can still pull the libraries
into the graph. Separating the BSP into smaller units and removing remaining
indirect SD dependencies is still deferred in `planned-todo.md`.
Device UI pages are not yet selectable in this shell profile. A future board
must supply an explicit adapter and pinout; changing the YAML board name alone
is rejected.

The Windows simulator can preview this exact YAML and the shell's packaged
`root.json` with `device.ps1 simulator-luna -ToolArguments '--board-config'`. It shows the 466 × 466
screen, one 320 × 320 touch area, and the mascot. Taps increment the synthetic
console counter. A visible `SYNTHETIC`
label distinguishes the preview from firmware. Its contract checks verify
the YAML, screen document, and touch bounds; they cannot prove Brookesia,
LVGL, panel, or CST9217 behavior on hardware.

No flash or on-device behavior is implied by a successful host build. Device
access needs explicit authorization for the requested operation.

The lifecycle, charging, codec, and build-clean corrections above are pending the
final validation package recorded in `TODO.md`. Regression cases for the actual
shell lifecycle and build-clean helper are prepared in
`scripts/device.ps1 check -Check board-services`; tests and firmware builds will run only on
explicit user request.
