# Pearl jelly / scanline experiment

This opt-in Windows simulator mode applies the user's selected
**08 / GELATINA PERLATA** concept to Cody. It replaces only the cloud body and
four vector relief paths. The face remains vector artwork but follows the same
body warp by default; navigation stays independent. The Windows mode is opt-in.
The firmware renderer is selectable, currently enabled by default at the user's
request. Nothing in the artwork renderer reads physical sensors.

## Artwork and provenance

`jelly-body-master.png` is a real-alpha, face-free body sprite generated on
2026-10-03 with the built-in `image_gen` tool, following the imagegen skill.
The user's concept image supplied the shape, milky aqua/mint crown,
lavender/pink base and broad creamy highlights. The first edit removed the
face, text and black matte; a second edit recovered transparent framing around
the complete silhouette. The final image is stored here, not loaded from the
tool's output folder.

The source is decoded once, cropped to alpha >= 64 with four source pixels of
padding, and resized with premultiplied alpha. This master's crop is
`(28, 206, 1229, 1082)`; the runtime texture is **320 x 233**.
Colors are quantized to RGB565 while alpha remains eight-bit.
The native port keeps premultiplied RGBA8 after RGB565 source quantization:
298,240 bytes (291.25 KiB) in internal flash, plus binding tables. Eight-bit
premultiplied channels preserve faint alpha-edge colors during filtering.
The Python process separately retains Pillow buffers.

## Deformation contract

- 33 named row controls have precomputed weights to the existing contour-lobe
  anchors and pinned center. Each row stores its fixed source Y coordinate.
- Per frame, the evaluated rig provides a destination Y and left/right extent
  for each control row. Intermediate display rows interpolate those controls.
- Inverse row sampling stretches or compresses the source horizontally and
  vertically, using premultiplied bilinear filtering and the same texture alpha
  for the visible contour. There is no independent vector body clip.
- Highlights and shadows are baked into the sprite, so they deform with the
  surface. No dynamic lighting, refraction, blur or height field is calculated.
- Facial expressions still use the existing independent channels. With the
  runtime face flag enabled, sampled facial curves and their debug/touch points
  then receive the exact forward row warp used by the texture. Disabling the
  flag retains the previous independent facial geometry. Navigation never warps.
- Non-finite, collapsed or vertically folded mappings are errors.
- This is deliberately a row-warp study, not a triangle mesh. It cannot move
  two interior points on the same scanline independently; stronger local shear
  may require a mesh or more specialized deformation.
- The host uses Pillow affine row sampling; the native port uses Q16 source
  coordinates and integer premultiplied bilinear filtering. The same master
  and bindings feed both, but pixel-for-pixel Pillow parity is not claimed.
  The first device capture reports roughly 17-18 FPS, not the 25 FPS target.
  Performance optimization is explicitly deferred until the artwork is settled.

## Firmware selection and runtime face flag

`CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY=y` selects this material for the Cody
profile. It is enabled by default for the current visual iteration. Set it to
`n` in `config/sdkconfig/v1/profiles/cody-companion.defaults` to select the
existing vector renderer instead; regenerate that profile through
`device.ps1 set-target -Profile cody-companion` before the next authorized build.
CMake then omits both the texture and the jelly mapping cache. That is a named
rendering choice, not a recovery path after a failed frame.

`CONFIG_LUNASHELL_CODY_FACE_FOLLOWS_BODY=y` supplies the initial face preference
on each boot, also enabled by default. After the consumer starts successfully,
call `CODY_companion_set_face_deformation(true)` to follow the body or pass
`false` to retain the independent face. Read the requested preference through
`CODY_companion_get_face_deformation(&enabled)`. Both return `esp_err_t`; callers
must handle the real error. A build without jelly returns `ESP_ERR_NOT_SUPPORTED`.

The setter queues a redraw under the display/state lock; only the worker changes
geometry. Switching while paused rebuilds the saved pose without advancing
animation clocks or observing new sensor values. A parked display redraws when
the normal display owner permits rendering. The getter reports the requested
choice, not a claim that DMA has already presented it. The flag is session-only:
there is no implicit NVS write, settings migration, new device settings screen,
or restart of a failed consumer. A future preference UI can call this API.

The interactive simulator exposes the same choice in Face ->
`Deform face with jelly body`, checked initially. Synthetic input remains
visibly labelled; simulator speed is not device performance.

## Reproduce through the public simulator CLI

Interactive preview:

```powershell
.\scripts\device.ps1 simulator -CloudJellyStudy
```

Still image and animated preview:

```powershell
.\scripts\device.ps1 simulator -CloudJellyStudy -OutputPath artifacts/simulator/cloud-jelly-pearl-v1.png
.\scripts\device.ps1 simulator -CloudJellyStudy -OutputPath artifacts/simulator/cloud-jelly-pearl-motion-v1.gif
```

The interactive mode starts on the synthetic contour controls with overlays
hidden, amplitude 0.9 and speed 0.4. Its host target cadence is 25 Hz; that is not
a measured device FPS. Synthetic status and unmeasured device performance are
labelled in the window.

The GIF contains 80 frames at 40 ms per frame: a 3.2-second loop encoded for
25 FPS playback, not a rendering benchmark. It uses a stationary root and a
synthetic contour sweep (amplitude 1, fullness +/-0.8); it does not replay
recorded IMU data or characterize production response. A full-color four-pose
PNG is saved beside it. GIF palette reduction can change subtle pearl colors;
use PNG captures when judging color. `-AtMs` and `-SafeGuide` are not accepted
for this fixed GIF timeline.

This mode is mutually exclusive with the painted PNG/triangle mode and the
procedural cloud-material mode. The previous experiments remain available.

## Native firmware integration

```powershell
.\scripts\device.ps1 generate -Generator cody-jelly
```

The generator writes the immutable texture and named lobe/row binding tables
under `generated/`. CMake links these into internal flash: no SD, PNG decoder,
texture allocation or filesystem read is needed on the device.
`CODY_jelly.cpp` prepares clipped logical row mappings once per evaluated frame
in the existing caller-owned PSRAM scene, then draws into the ordinary compact
RGB565 tiles. The face is composited afterward and the physical owner applies
the existing shared cardinal rotation. The extra row cache is about 7.3 KiB
per scene; there are current and last-presented scenes.

The dirty planner compares source mappings as well as extents and redraws
both old and new texture footprints. This is necessary even when moving
highlights leave the bounding rectangle unchanged. Vector body/relief paths
remain available to the rig inspector but are not painted over the texture.
Invalid or folded row maps fail with their real error; there is no silent
vector fallback. The existing snapshot-driven motion owner is unchanged.

The firmware target period is 40 ms (25 FPS), not a throughput assertion.
The active release configuration uses `CONFIG_COMPILER_OPTIMIZATION_PERF=y`.

## Evidence and remaining work

The requested PNG/GIF visual captures completed, and the still/pose sheet were
visually reviewed during the initial host trial. After the user explicitly
requested the port, final native checks, build and flash were authorized;
their results are recorded in `docs/architecture/cody-cloud-finish.md` once
completed. Physical visual approval, wider rotation coverage and real ESP32
performance measurement remain separate from successful compilation or flash.
Firmware sensor consumers use only the canonical device snapshot.
The final default/face revision was built and flashed on COM10 without new test
runs, as requested. Physical runtime switching and latest-firmware stability
are not claimed from that successful flash; the reported intermittent stop
remains unresolved in the diagnostic evidence.

## Exact image-generation prompts

### 1. Face removal and transparent body

Use case: precise-object-edit. Asset type: production transparent PNG body sprite for the Cody cloud simulator. Edit the provided GELATINA PERLATA concept. Preserve the exact recognizable cloud outline, proportions, lobe arrangement, milky mint/aqua crown, opalescent pale lavender lower lobes, subtle pearl-pink reflections, and broad creamy specular highlights following the outer lobes. Change ONLY this: remove all eyes, eyebrows, nose, mouth and both coral cheek circles, seamlessly reconstructing the same smooth milky jelly surface where the face was; remove all text and the solid black background. Return ONE isolated cloud body, genuinely transparent outside the contour, no checkerboard, no black matte, no labels, no face, no props. The inner body must remain milky and opaque enough to receive separately drawn facial features; do not make the body itself disappear through transparency. Keep every major highlight, shaded lobe and the small lower feet from the reference, with clean antialiased alpha edges and no external glow or drop shadow. Frame the complete unchanged body centered with modest transparent margins. This is texture extraction and face removal, NOT a redesign.

### 2. Complete silhouette and padding

Use case: precise-object-edit. This image is already the desired face-free pearlescent jelly cloud body. Change ONLY framing: place the COMPLETE same cloud, including every left/right lobe and both small lower feet, fully inside a square transparent canvas with at least 8 percent of canvas width as transparent margin on EACH side. Reconstruct any tiny clipped left/right edge smoothly if necessary. Do not crop any contour, edge highlight or foot. Preserve exactly the body shape, colors, large creamy highlights, aqua crown, lavender base and smooth opaque milky center. Do not add face features, cheeks, text, border, shadow, black matte or checkerboard. Real alpha transparency outside the cloud. The output is the same body sprite, safely padded for deformation.
