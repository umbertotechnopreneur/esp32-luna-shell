# Canonical device assets

This directory is the canonical repository for assets used by the round
ESP32-S3 device.

- `branding/LUNASHELL/source/` contains preserved source artwork.
- `branding/LUNASHELL/generated/` contains device-ready boot assets and previews.
- `mascot/source/` preserves the original character concept and the head layer.
- `mascot/generated/` contains the 170 x 198 head preview and BGRA8888 firmware sprite.
- `display/` contains the machine-readable round-display profile.
- `sfx/source/` contains only the selected, preserved source sounds.
- `sfx/generated/` contains normalized device-ready derivatives.

Do not edit generated files manually. Run:

```powershell
pwsh -NoProfile -File .\scripts\generate-device-assets.ps1
```

For SFX only, run `pwsh -NoProfile -File .\scripts\generate-sfx.ps1`.
For the idle mascot head, run `python .\scripts\generate_mascot_asset.py`
(requires Pillow). The face and articulated limbs are assembled from independent
GUI nodes; the head sprite deliberately has a blank display.
Both commands operate only on repository assets; neither initializes ESP-IDF,
builds firmware, or accesses a connected device. Generators reject source and
output paths that escape their canonical `source` and `generated` trees. Add
`-ValidateOnly` to the combined command for a preflight with no canonical output
writes. All three entrypoints acquire the same fail-closed
`.device-asset-generator.lock` in this directory for their full preflight and
commit window. The combined command passes its lock token to both child
generators, and every standalone generator acquires and releases the lock itself.
A stale lock is deliberately left visible for inspection; check `owner.json`,
retained staging, and package backups before any manual recovery. A normal
combined run verifies both plans before canonical writes and restores the whole
previous device-asset package if either generator fails. The SFX configuration
is parsed from one byte snapshot and bound to its SHA-256, so a concurrent change
aborts and rolls back outputs without replacing that external configuration
change. Each standalone generator also stages and verifies its outputs before a
manifest-last commit.

Fresh manifests include generator/tool provenance because byte-for-byte output
can depend on those versions. Existing schema-1 manifests remain valid and gain
the additive provenance fields on the next intentional regeneration.

The original external logo and SFX archives remain untouched. Only selected
inputs are copied here so every derivative can be regenerated and audited.
