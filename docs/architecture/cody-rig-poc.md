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
debug markers use this table, quantized to RGB565. Slot numbers are an encoding detail. Blue occupies slots 20-83; mint
occupies 84-131. YAML refers to their unique color IDs. To try mint, change `style.paints.cloud` and `style.back.fill` to
`[mint.level.00, mint.level.47]`, regenerate the tables, then restart the preview. Core graphics and
tables stay in internal flash. Original boot assets retain their own indexed
palettes; this does not recolor those approved PNGs.

```powershell
# Select a Python interpreter with Pillow/PyYAML through -PythonExecutable if needed.
.\scripts\device.ps1 generate -Generator cody-rig
```

Conversion checks the authored contract and writes immutable C++ data under
`assets/cody/rig/generated/`. It is separate from building or running firmware.
Do not edit generated point tables. IDs use viewer-left/viewer-right consistently.
The point cloud contains 145 named points, 44 cubic segments and 11 drawn parts.

## Named skins and readable implementation

The v2 authoring contract selects one front-facing character per YAML. Points,
curves, paths, entities and colors have unique IDs within their typed namespace.
Paths list curve IDs directly; boot and expression presets map channel names
to values. YAML declaration order does not choose a control's meaning.
The generator rejects duplicate IDs and names that collide after C++ enum
conversion. Numeric offsets remain internal to compact rendering tables.

`FaceChannel`, `ContourControl`, `PointRole` and `CurvePoint` make engine
semantics explicit. Generated `PointId`, `CurveId`, `PathId`, `EntityId`,
`ColorId` and `ContourLobeId` carry authored identities into firmware.
Style owns the named background, paint ramps, debug colors, debug line/marker
metrics and lateral icon stroke widths. Native raster limits remain engine
constraints rather than skin settings.

The selected contour path and its bounded lobe definitions come from YAML;
the engine no longer requires Cody's eleven particular lobes. A skin still
must fit the supported ten face controls, four contour controls and bounded
front-facing geometry. This is character reuse within this renderer's contract.

```powershell
# Preview the selected YAML directly.
.\scripts\device.ps1 simulator -Skin assets/cody/cody.yaml

# Select the YAML for the next firmware asset conversion.
.\scripts\device.ps1 generate -Generator cody-rig -ToolArguments '--skin','assets/cody/cody.yaml'
```

Conversion replaces the active generated rig package and records the selected
source path and character ID in its manifest. Firmware reads that one package;
there is no runtime YAML parser or skin switch. The three existing boot
artworks stay separate and need their own authoring/conversion for a new
character's complete startup imagery.

The loader separates style resolution, channel definitions, geometry ownership,
face bindings and contour constraints. Firmware drawing separates artwork,
debug geometry and lateral navigation. The companion separates geometry
evaluation from measured frame diagnostics and adopts an acquired BSP lock
with an automatic release on every draw exit.

The preview separates face, contour and debug control construction from frame
advancement. Frame presentation, contour readouts and point inspection now have
their own methods. Desktop scaling uses the same logical framebuffer; input and
labels use the layout and rotation that were actually presented.

This refactor has static source review and successful asset conversion only.
Builds, simulator execution, regression checks and physical behavior have not
been validated for this revision.

## Geometry and animation

Points use normalized offsets relative to their named parent, with x right and
y down. Parents precede children, so moving an eye anchor also moves its curve
controls and highlight. Channel deltas are applied to those offsets, the parent
chain resolves into centered rig coordinates, and a shared translation/uniform
scale places the whole character. Rendering and the debug overlay read one evaluated scene. The overlay
shows semantic parent links, cubic tangent lines, knots and handles. Selecting
a point reports its stable name and evaluated local/canvas coordinates.

The independent channels are left/right eye openness, left/right brow tilt,
mouth openness, breathing, mouth width, nose size, cheek size and cheek opacity.
No enum selects an entire face. A four-second
breathing cycle and a smooth 280 ms blink every 4.8 seconds compose over those
base controls, so closed or half-open eyes can coexist with a breathing mouth.
Both clocks use elapsed time, skip overdue frames and never alter sensor data.
There is no invented charge, motion, stress or fear measurement.

`face_transforms` binds the mouth's horizontal scale and the nose/cheeks' uniform
scale to their named anchors. Those anchors stay in place, and both artwork and
debug use the resulting evaluated controls. `cheek_size` starts at 1.2 times the
authored size; width and nose size start at 1.0. Cheek opacity ranges from 0
(fully absent) to 1 (fully opaque), independently of cheek size.

Facial targets interpolate with the YAML `face_transition_seconds` time constant
(0.18 seconds by default). Changing sliders or presets preserves the current
expression and moves toward the new target instead of replacing it abruptly.
Breathing and blinking then compose over those smoothed targets; their clocks
are not reset by a facial preset. `expression_presets.embarrassed` is a manual
example of parameter composition, not emotion detection or an enum-based state
machine. At zero active elapsed time the target transition is frozen.

The palette still contains exactly 256 authored source colors. True cheek
translucency blends their native RGB565 channels over the already drawn cloud,
rounded with an 8-bit opacity value. The resulting framebuffer can contain
derived colors outside that source palette. The host remains indexed for opaque
frames and expands partial-opacity frames to RGB whose values are quantized to
the same RGB565 channel precision. No additional firmware framebuffer is used.

### Independent cloud contour

The `contour` section in `assets/cody/cody.yaml` owns four continuous controls:
`fullness` changes the resting silhouette, `amplitude` sets the moving
displacement, `speed` is measured in cycles per second, and `phase_spread`
offsets the motion between lobes. These controls deform only the eleven named
`lobe.*` anchors and their descendant handles. They do not select facial poses
or modify eye, brow, cheek, nose or mouth coordinates. The existing global
breathing transform remains a separate control; disable facial animation and
set `breath` to zero when judging contour movement alone.

Each lobe has authored movement coefficients. The small lower lobes use smaller
displacements than the large upper ones. The fixed connectivity, stable point
names and twelve samples per cubic remain unchanged. These limits are a
conservative authoring choice; they do not establish freedom from every visual
artifact or replace simulator/device review.

The configured `transition_seconds` is the exponential smoothing time constant.
Targets can change during motion: the evaluator smooths their values and
integrates the changing speed into a persistent phase. A speed change does not
restart the cycle. Host and firmware use the same equations; no random value
is chosen per frame. A zero time step freezes the state exactly, while a zero
speed target gradually brings the motion to a stop. Zero fullness and zero
amplitude reproduce the original silhouette at any phase.

The host scene also exposes the undeformed body outline under the same current
placement and breathing scale. This reference makes local contour changes
visible without confusing them with translation, rotation or whole-body scale.
The animated outline, point markers and handles all come from the displayed
scene. The 256-slot palette and RGB565 strip dimensions are unchanged.

The first raster pass uses twelve straight samples per cubic, rounded strokes,
scanline fills and the indexed soft ramp. It has no edge antialiasing yet. The
host and device use the same authored curves and additive control equations,
but different raster backends (Pillow versus CODY2D); pixel parity is not claimed.
This PoC is for geometry, composition, timing and resource observations before
choosing a quality/performance tradeoff.

## Simulator controls

Run from the Cody checkout:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 simulator -PythonExecutable 'E:\ESP32 Cody Companion\artifacts\python\simulator\Scripts\python.exe'
```

The rig opens with points, handles, contour names and both outlines visible.
The controls are grouped into `Face`, `Contour` and `Debug` tabs. `Face` keeps
ten independent sliders, Sleep/Waking/Ready channel examples and the Embarrassed
expression example. Turning off breathing/blinking leaves the smoothed base
expression; slider and preset interpolation remains active. `Contour` exposes the four
shape/motion targets, their current smoothed values, and an independent run/freeze
toggle. Pausing freezes the full contour state; targets edited during pause take
effect after resume. Restoring contour targets never resets the phase.

`Debug` independently toggles points/handles, stable names, the current mint
outline, the original dashed amber outline and the safe square. Names use
upright desktop margin labels with leader lines ending at the evaluated,
rotated point positions. These annotations are outside the physical display
preview, not part of firmware rendering. The initial name scope shows the eleven
contour anchors. Other scopes show all anchors, a selected entity, a selected
point, or all points in groups sized to the available viewport height, up to 36
including space for the selected point. Clicking a label or marker, or choosing a name from the list,
shows normalized, logical canvas and rotated panel coordinates in the inspector.

Resizing the desktop window preserves the native 466 x 466 rendering contract.
The square preview scales uniformly and remains centered; labels retain their
readable desktop font and shorten long names when needed. At narrow widths the
controls move below the preview and remain accessible through vertical scrolling.
At wider widths they sit beside it. A shared `PreviewLayout` maps artwork,
annotation endpoints and mouse input. Input uses the last displayed layout and
orientation until the next frame presents a pending resize or rotation. Resize
notifications never advance the animation clock themselves.

The position/scale sliders change placement without changing topology. The entity
selector comes from YAML. Orientation
accepts 0/90/180/270 degrees and inverse-maps mouse input through the same rotation.
The safe-square guide is optional. Back is a left chevron at (10,203), 60 x 60,
returning to the completed boot artwork; touching the center of that ready screen
reopens the rig.

The right lateral points icon toggles point/link/handle geometry in both host
and device views. It starts enabled. Host name and outline layers have separate
toggles in `Debug`; turn off all those layers when comparing artwork-only host
rendering cost. The character animation remains independent of overlay visibility.

The screen selector also exposes `boot-sleep`, `boot-waking` and `boot-ready`,
using the three existing Imagegen artworks. These are manually selected desktop
previews, not simulated proof that a hardware startup stage completed. All
desktop views are labelled SYNTHETIC outside the circular UI.

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 simulator -Screen boot-waking -OutputPath .\artifacts\cody-poc\boot-waking-launcher.png -PythonExecutable 'E:\ESP32 Cody Companion\artifacts\python\simulator\Scripts\python.exe'
.\scripts\device.ps1 check -Check cody-poc -ToolArguments '--previews'
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

The evidence below describes the initial rig checkpoint `03c6c544`, before the
contour and named-overlay extension. That extension and the later responsive
viewport/facial-control changes have static source review
only; its newly prepared checks have not been executed. Firmware compilation,
simulator execution and device performance remain unverified for that extension.

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
