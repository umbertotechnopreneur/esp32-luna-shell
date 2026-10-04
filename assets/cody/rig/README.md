# Cody authored rig

`../cody.yaml` is the canonical front-facing character graph: named entities,
points, curves, paths, palette roles, face/contour controls, response tuning and
default placement. Names and typed IDs are stable; array order is not identity.
Local x points right, local y down; parent-relative anchors move descendants.

## Ownership

- Ten face channels and four contour controls share evaluated geometry.
  Fullness/amplitude zero preserves the authored outline; phase remains continuous.
- Explicit face anchors, path opacity and relief bindings preserve topology.
  Palette indices are generated encoding; authoring references named colors.
- Breathing/blinking use elapsed active time, not completed frame counts.
- Firmware consumes immutable flash tables; the host reads authoring data and
  uses visibly synthetic inputs. Neither path creates another hardware owner.
- Coverage-enabled vector paths use bounded row scratch; partial alpha may create
  RGB565 colors outside the source palette. Pearl artwork is a separate selectable
  finish, not dynamically generated lighting.

## Generation and studies

```powershell
.\scripts\device.ps1 generate -Generator cody-rig
.\scripts\device.ps1 simulator
```

An explicit alternate authored skin can be selected with `-Skin` or generator
`-ToolArguments @('--skin', 'assets/path/character.yaml')`. Outputs and the
normalized-source manifest live in `generated/`; do not hand-edit them.
Asset preparation does not execute firmware or prove pixel/performance parity.

`-CloudMaterialStudy` and `-CloudPngMeshStudy` are opt-in host-only studies;
their visual output is not a native implementation claim. The
[PNG mesh study](../appearance/png-mesh/README.md) retains its own provenance.
Current rig, response, AA, pearl and memory contracts are in
[rendering](../../../docs/architecture/cody/rendering.md); open evidence is in
[TODO](../../../TODO.md).
