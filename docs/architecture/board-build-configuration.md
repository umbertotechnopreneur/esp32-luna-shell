# Board build configuration

The first board YAML is
[`ui-bootstrap.yaml`](../../config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml).
It selects the `ui-minimal` compiler variant on the Waveshare ESP32-S3 Touch
AMOLED 1.75 board. Run it through the repository wrapper:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 set-target -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
pwsh -NoProfile -File .\scripts\device.ps1 build -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
pwsh -NoProfile -File .\scripts\device.ps1 size -BoardConfig config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml
```

`set-target` is required after changing the YAML. The wrapper validates its
exact board, module, and UI contract before ESP-IDF runs. Its hash is part of
the build state, so an edited configuration cannot silently reuse an old
`sdkconfig` or dependency lock. Other YAML combinations are rejected until
their component graph and dependency lock are reviewed.

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
The boot splash, audio, radios, PMU, RTC, IMU, GNSS, removable SD, distance,
provisioning, and HTTP services are disabled by this profile. Their runtime
snapshot states stay explicitly unavailable where applicable.

## Current compile boundary

The Waveshare BSP is still one C component containing display, touch, I2C,
audio, SD, and an I/O expander. Selecting this build does **not** yet prove that
all disabled BSP code and dependencies are absent from the binary or dependency
graph. The next checkpoint separates the BSP's bus, display/touch, audio, and SD
units so `off` removes the corresponding driver sources and dependencies. Only
then can additional board adapters and arbitrary `off`/`service`/`ui` module
combinations be accepted. A future board must supply an explicit adapter and
pinout; changing the YAML board name alone is rejected.

No flash or on-device behavior is implied by a successful host build. Device
access needs explicit authorization for the requested operation.
