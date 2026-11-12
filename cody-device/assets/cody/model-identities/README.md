<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Model identity artwork

`source/` retains the generated Terra, Sol, Luna and Astra masters. They are
artistic Cody interpretations, not official model logos. The firmware selects
the images only for fresh exact model IDs: `gpt-6.1-terra`, `gpt-6.1-sol`,
`gpt-6-sol`, `gpt-6-luna` and `gpt-6-astra`. Unknown models have no background.

Run `scripts/device.ps1 generate -Generator cody-model-identities` to produce
the 64 x 64 RGB565 palette-indexed, four-bit packed firmware header. Index 0 is
transparent. The renderer expands using nearest-neighbor sampling and blends
the artwork at low opacity behind the existing foreground. The asset uses
2,048 bytes for indices and 32 bytes for the palette in flash per image.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
