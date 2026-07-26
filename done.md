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
- [x] Add an icon-only Maximum Debug Mode command. It atomically selects the
  fastest Full Snapshot and diagnostic cadences, raw IMU input, and SD log
  mirroring, with rollback if any setting cannot be applied.
