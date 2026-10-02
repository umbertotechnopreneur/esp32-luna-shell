# Standalone display and touch

The `display-touch` profile selects the physical CO5300 AMOLED and CST9217
touch controller without the Luna application, Brookesia GUI, LVGL, or the
LVGL adapter. Both devices are mandatory for this profile. An initialization
failure stops startup and remains visible in `DeviceSnapshotService`.

`LUNASHELL_BUILD_ENABLE_DISPLAY` and `LUNASHELL_BUILD_ENABLE_TOUCH` select
hardware. `LUNASHELL_BUILD_ENABLE_UI` selects only the existing Luna GUI.
The existing UI profiles retain that GUI. Headless retains neither display nor
touch. Touch uses the panel's logical orientation and requires the display.
Select a UI-off catalog profile rather than disabling the GUI inside an
existing UI profile: component roots are admitted before Kconfig runs. An
inconsistent combination is rejected instead of retaining unused GUI roots.

The standalone owner exposes bounded RGB565 writes and actual touch samples.
Its synchronous transfer copies PSRAM pixels into short internal DMA strips;
the caller can reuse its source buffer after the write returns. Rendering,
fonts, widgets, animation, and frame allocation belong to the consuming app.
The driver reports real read/transfer errors rather than plausible input.

`LUNASHELL_display_set_rotation_degrees()` applies and persists exactly 0, 90,
180, or 270 degrees while auto-rotation is disabled, including without an IMU.
The panel and touch change together. A failed persistence operation rolls the
panel back and publishes the real failure. Invalid saved settings require an
explicit reset. Renderers must redraw the whole frame after the atomic content
revision changes on rotation or wake; the standalone BSP keeps no frame cache.

The pinned CST9217 driver has a repository-owned configure-time patch that
preserves the original I2C error and invalidates failed samples. Patch drift
stops configuration; it does not silently admit an unpatched driver.

The profile selects no radios, sensors, audio, SD, or GNSS by default. A
separate connected consumer can select those services and own their lifecycle
without depending on Luna Settings.

## Deferred validation

This checkpoint has static review only. No configure, dependency resolution,
build, simulator, test, size report, or hardware operation has been run.
The new profile's canonical dependency lock must be generated and reviewed in
an explicitly requested validation session. Existing locks also need review
after the BSP's manifest gates change. Do not invent a lock or reuse a GUI
lock for the standalone profile; the wrapper rejects missing locks on build.

Only when that workflow is requested, use `scripts/device.ps1 update-lock
-Profile display-touch`, then the requested configure/build workflow. Keep
all outputs under `artifacts/`. On-device display, touch, orientation, DMA,
radio coexistence, and standby behavior remain separate physical checks.
