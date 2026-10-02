# Cody normalized cloud rig

`../cody.yaml` is the single authored configuration and geometry source. It owns
the character name, stable entity IDs, readable entity labels, named points,
paths, channel ranges and defaults, placement, boot-channel previews and style.
Paint roles resolve into the shared 256-slot palette; debug and Back colors are
also configured by role. Names use the viewer's
left and right. Local x points right and local y points down, with `center` at
(0, 0). Authored point offsets are relative to the named parent; moving an
anchor carries all descendant knots, handles and features. Translation and
uniform scale position the same front-facing character.

The source has 145 named points, 44 cubic curves and 11 paths. Semantic anchors
include `center`, `lobe.crown`, the other named lobes, `eye.left/right`,
`brow.left/right`, `cheek.left/right`, `nose` and `mouth`. Curve knots and handles
have stable names and never receive identities from runtime traversal order.
Curve segments refer to those points directly; the debug overlay traverses the
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
The 256-entry source palette stays unchanged; the framebuffer may contain
derived blend colors. Opaque host frames remain indexed, while translucent
host frames use RGB output quantized to RGB565 precision.

The separate `contour` configuration changes the cloud silhouette through its
eleven lobe anchors. Fullness, wave amplitude, cycles-per-second speed and
inter-lobe phase spread are continuous controls. Per-lobe coefficients preserve
the authored topology and move descendant handles together; facial anchors stay
independent. Target changes use a configurable smoothing time constant and a
continuously integrated phase. Pausing the contour freezes its state. Zero
fullness and amplitude preserve the original outline.

The simulator can overlay the deformed and original outlines, point positions,
curve handles and stable names. The reference outline shares the current
placement and breathing scale. It is a desktop inspection aid; firmware keeps
its existing bounded scene and strip rendering path without a text overlay.

Convert authored geometry and the shared 256-slot palette with:

```powershell
python -B .\scripts\generate_cody_rig.py
```

This asset conversion writes immutable flash tables and a source-hash manifest
under `generated/`. It does not compile or execute firmware. The Windows preview
reads the YAML directly; firmware reads the generated tables. Both evaluate
the same additive controls and use 12 samples per cubic. Host and device
evaluators are separate implementations, so runtime/pixel parity still needs
explicit validation. Geometry and overlay share one evaluated scene inside
each renderer.

The PoC uses bounded, non-antialiased RGB565 scanline rendering. It clips at tile
edges without resetting the gradient. Firmware uses one PSRAM scene and the
existing 466 x 8 strip, with no full-frame allocation. Layout and touch retain
the repository's common rotation owner. No runtime performance claim is made.

The elapsed-time animation composes a four-second breathing cycle with a smooth
280 ms blink every 4.8 seconds. Left/right eye openness remains independently
editable, including closed eyes while the mouth continues breathing. The clock
skips overdue frames; animation speed never depends on completed frame count.

Host PoC checks and preview evidence are recorded in the architecture document.
The initial ten-check result predates the contour extension; the extension's
prepared regression checks have not been run in this implementation step.
Firmware builds, physical behavior and ESP32 performance require separate
validation; desktop timings do not establish target performance.
