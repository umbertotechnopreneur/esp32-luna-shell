# Lessons

## Brookesia startup overlay

- The `SuperOS` banner in the boot video came from Brookesia System Super's
  startup overlay, not from the QSee app.
- The visible image is
  `third_party/esp-brookesia/system/brookesia_system_super/resource/startup/images/background.png`,
  loaded through the System Super startup overlay.
- To remove that banner, disable
  `CONFIG_BROOKESIA_SYSTEM_SUPER_ENABLE_STARTUP_OVERLAY` instead of changing
  the QSee boot screen.
- The QSee boot image and the Brookesia startup overlay are separate stages, so
  check both whenever a future video shows two branded frames.

## Single-app Brookesia runtime

- System Core is the application/runtime layer. System Super adds an optional
  Shell application with launcher, global overlays, keyboard, and dialogs.
- QSee is the only application on this device, so use System Core directly and
  do not stage, install, or start the System Super Shell.
- Removing the Shell also removes its keyboard and message-dialog
  implementations. Product flows that need them must use explicit QSee-owned
  declarative components; do not silently ignore those requests or reintroduce
  the Shell as a fallback.

## ESP-IDF 6 flash operations and PSRAM task stacks

- ESP-IDF 6 asserts if a task whose stack is in PSRAM performs an operation
  that disables the flash/PSRAM cache, such as an NVS commit.
- Keep Brookesia System Core's three 64 KiB scheduler workers in PSRAM. App
  startup synchronously posts GUI work, so one worker deadlocks waiting for its
  own scheduler. A 32 KiB internal Core worker also does not fit reliably after
  display and hardware startup because the remaining internal heap is
  fragmented.
- Route QSee settings through its fixed internal-stack NVS executor. Keep
  Brookesia's general service workers in PSRAM and use one internal secondary
  scheduler worker for the Storage service.

## Cardinal rotation rendering

- The CO5300/CST9217 cardinal transform keeps LVGL in the same logical
  466 x 466 coordinate system. Invalidating the active screen after changing
  the panel scan direction forces an unnecessary full software redraw of every
  declarative gradient, shadow, border, and glyph.
- Keep the last successfully flushed RGB565 frame in PSRAM and replay it in
  bounded QSPI strips after the hardware transform. Fall back to a full LVGL
  invalidation only before the cache has observed a complete frame.
- Removable SD is not a render cache: filesystem latency, bus contention, and
  media wear make it unsuitable for rotation-critical pixels.

## Partial-render scrolling

- A 10-row partial buffer makes slow software rasterization visible as scan
  bands during scrolling, even when QSPI itself is already configured at
  40 MHz with a queued double-buffer pipeline.
- ESP-IDF 6 exposes direct PSRAM DMA for SPI LCD color buffers, but the CO5300
  QSPI path on this board underflows during the first LVGL flush and leaves a
  corrupted frame. Use tall PSRAM raster tiles only behind a small internal DMA
  bounce buffer.
- Keep the bounce buffer small, internal, and transient for both normal flushes
  and rotation replay. A persistent 10-row staging buffer fragments enough
  internal RAM to prevent Brookesia Storage from reading the app JSON.
- A transient two-slot pipeline can overlap CPU copies and panel transfers
  without reserving internal RAM permanently. Align every slot independently
  and log a one-slot retry when fragmentation prevents the optional second
  slot.
- One LVGL flush may be split into several internal-DMA panel transfers, but it
  still has exactly one logical completion. Intermediate color-transfer ISRs
  must not release LVGL's draw buffer; signal flush-ready only after the
  synchronous bounce callback has drained every strip.
- Do not leave an expensive full-screen vector gradient below an opaque
  rasterized background. LVGL can repaint both layers for every dirty scroll
  area even though the lower layer is never visible.
- Declarative binding batches must compare values in the data store before
  applying them to nodes. Reapplying unchanged telemetry still invalidates
  styles and labels, making an otherwise static scroll appear to repaint in
  narrow bands.

## Mutually exclusive runtime profiles

- Performance Mode is session-scoped suppression and must not overwrite the
  user's saved service settings.
- Maximum Debug Mode deliberately changes persistent diagnostic settings. Apply
  its preset as one fail-fast transaction and roll back prior values if any
  individual setting fails.

## Snapshot-owned adaptive chrome wake

- Touch and motion publish activity intent only. They must not optimistically
  change Status Arc, Command Arc, or display-idle state in the UI because the
  preceding snapshot can briefly reverse that local change and make both arcs
  flash.
- Route changes preserve the Command Arc visibility target from the latest
  canonical snapshot. Reconfiguring commands must not reset opacity or hit
  testing to visible.
- An active power policy must agree with the display mode and applied brightness
  published by the display owner. Treat a mismatch as an incomplete wake and
  restore the configured brightness before publishing the active state.
