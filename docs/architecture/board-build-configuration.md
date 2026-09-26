# Board build configuration

The Waveshare board YAML is
[`ui-bootstrap.yaml`](../../config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml).
[`ui-services.yaml`](../../config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-services.yaml)
shows all supported services enabled on the same BSP.
It selects the `ui-minimal` shell on the Waveshare ESP32-S3 Touch AMOLED 1.75.
Display, touch, shared I2C, and internal LittleFS/NVS remain required. Set any
of `pmu`, `battery`, `imu`, `gnss`, `audio_output`, `microphones`, or `sd` to
`service` to compile and start that device without adding a UI page. `off`
excludes its driver path and service from the firmware. Battery service needs
PMU service; PMU can run PWR button events while battery telemetry is off.
ES8311 audio output and ES7210 microphones are independent. Only this BSP is
accepted by the YAML validator.

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
`package_shell/res/root.json`. The shell shows `LUNA SHELL` and increments a
counter on touch, so display and input can be checked without sensor pages.
The default YAML leaves optional services off. Boot splash, radios, RTC,
distance, provisioning, and HTTP remain off for every shell configuration.
Their runtime snapshot states stay explicitly unavailable where applicable.

## Current compile boundary

The Waveshare BSP remains one C component, with preprocessor gates for audio
and removable SD. The individual audio codec routes are also gated, so an
input-only build does not construct ES8311 and an output-only build does not
construct ES7210. When both are off, `esp_codec_dev` leaves the managed graph.
QMI8658 and XPowers leave the graph when IMU and PMU are off, respectively.
Battery readings and the battery snapshot sampling path compile only when
`battery: service`; the PMU retains only the PWR button IRQ path when battery
telemetry is off. Battery detector, ADC, and charger policy setup are included
with the battery module.
`esp_board_manager` remains required by ESP-IDF component Kconfig integration;
it pulls FatFS and SDMMC into the component graph even though this BSP does not
compile its SD mount, format, or status functions when `sd: off`. A later
checkpoint must separate the BSP into bus, display/touch, audio, SD, and
I/O-expander units and remove remaining indirect dependencies where possible.
Device UI pages are not yet selectable in this shell profile. A future board
must supply an explicit adapter and pinout; changing the YAML board name alone
is rejected.

The Windows simulator can preview this exact YAML and the shell's packaged
`root.json` with `run-simulator.ps1 -BoardConfig`. It shows the 466 × 466
screen, one 320 × 320 touch area, and the tap counter. A visible `SYNTHETIC`
label distinguishes the preview from firmware. Its contract checks verify
the YAML, screen document, and touch bounds; they cannot prove Brookesia,
LVGL, panel, or CST9217 behavior on hardware.

No flash or on-device behavior is implied by a successful host build. Device
access needs explicit authorization for the requested operation.
