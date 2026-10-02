# Cody normalized cloud rig

`cloud-rig.json` is the single authored geometry source. Names use the viewer's
left and right. Local x points right and local y points down, with `center` at
(0, 0). Translation and uniform scale position the same front-facing character.

The source has 145 named points, 44 cubic curves and 11 paths. Semantic anchors
include `center`, `lobe.crown`, the other named lobes, `eye.left/right`,
`brow.left/right`, `cheek.left/right`, `nose` and `mouth`. Curve knots and handles
have stable names and never receive identities from runtime traversal order.
Curve segments refer to those points directly; the debug overlay traverses the
same evaluated point arrays, including tangents and parent links.

Six independent channels control left/right eye openness, left/right brow
tilt, mouth openness and a small breathing scale. These are geometric controls,
not sensor measurements or a complete emotional-state model. Channel changes
are additive point deltas before the shared transform. The `boot_channels`
entries are convenient rig previews; firmware boot retains the original three
indexed artworks and their actual startup checkpoints.

Convert authored geometry and the shared 256-slot palette with:

```powershell
python -B .\scripts\generate_cody_rig.py
```

This asset conversion writes immutable flash tables and a source-hash manifest
under `generated/`. It does not compile or execute firmware. The Windows preview
reads the JSON directly; firmware reads the generated tables. Both evaluate
the same additive controls and use 12 samples per cubic. Host and device
evaluators are separate implementations, so runtime/pixel parity still needs
explicit validation. Geometry and overlay share one evaluated scene inside
each renderer.

The PoC uses bounded, non-antialiased RGB565 scanline rendering. It clips at tile
edges without resetting the gradient. Firmware uses one PSRAM scene and the
existing 466 x 8 strip, with no full-frame allocation. Layout and touch retain
the repository's common rotation owner. No runtime performance claim is made.

Implementation and asset generation only. Build, simulator checks and device
validation are deferred until explicitly requested.
