# Canonical device assets

This directory is the canonical repository for assets used by the round
ESP32-S3 device.

- `branding/LUNASHELL/source/` contains preserved source artwork.
- `branding/LUNASHELL/generated/` contains device-ready boot assets and previews.
- `display/` contains the machine-readable round-display profile.
- `sfx/source/` contains only the selected, preserved source sounds.
- `sfx/generated/` contains normalized device-ready derivatives.

Do not edit generated files manually. Run:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 assets
```

The original external logo and SFX archives remain untouched. Only selected
inputs are copied here so every derivative can be regenerated and audited.
