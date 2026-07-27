# Done

Completed items moved out of `TODO.md` live here.

## UI rotation and Performance Mode

- [x] Cache the last complete RGB565 frame in PSRAM and replay it after a
  cardinal panel/touch rotation instead of invalidating and rasterizing the
  entire LVGL tree.
- [x] Add the Settings Command Arc Performance Mode action. It pauses effective
  SD log writes, periodic Full Snapshots, and the optional voice AFE while
  preserving saved preferences and keeping connectivity and sensor services
  available.
- [x] Enable bounded LVGL shadow and circle caches for repeated chrome drawing.
- [x] Replace the visible 10-row scrolling tiles with 64-row double buffers in
  PSRAM, copied through one transient 4-row internal DMA buffer before QSPI.
- [x] Retire the icon-only Maximum Debug preset after device monitoring showed
  that its 100 ms diagnostics, 300 ms Full Snapshots, global verbose logging,
  and unavailable SD mirroring could starve the UI. Keep each diagnostic
  setting explicit and independently selectable instead.
