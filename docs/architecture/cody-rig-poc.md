# Cody cloud rig PoC

This is a local character/rendering experiment on `esp32-cody-companion` in
`E:\ESP32 Cody Companion`. It reuses the Windows simulator toolchain from Luna,
the standalone display/touch owner and the local CODY2D renderer. Display, touch,
Wi-Fi and Bluetooth remain mandatory; the PoC disables HTTP diagnostics and
does not implement a companion server, OOBE, voice or emotional-state inference.

## Authoring source

`assets/cody/cody.yaml` owns the character name, stable entity IDs and readable
labels, point names and normalized coordinates, cubic connectivity, independent
channel limits, initial placement, preview presets and style. `style.paints`
maps roles to palette ranges. `style.debug` assigns anchor, knot, handle, link
and selection colors; `style.back` colors the icon-only lateral control.
`style.host_font` selects the desktop inspector font; firmware labels/fonts are
not introduced by this PoC.

The shared table in `assets/cody/palette/palette256.json` has exactly 256 slots:
172 allocated colors and 84 reserved black slots. All rig fills, strokes and
debug markers use this table, quantized to RGB565. Blue uses slots 20-83; mint
uses 84-131. To try mint, change `style.paints.cloud` and `style.back.fill` to
`[84, 131]`, regenerate the tables, then restart the preview. Core graphics and
tables stay in internal flash. Original boot assets retain their own indexed
palettes; this does not recolor those approved PNGs.

```powershell
# Use the existing Luna simulator interpreter, or a local environment with Pillow/PyYAML.
& 'E:\ESP32 Luna Shell\.venv-simulator\Scripts\python.exe' -B .\scripts\generate_cody_rig.py
```

Conversion checks the authored contract and writes immutable C++ data under
`assets/cody/rig/generated/`. It is separate from building or running firmware.
Do not edit generated point tables. IDs use viewer-left/viewer-right consistently.
The point cloud contains 145 named points, 44 cubic segments and 11 drawn parts.

## Geometry and animation

Points use normalized offsets relative to their named parent, with x right and
y down. Parents precede children, so moving an eye anchor also moves its curve
controls and highlight. Channel deltas are applied to those offsets, the parent
chain resolves into centered rig coordinates, and a shared translation/uniform
scale places the whole character. Rendering and the debug overlay read one evaluated scene. The overlay
shows semantic parent links, cubic tangent lines, knots and handles. Selecting
a point reports its stable name and evaluated local/canvas coordinates.

The independent channels are left/right eye openness, left/right brow tilt,
mouth openness and breathing. No enum selects an entire face. A four-second
breathing cycle and a smooth 280 ms blink every 4.8 seconds compose over those
base controls, so closed or half-open eyes can coexist with a breathing mouth.
Both clocks use elapsed time, skip overdue frames and never alter sensor data.
There is no invented charge, motion, stress or fear measurement.

The first raster pass uses twelve straight samples per cubic, rounded strokes,
scanline fills and the indexed soft ramp. It has no edge antialiasing yet. The
host and device use the same authored curves and additive control equations,
but different raster backends (Pillow versus CODY2D); pixel parity is not claimed.
This PoC is for geometry, composition, timing and resource observations before
choosing a quality/performance tradeoff.

## Simulator controls

Run from the Cody checkout:

```powershell
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -Cody -PythonExecutable 'E:\ESP32 Luna Shell\.venv-simulator\Scripts\python.exe'
```

The rig opens with points and handles visible. Use the six sliders independently;
pause animation to inspect an exact pose, choose Sleep/Waking/Ready as facial
channel examples, and change position/scale without changing topology. The entity
selector comes from YAML. Clicking near a rendered point inspects it. Orientation
accepts 0/90/180/270 degrees and inverse-maps mouse input through the same rotation.
The safe-square guide is optional. Back is a left chevron at (10,203), 60 x 60,
returning to the completed boot artwork; touching the center of that ready screen
reopens the rig.

The right lateral points icon toggles the geometry overlay in both host and
device views. It starts enabled, and allows comparing artwork rendering with
debug drawing cost while keeping the character animation running.

The screen selector also exposes `boot-sleep`, `boot-waking` and `boot-ready`,
using the three existing Imagegen artworks. These are manually selected desktop
previews, not simulated proof that a hardware startup stage completed. All
desktop views are labelled SYNTHETIC outside the circular UI.

```powershell
pwsh -NoProfile -File .\simulator\windows\run-simulator.ps1 -Cody -CodyScreen boot-waking -Capture .\artifacts\cody-poc\boot-waking-launcher.png -PythonExecutable 'E:\ESP32 Luna Shell\.venv-simulator\Scripts\python.exe'
& 'E:\ESP32 Luna Shell\.venv-simulator\Scripts\python.exe' -B .\scripts\verify_cody_poc.py --previews
```

The focused host package exercises named geometry, transforms, independent
channels, animation bounds/continuity, rotation/input, palette use, current
generated-table provenance, and real Tk navigation controls. Its optional PNGs,
24-second looping GIF and measured host raster timings stay in
`artifacts/cody-poc/`. GIF playback is authored at 25 fps; that is not a firmware
benchmark. The interactive desktop loop targets 60 Hz and reports actual host
render and interval times.

## Prepared device behavior

The three startup images retain their real checkpoints:

| Image | Startup boundary |
| --- | --- |
| Closed eyes | Mandatory display/touch and saved orientation ready |
| Half-open eyes | Local time/power/storage stages finished, before radios |
| Open eyes | Remaining stages and startup-watchdog cleanup completed |

There is no timer that claims 50 percent boot completion or hides a startup
failure. The last successfully completed checkpoint stays visible on failure.
After ready, a touch within the safe square opens rig debug with breathing and
blinking. A fresh press selects a point and logs its stable name; the icon-only
Back returns to the ready artwork. Debug is enabled by default inside this route.

The device targets a 33 ms animation interval, not a promised measured frame rate.
One PSRAM scene is evaluated per frame; CODY2D draws through the existing
466 x 8 RGB565 tile (7,456 bytes), without a full framebuffer. The existing BSP
continues to own cardinal rotation, touch mapping, panel wake and DMA transfer.
The shared display lock keeps one frame's strips under one orientation. Updates
park while display writes are disabled and redraw when the panel becomes writable.

At most once every two seconds, successful rig frames log their measured geometry,
raster, transfer and total microseconds, overlay mode and scene/strip bytes. Errors retain
their original return and terminate the consumer. No performance estimate is
published as CPU use or sensor telemetry. The worker stack is 6 KiB in PSRAM;
high-water mark and Wi-Fi/BLE headroom remain physical-validation questions.

Firmware dependency-lock resolution, configure/build/size, flash and physical
performance are not part of the host PoC checks. They remain pending in `TODO.md`
and must use `scripts/device.ps1` when explicitly requested.

## Host evidence, 2026-10-02

Ten focused checks passed, including the real Tk screen/rotation/control path
and moving a parent anchor with all descendant controls.
The common PowerShell launcher also captured the waking boot screen successfully.
All three boot views and rig previews were rendered; the rig and cardinal
rotation output were visually reviewed. The 600-frame artwork-render sample
measured 4.09 ms mean and 6.04 ms p95 on this PC. That sample excludes Tk window
presentation and device transfer; it does not establish interactive or ESP32 FPS.
`artifacts/cody-poc/host-review.json` records the checks and explicit unverified
firmware boundaries. `host-render-review.json` retains the separate render sample.
`git diff --check` passed for this change.
