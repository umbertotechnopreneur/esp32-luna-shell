<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# BOOT menu artwork

`source/atlas.png` retains the authored four-cell master. Its cells are Settings,
Events, Counters and About, in reading order. The artwork is decorative and does
not determine availability or touch behavior.

Run `scripts/device.ps1 generate -Generator cody-boot-icons` to generate four
32 x 32 images with four-bit palette indices and RGB565 colors. Index zero is
transparent. Each image occupies 512 index bytes and 32 palette bytes in flash;
the renderer scales with nearest-neighbor and dims disabled icons.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
