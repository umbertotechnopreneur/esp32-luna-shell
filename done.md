# Done

Completed items moved out of `TODO.md` live here.

## Audio scope visualization

- [x] Improve FFT sensitivity by mapping the measured DFT range from -90 to
  0 dBFS, so quiet valid ES7210 captures are visible without synthetic gain.

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

## Internal memory and diagnostic sampling

- [x] Bound both the canonical logger ring and its snapshot scratch to 15
  complete lines, and move the permanent logger, SD storage, and diagnostic
  task stacks to PSRAM while retaining their FreeRTOS TCBs in internal RAM.
- [x] Publish raw internal/PSRAM heap block metrics and allocation-failure hook
  counters so fragmentation and failed requests remain explicit.
- [x] Reuse each fresh Diagnostic IMU or microphone sample at most once in the
  next Full Snapshot without changing its measurement timestamp, and gate the
  two diagnostic streams independently.

Device behavior for these changes has not yet been verified; completion here
records the implementation only.
