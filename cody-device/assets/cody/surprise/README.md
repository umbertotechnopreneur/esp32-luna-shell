<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Surprise artwork

The three original `.rgb565` files are the preserved 466 x 466 big-endian
RGB565 masters. The matching PNG artwork is also retained unchanged.

The device embeds the generated `.indexed565` resources: a 256-entry RGB565
palette followed by one 8-bit index per pixel. The display draws each tile
directly from flash without a decoded framebuffer or heap allocation. The
conversion is lossy, so inspect its exact RGB565 previews under
`artifacts/cody-surprise-preview` before accepting a regenerated asset.

Generate the three resources from the repository root:

```powershell
pwsh -NoProfile -File .\scripts\device.ps1 generate -Generator cody-surprise-images
```

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
