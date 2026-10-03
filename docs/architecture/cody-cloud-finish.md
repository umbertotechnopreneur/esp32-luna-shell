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
