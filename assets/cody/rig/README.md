# Cody normalized cloud rig

`../cody.yaml` is the default authored skin. Another YAML below `assets/`
can be selected explicitly with the simulator's `-Skin` or the generator's
`-ToolArguments '--skin','assets/path/character.yaml'`. It owns
the character name, stable entity IDs, readable entity labels, named points,
paths, channel ranges and defaults, placement, boot-channel previews and style.
Paint roles reference unique named colors in the shared 256-slot palette.
Background, debug and Back colors, line widths and marker diameters are
configured in YAML. Numeric palette slots belong to the generated encoding. Names use the viewer's
left and right. Local x points right and local y points down, with `center` at
(0, 0). Authored point offsets are relative to the named parent; moving an
anchor carries all descendant knots, handles and features. Translation and
uniform scale position the same front-facing character.

The source has 190 named points, 59 cubic curves and 15 paths. Semantic anchors
include `center`, `lobe.crown`, the other named lobes, `eye.left/right`,
`brow.left/right`, `cheek.left/right`, `nose` and `mouth`. Curve knots and handles
have stable names and never receive identities from runtime traversal order.
Paths list their owned curve IDs; curve segments refer to point IDs directly; the debug overlay traverses the
same evaluated point arrays, including tangents and parent links.

Ten independent channels control left/right eye openness, left/right brow
tilt, mouth openness, a small breathing scale, mouth width, nose size, cheek
size and cheek opacity. These are geometric controls,
not sensor measurements or a complete emotional-state model. Channel changes
are additive point deltas before the shared transform. The `boot_channels`
entries are convenient rig previews; firmware boot retains the original three
indexed artworks and their actual startup checkpoints.

Declarative `face_transforms` scale descendant controls around fixed named
anchors; `path_opacity` binds the cheek fills to their shared opacity control.
Cheeks start at 1.2 times their authored size. `face_transition_seconds` controls
smooth target interpolation for sliders and presets, before breathing/blinking
are composed. `expression_presets.embarrassed` is a manual example using these
same controls. It does not infer an emotional state.

Partial cheek opacity uses real source-over blending in native RGB565 channels.
The source catalog retains 256 slots; the framebuffer may contain derived
coverage and opacity colors. The body uses warm ivory and cool blue-grey ramps,
with gentler coral cheeks. Separate opaque gradients shade the underside and
three local lobe junctions. Blue navigation and the boot artwork keep their
original colors.

Filled paths explicitly select `antialiasing: coverage` where needed. The body
and its four relief shapes use four vertical subscanlines and 1/256-pixel
horizontal coverage. Coverage is combined once with opacity in RGB565; full
interior runs still use the opaque span fill. The working mask is one bounded
row, not an enlarged framebuffer. Other paths retain pixel-center rasterization.
The host preview expands coverage-enabled frames to RGB565-quantized RGB.

An optional host-only material study replaces the flat body/surface paints with
one softly joined lobe height field, warm directional light, cool ambient
shadows and fine deterministic RGB565 dither. It retains the evaluated outline,
face and navigation, but is not implemented by the firmware rasterizer:

```powershell
.\scripts\device.ps1 simulator -CloudMaterialStudy -OutputPath artifacts/simulator/cloud-study.png
```

Its output is explicitly labelled `MATERIAL STUDY / HOST ONLY`; judge the visual
direction there before planning a device implementation or making CPU claims.

The newer opt-in `-CloudPngMeshStudy` experiment uses a painted transparent body
instead of procedural height shading. Its texture and alpha deform together
through triangles bound to the named lobe anchors; the face stays separate.
PNG stills and synthetic GIF loops are capture-only and are not firmware parity.
See [the experiment contract and prompt](../experiments/png-mesh/README.md).

The separate `contour` configuration changes the cloud silhouette through its
eleven lobe anchors. Fullness, wave amplitude, cycles-per-second speed and
inter-lobe phase spread are continuous controls. Per-lobe coefficients preserve
the authored topology and move descendant handles together; facial anchors stay
independent. Target changes use a configurable smoothing time constant and a
continuously integrated phase. Pausing the contour freezes its state. The selected contour path, named lobes
and per-lobe displacement limits are authored in the skin. Zero
fullness and amplitude preserve the original outline.

`contour.surface_paths` explicitly permits the relief shapes to inherit nearby
lobe motion. They own separate named curve controls and cannot borrow facial
geometry, visibility or opacity channels. Their opaque, inset gradients add
local volume without moving eyes or mouth.

The simulator can overlay the deformed and original outlines, point positions,
curve handles and stable names. The reference outline shares the current
placement and breathing scale. It is a desktop inspection aid; firmware keeps
its existing bounded scene and strip rendering path without a text overlay.

Convert authored geometry and the shared 256-slot palette with:

```powershell
.\scripts\device.ps1 generate -Generator cody-rig
```

This asset conversion writes immutable flash tables and a source-hash manifest
under `generated/`. It does not compile or execute firmware. The Windows preview
reads the YAML directly; firmware reads the generated tables. Both evaluate
the same additive controls and use 12 samples per cubic. Host and device
evaluators are separate implementations, so runtime/pixel parity still needs
explicit validation. Geometry and overlay share one evaluated scene inside
each renderer.

The PoC uses bounded RGB565 scanline rendering with per-path coverage selection.
It clips at tile edges without resetting gradients or subpixel coordinates.
Firmware draws compact tiles into the frame owner's existing DMA lease, with
no additional full-frame allocation. The PSRAM worker stack is 8 KiB to leave
room for bounded coverage scratch and nested display operations. Layout and
touch retain the repository's common rotation owner. The demo opens in artwork
view; its lateral control still exposes the complete skeleton. Runtime costs
require device measurements rather than inference from the algorithm.

The elapsed-time animation composes a four-second breathing cycle with a smooth
280 ms blink every 4.8 seconds. Left/right eye openness remains independently
editable, including closed eyes while the mouth continues breathing. The clock
skips overdue frames; animation speed never depends on completed frame count.

An earlier October 3 cloud-finish revision passed 27 host PoC checks and the
native C++ raster/geometry package, including coverage, tile seams and dirty
reconstruction. Subsequent artwork, motion-default and touched-event raster
edits still require fresh checks; optimization is paused while the user reviews
the appearance. Revision-specific evidence is recorded separately in
`docs/architecture/cody-cloud-finish.md`. Desktop timings do not establish target
performance or physical appearance.
