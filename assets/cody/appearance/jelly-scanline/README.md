# Pearl-jelly artwork

The approved 08 / GELATINA PERLATA concept supplies the front-facing face-free
body. Highlights and shadows are baked paint and deform with texture alpha.
Expressions remain vector geometry; face-follow uses the same body warp.
The firmware finish is selectable and currently enabled by default; the host
study is opt-in. Runtime selection/persistence for all five skins exists in screen 11.

## Source and generation

`jelly-body-master.png` was generated from the supplied concept on 2026-10-03:
remove face/text/black background, then restore complete transparent framing.
Four texture-only variants in `skins/` reuse the master's exact alpha.
`skins.json` owns stable IDs, enum values, names and source paths.

One crop `(28,206,1229,1082)` and premultiplied-alpha resize produce 320 x 233
RGB565-quantized RGBA8 textures. Different canvas/mode/alpha fails rather than
silently inferring new geometry. Payload is 298,240 bytes per skin, 1,491,200
total flash bytes, excluding binding tables. Only the selected texture is sampled.

```powershell
.\scripts\device.ps1 generate -Generator cody-jelly
.\scripts\device.ps1 simulator -CloudJellyStudy
.\scripts\device.ps1 simulator -CloudJellyStudy -OutputPath artifacts/simulator/pearl.png
```

`generated/` contains immutable textures and named row/lobe bindings, not a
runtime PNG parser or SD dependency. Thirty-three weighted row controls feed Q16
inverse bilinear sampling; alpha, paint and facial geometry deform coherently.
Folded/nonfinite mappings fail without a silent vector fallback. Independent
motion of two interior points at the same y needs a different mesh.

`CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY` selects the build finish;
`CONFIG_LUNASHELL_CODY_FACE_FOLLOWS_BODY` selects the initial session preference.
Runtime `CODY_companion_set_face_deformation()` /
`CODY_companion_get_face_deformation()` preserve real errors and do not write NVS.
Disabling jelly excludes textures/mapping state. No sensor acquisition belongs here.

Current renderer/face/damage/memory contracts:
[rendering](../../../../docs/cody/rendering.md).
Selection/persistence: [onboarding](../../../../docs/cody/navigation.md).
40-ms scheduling and synthetic GIF playback are targets, not measured device FPS.
Host captures, flashing and old measurements do not validate current physical
output, performance or the unresolved display/touch stop.

## Exact image-generation prompts

### 1. Face removal and transparent body

Use case: precise-object-edit. Asset type: production transparent PNG body sprite for the Cody cloud simulator. Edit the provided GELATINA PERLATA concept. Preserve the exact recognizable cloud outline, proportions, lobe arrangement, milky mint/aqua crown, opalescent pale lavender lower lobes, subtle pearl-pink reflections, and broad creamy specular highlights following the outer lobes. Change ONLY this: remove all eyes, eyebrows, nose, mouth and both coral cheek circles, seamlessly reconstructing the same smooth milky jelly surface where the face was; remove all text and the solid black background. Return ONE isolated cloud body, genuinely transparent outside the contour, no checkerboard, no black matte, no labels, no face, no props. The inner body must remain milky and opaque enough to receive separately drawn facial features; do not make the body itself disappear through transparency. Keep every major highlight, shaded lobe and the small lower feet from the reference, with clean antialiased alpha edges and no external glow or drop shadow. Frame the complete unchanged body centered with modest transparent margins. This is texture extraction and face removal, NOT a redesign.

### 2. Complete silhouette and padding

Use case: precise-object-edit. This image is already the desired face-free pearlescent jelly cloud body. Change ONLY framing: place the COMPLETE same cloud, including every left/right lobe and both small lower feet, fully inside a square transparent canvas with at least 8 percent of canvas width as transparent margin on EACH side. Reconstruct any tiny clipped left/right edge smoothly if necessary. Do not crop any contour, edge highlight or foot. Preserve exactly the body shape, colors, large creamy highlights, aqua crown, lavender base and smooth opaque milky center. Do not add face features, cheeks, text, border, shadow, black matte or checkerboard. Real alpha transparency outside the cloud. The output is the same body sprite, safely padded for deformation.
