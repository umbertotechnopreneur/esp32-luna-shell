<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# VibeWare boot identity

The approved transparent floppy-insertion GIF is preserved unchanged in
`source/`. It comes from the VibeWare Obsidian press kit, under
`20_Execution/Projects/VibeWare/press-kit/assets/boot/`.

Regenerate with the sole public CLI:

```powershell
.\scripts\device.ps1 generate -Generator vibeware-boot
```

The package retains all 42 original frames and their 80/90-ms durations:
3,500 ms of authored dwell per cycle. Transparent pixels are composited against
black. The common visible bounding box is enlarged exactly twice with nearest
neighbor sampling, yielding 256 x 292 pixels at (105, 87), wholly inside the
central safe square. No colors are quantized beyond the display's RGB565 format.
Identical images share immutable palette/RLE storage. Current pixel resource data
occupies 630,600 bytes; frame metadata, linked section alignment and runtime costs
need an authorized firmware build/size and physical review.

Cody holds the first frame while services start. The Ready checkpoint then
replays the complete cycle from frame zero through the native DMA owner, with
the original GIF dwell after each frame retires. The first interactive route
opens after the cycle. Deep-sleep and software standby wake omit the introduction.

Luna uses its existing LVGL owner with the same flash data and a 149,504-byte
PSRAM image buffer. It loops while services initialize and hands off to System
Core after the current cycle. The source GIF is never decoded on the device;
neither renderer depends on removable SD or a new LVGL/GIF adapter.

The Windows previews use the preserved GIF, generated placement/timing and exact
RGB565 color expansion. They remain visibly synthetic. The provided manifesto
reference is [VibeWare manifesto](https://umbertogiacobbi.biz/vibeware/manifesto);
its content is not quoted or inferred by this feature.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
