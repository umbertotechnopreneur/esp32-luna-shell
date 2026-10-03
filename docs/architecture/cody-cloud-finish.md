# Cody cloud finish

The October 3, 2026 finish keeps Cody's front-facing identity, silhouette and
facial animation while improving color, local volume and silhouette sampling.
Blue navigation and the three original boot images remain independent.

## Rendering contract

- The 32-color body ramp uses a warm ivory crown and a cool blue-grey underside.
  Cheeks use a gentler existing coral color and retain their opacity control.
- Four opaque surface paths shade the underside and three local lobe junctions,
  each owning separate named controls that follow nearby lobe anchors. The authoring
  contract rejects facial channels, ambiguous references and controls shared
  with other paths. These are inset authored shapes, not a runtime body mask.
- `antialiasing: coverage` is explicit on the body and its four relief paths.
  The rasterizer samples four vertical positions per pixel row and computes
  horizontal coverage in 1/256-pixel units. All samples combine before a
  pixel is composited, avoiding darker overlapping segment joints.
- Fully covered opaque spans retain the contiguous fill path. Partially
  covered pixels use native RGB565 blending. The coverage arrays occupy
  3718 bytes of bounded working storage, separate from the ordinary fill
  scratch. The companion worker has an 8 KiB PSRAM stack.
- Tile coordinates stay absolute. Path culling includes half a pixel of
  coverage margin; old/new damage rectangles retain their existing 1-pixel
  margin. No whole-frame supersampling or additional framebuffer is used.
- The generated rig has 190 points, 59 curves and 15 paths. Its 118 debug
  tangents fit the explicitly sized cache. Navigation and touch coordinates
  retain the shared cardinal-rotation owner.
- The device demo opens in artwork mode. The lateral overlay button still
  exposes the skeleton and control handles.

## Evidence and limits

Static source review, authoring generation and the explicitly authorized
firmware build/flash completed. Host checks and device observations follow.
The Windows preview is a separate implementation; host images cannot prove
physical display appearance, timing or touch behavior.

Before changes, a 40-second COM10 reset/capture observed the previous firmware
in skeleton mode: steady readouts around 29.4 FPS and CPU1 53-56%, with artwork
raster samples around 3-4 ms. It also showed existing optional RTC/storage
errors, an I2S rate-conflict line and snapshot-log truncation. This is not a
matched artwork baseline and must not be used to attribute savings from the
new default overlay state.

The earlier documented artwork-only measurement was 29.4 FPS, CPU1 33%, and
approximately 4 ms of artwork rasterization. It is historical context, not a
fresh matched measurement for this finish. See `cody-render-performance.md`.

Coverage adds intersection and blending work, and the surface paths add
geometry. No zero-cost claim is made. Physical appearance, subpixel movement,
all rotations and sustained stack headroom require explicit device evidence.

The explicitly authorized `device.ps1 check -Check cody-poc` run passed all
27 checks on the 190-point revision, including analytic coverage, native-color
blend expectations, surface ownership/motion, face isolation and preview input
rotation. These are host checks, not physical display approval. The first
`cody-native` invocation stopped before compilation because no host compiler
was on PATH; Visual Studio Insiders MSVC was subsequently located. The freshly
authorized retry passed after initializing its x64 developer environment.
It covers analytic rectangle/concave coverage, opacity, cap gradients, tile
seams/sentinels, compact/full rendering, dirty reconstruction and native/host
ready geometry. This does not establish arbitrary-pose pixel equality between
the separate Python-double and firmware-float rasterizers. No compiler was
installed. Native report: `artifacts/checks/cody-native/report.json`.

### Current visual iteration and paused optimization

The full state preceding the PNG experiment was committed as `3b90f25a`.
After rejecting the added billows' weak relationship to the actual outline,
the user authorized a reversible PNG-plus-triangles experiment. It is isolated
on `codex/cloud-png-mesh-study` and enabled only with `-CloudPngMeshStudy`.
The built-in image generator supplied one transparent, face-free painted body;
the simulator binds its color and alpha to the same lobe-weighted triangle mesh.
The existing vector shader remains the default and the firmware is untouched.
See `assets/cody/experiments/png-mesh/README.md` for exact provenance, prompt,
commands, 99-vertex/138-triangle topology and the 320 x 220 prepared texture.
Static PNG, wireframe and a 32-frame synthetic motion GIF have been captured.
This establishes a visible host experiment, not measured device feasibility.
No new automated tests, firmware build or flash ran for the PNG trial.

The user subsequently prioritized visual quality over optimization, requested
broader/more numerous-looking cloud volumes and a high-sensitivity inertial
default, and made snapshot-only consumer access an explicit repository rule.
The new appearance is being reviewed in the simulator before another flash.
The broader opaque oval trial still read as applied spots, so a separate,
opt-in host material study explores continuous lobe shading and fine texture.
It is not firmware rendering parity and must be labelled as a study:

```powershell
.\scripts\device.ps1 simulator -CloudMaterialStudy -OutputPath artifacts/simulator/cloud-study.png
```

The initial study capture at animation time 1000 ms was
`artifacts/simulator/cloud-study.png`. After the user requested more simulator
iteration, the latest visually inspected capture is
`artifacts/simulator/cloud-study-billows-v5.png`, at the same animation time.
It combines 12 broad height volumes, 25 named secondary billows and three
irregular small caps per billow. Soft unions, floating-point height normals,
local occlusion and directional shadows replace the large smooth patches.
Fine relief is subdued across the face; highlights transition from warm ivory
above to cooler blue-white below. Deterministic native RGB565 dithering avoids
large palette bands. The original silhouette coverage is applied once;
facial geometry, navigation and touch remain unchanged.

Intermediate captures v2-v4 exposed overly quilted or spherical relief and
were refined rather than treated as accepted artwork. The v5 appearance is
still pending user approval; it does not reproduce all the painted curls in
the reference. Only host preview captures and source review ran for this
iteration. No new tests, firmware builds, flash or physical measurements ran;
this study has not been ported to firmware. Reproduce the latest preview with:

```powershell
.\scripts\device.ps1 simulator -CloudMaterialStudy -OutputPath artifacts/simulator/cloud-study-billows-v5.png -AtMs 1000 -LogPath artifacts/logs/cloud-study-billows-v5.log
```

The stronger compiled motion defaults are sensitivity 3.2, baseline 1.0 s,
translation gain 0.045, depth gain 0.009, spring 2 Hz and damping ratio 0.32.
Stress attack/release are 0.06/1.4 s; contour response gains are 0.95/0.70 and
transition time is 0.16 s. Existing excursion limits, sensor sampling, validity,
axis mapping and automatic demo startup remain unchanged. These are artistic
configuration values, not manufactured telemetry or NVS replacements. They
have not been flashed or physically approved at this point. They produce
inertial bounce rather than persistent gravity-driven sliding.

The touched-boundary bitmap and removal of redundant nonnegative `floorf`
calls were completed after the passing native check. Those latest raster edits
are not yet built, tested or measured; optimization work is now paused at the
user's request. Likewise, the 27-check host result above predates the newest
palette/shadow and motion-default edits, and must not be presented as validation
of that later revision.

The second flashed candidate (`407a6f650` ELF prefix, 1,719,440-byte image)
was captured for 60 seconds on COM10: steady CPU1 around 60-63%, CPU2 0%,
artwork raster 13-14 ms, and usually 29.2-29.4 FPS. It does not meet the
low-cost objective. Logs: `artifacts/serial/cloud-shadows.log`.

Cody already reads `DeviceSnapshotService::readMotionPowerObservation`; it
does not call a sensor driver. Accelerometer and gyro are both published by
the canonical IMU owner, but the current response adapter consumes acceleration
only. Source review shows a nominal 100 ms active-motion acquisition interval,
separate from the 1000 ms full-snapshot scheduler; live cadence was not measured.
Keeping translation/depth limits unchanged is not a proof of navigation
clearance at the existing scale. Review that envelope before treating stronger
IMU response as physically validated.

### Pearl-jelly native port (2026-10-03)

The user selected concept 08 and explicitly requested the C++ port, release
build and flash after the host preview. The face-free source lives at
`assets/cody/experiments/jelly-scanline/jelly-body-master.png`; its README records
both imagegen prompts and the generation command. The 320 x 233 prepared
texture occupies 298,240 flash bytes as premultiplied RGBA8 with RGB565-quantized
source colors. It needs neither an on-device PNG decoder nor removable storage.

`CODY_jelly.cpp` derives 33 row controls from the existing named lobe anchors,
then prepares Q16 row sampling in the caller-owned PSRAM scene. Integer bilinear
filtering deforms both painted shading and alpha together. The face remains
vector artwork, and can now receive the exact forward body warp through a
runtime preference; navigation always remains independent. Dirty planning retains both previous and
current sprite footprints. No direct sensor access or snapshot cadence change
was introduced. The active-motion target is now 40 ms / 25 FPS; the HUD still
reports measured throughput rather than the target.

The authorized `cody-native` check passed using MSVC x64, including tile/dirty
reconstruction, alpha samples, invalid-map rejection and a 72-pose full-range
contour sweep. The first firmware build then found Xtensa's distinct `int32_t`
and `uint32_t` template deduction against ordinary integer literals. Explicit
32-bit template arguments fixed that compile error without changing sampling
arithmetic. The user requested no further tests; native checks were not rerun.

The final `device.ps1 flash -Profile cody-companion -Port COM10 -Jobs 8`
succeeded using ESP-IDF 6.0.2 and the performance-optimized configuration.
The application is 2,026,416 bytes (`0x1eebb0`), version `f1970dac-dirty`.
Esptool verified each written image's hash and issued the hardware reset.
No erase-all, serial monitor/capture, physical visual inspection or performance
measurement followed. Device appearance, motion and sustained performance must
not be inferred from host output or successful flash.

Final log: `artifacts/logs/cody-jelly-release-flash.log`.
The mandatory transient cleanup was preceded by a recoverable evidence copy at
`artifacts/build-cody-companion/pearl-jelly-evidence-20261003-214526/`, including
native reports, prior previews and the first build-failure log. Simulator and
imagegen previews were restored to their original artifact paths after flashing.

### Default selection, face preference and reported freeze

The user approved the pearl finish and requested a configurable rendering mode,
then specified that it remain enabled by default for now.
`CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY=y` selects it; disabling that option
excludes the sprite data and jelly caches and retains the existing vector body.
`CONFIG_LUNASHELL_CODY_FACE_FOLLOWS_BODY=y` supplies the current boot default.
The runtime setter/getter in `CODY_companion.h` lets a future user-preference
owner switch the face without rebuilding firmware. The choice is session-only,
uses the display/state lock, and never persists or repairs settings implicitly.
The simulator has an equivalent checkbox in its Face tab.

Facial cubics are sampled before applying the forward row mapping. Their
vertices, world-space anchors and handles share the same deformation as the
painted body; culling bounds are recalculated from the warped vertices. Facial
expression channels remain independent inputs, and navigation is not warped.
Paused switching rebuilds the saved pose without advancing clocks or sampling
new telemetry. Thirty-three retained destination supports cost 396 bytes per
scene in PSRAM, separate from the existing inverse pixel rows.

The first requested serial capture began after the reported freeze. The MCU
was still publishing snapshots at uptime 275-365 seconds, with DISPLAY and
TOUCH in error, low CPU and no allocation failures. The triggering failure
line was not in that capture, so its original error/call site is unknown.
After the authorized diagnostic reset, a 120-second boot capture and a later
240-second capture showed the renderer active through uptime 487 seconds,
with DISPLAY and TOUCH ready and no recorded consumer stop or panic. The gap
between captures was not continuously observed. This does not establish a fix
or exclude an intermittent failure. Logs are `artifacts/serial/cody-jelly-crash*.log`.

The captured older firmware measured roughly 17-18 FPS, 55-56 ms mean frame
time, 36-40 ms artwork raster, core 0 utilization around 82-84%, and core 1
around 0%. These measurements precede the new face deformation; the 25 FPS
period remains a target, not achieved throughput. Optimization is deferred.
No further test suites or simulator checks were run, as explicitly requested.
The default/face revision was then built and flashed successfully on COM10 with
ESP-IDF 6.0.2, performance optimization and at most eight compiler jobs.
The new application is 2,027,936 bytes (`0x1ef1a0`), still labelled
`f1970dac-dirty` because the requested commit follows the flash. Esptool verified
the written hashes and reset the board. No new serial capture, physical visual
inspection, runtime flag exercise or sustained-performance measurement followed
this latest flash. Build/flash success is not a stability claim.

Final log: `artifacts/logs/cody-pearl-face-release-flash.log`. Before regenerating
the profile, previous evidence and all diagnostic captures were copied to
`artifacts/build-release-evidence/pearl-face-20261003/`. This archive survived
the mandatory transient cleanup; serial and visual artifacts were restored to
their original paths afterward. No versioned artwork was deleted.

### First device candidate (superseded)

The first build/flash succeeded on COM10 (`e6061d76-dirty`, ELF prefix
`a5d23b967`, application 1,718,832 bytes). A 60-second reset/capture observed
artwork mode at roughly 29.0-29.4 FPS, CPU1 63-65% after startup, and 14-15 ms
of artwork rasterization. This cost is too high for the requested lightweight
finish. The simulator also exposed conspicuous horizontal RGB565 bands and
closed inset patches. The next revision softens the palette, simplifies the
relief and narrows AA edge traversal per row. After seeing the first preview,
the user requested more local shadows, so three tapered lobe-junction pockets
were added around the bright face; the first result is not the
accepted performance target. The standard build cleanup removes transient
logs, so these observations are retained here independently of those files.
