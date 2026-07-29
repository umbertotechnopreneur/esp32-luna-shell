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

## Declarative GUI document lifetime

- Keep `components/qsee_app/gui/root.json` as the single editable declarative
  source, but never ship it as one parsed runtime document. Generate a small
  resident bootstrap containing only the background, shared chrome, Welcome,
  and empty content target, plus one independent document per non-Welcome
  content screen.
- Release resident content before changing routes, clear the outgoing
  document's action subscriptions, unload it, and only then load and mount the
  requested document. Keep exactly one optional active route `DocumentId`;
  additional preloading or caching requires measured device evidence and an
  explicit bound.
- A load, subscription, mount, or unload failure keeps its real error. Clean up
  any partially loaded document and restore resident Welcome explicitly; never
  report the requested route as active after partial success.
- Log internal and PSRAM free/largest-block values around release, load, mount,
  and unload. Static checks can prove document ownership and the one-active
  policy, but only repeated physical navigation can establish fragmentation
  behavior.
- Treat `package/res/root.json` and `package/res/screen_*.json` as generated
  artifacts. Regenerate and verify the complete package whenever the canonical
  source changes so actions, templates, route descriptors, and staged files
  cannot drift independently.

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

## Internal-memory optimization invariants

- Keep firmware-owned values, counters, sizes, masks, and arithmetic 32-bit.
  Never introduce a 64-bit type merely to widen or compare native ESP32-S3
  data. Retain 64-bit values only where an ESP-IDF API mandates that boundary,
  such as `esp_timer` timestamps, and do not propagate them into unrelated
  state.
- Keep the permanent logger writer, removable-storage owner, and diagnostic
  monitor stacks in PSRAM while their TCBs remain internal. If a future stop or
  deinit is added, delete these tasks only with `vTaskDeleteWithCaps`, never
  `vTaskDelete`.
- Keep the logger ring and snapshot limit aligned at 15 complete lines so
  retained history and snapshot scratch follow one explicit contract.
- Diagnose fragmentation with raw internal/PSRAM heap totals, minima, largest
  blocks, and allocated/free/total block counts. The failed-allocation hook
  must remain cache-safe and must not allocate or log from inside the callback.
- A Full Snapshot may consume a newly committed Diagnostic IMU or microphone
  sample only once within its reuse window. Preserve the original measurement
  timestamp and mark it consumed only after the Full Snapshot commits.
- Gate Diagnostic IMU and microphone streams independently so a page never
  polls or claims an unrelated sensor stream. Before returning microphone
  ownership to the AFE, disable diagnostics under the shared refresh mutex and
  have the monitor recheck the flag after acquiring that same mutex.
- Derive measured-sample freshness from the active Full Snapshot cadence with
  bounded scheduling grace. A fixed TTL shorter than a supported cadence makes
  valid telemetry alternate between ready and stale. Preserve the attempt time,
  start freshness at successful completion, and when the monitor is paused,
  fall back to the short fixed TTL so old data still expires.
- Treat the PCF85063 OS bit as an integrity state, not malformed I2C data. A
  structurally valid reset calendar remains unavailable until SNTP or a valid
  GPS UTC fix writes a trusted epoch and clears OS.
- Keep timer cleanup ordered: `AppState::Stopping` blocks dispatch, the app
  releases its own IDs in `on_stop`, and System Core then cancels leftovers
  even after a failed stop. Backend-owned keyboard layout buffers must belong
  to their node record, never a process-lifetime accumulator.
- Release per-document maps in place during lazy unload. Copying long style and
  route keys into temporary vectors raises the allocation peak at the exact
  point where the runtime is trying to recover a large contiguous block.
- A signal disconnect must release its slot even if that signal never emits
  again. Counting only connected slots hides retained callbacks; remove the
  slot through weak shared state, but destroy its captures after releasing the
  registry lock so reentrant scoped-connection cleanup cannot deadlock.
- Provisioning activity is a lifecycle fact, not a display-state whitelist. A
  failed credential attempt can still own the BLE session; expose the running
  flag and stop that session before leaving the wizard or showing its result.

These are implementation invariants, not verified device-performance results;
runtime behavior still requires authorized hardware validation.

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
- Wi-Fi plus BLE provisioning can leave less contiguous internal DMA memory
  than one normal display slot even while PSRAM remains available. Size a
  transient low-memory slot from the largest current DMA-capable block, keep
  explicit allocator headroom, and split the same logical flush into shorter
  strips instead of letting the renderer enter an allocation-failure loop.
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
- Do not combine high-rate snapshots, high-rate diagnostics, global verbose
  logging, and SD mirroring behind one UI command. On this device that workload
  can keep the firmware alive while starving the GUI enough to look crashed.
  Expose bounded settings independently and use the serial monitor as the
  canonical crash source when SD is unavailable.

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

## AXP2101 PKEY and software standby

- The physical PWR control is the AXP2101 PKEY. GPIO41 is SDMMC chip select and
  must never be configured or polled as the power button.
- Keep PMIC IRQ registers owned by `qsee_hardware`: queue decoded PKEY events
  before a verified clear, and let the power worker consume them independently
  of the periodic Full Snapshot.
- PKEY polling must use a PMU-only mutex. A display replay or another long
  sampler operation must never hold the lock required to read the wake button.
  The transaction owned by that mutex must also have a real I2C timeout; a
  bounded mutex wait cannot rescue an unbounded XPowers transfer.
- Serialize brightness commands, panel on/off, and cardinal replay through the
  same display lock. If the panel is off, leave a requested rotation pending
  until wake instead of sending pixel transfers into a disabled controller.
- Always acquire the display lock before the brightness/settings mutex. If a
  standby or wake display transition fails after changing hardware state,
  attempt the inverse display transition before restoring the software state.
- This board has no verified ESP32 wake GPIO for the AXP2101 IRQ. PWR therefore
  toggles software Device Standby while its polling task remains alive; do not
  claim PWR wake from ESP light sleep without schematic and device evidence.
- A single noisy IMU sample must never reset the shared inactivity timeline.
  Require quiet re-arm, consecutive movement samples, hysteresis, and an
  explicit grace/cooldown around standby transitions.

## Snapshot-owned Fullscreen and adaptive content

- Manual Fullscreen is power-policy state, not a local opacity shortcut. Publish
  it with both arc targets in `DeviceSnapshotService`, and let every page derive
  its viewport from that one immutable state.
- A transparent wake surface may cover the screen for automatically hidden
  chrome or a dimmed/off display. It must not capture page touches while the
  display is active in manual Fullscreen.
- Disable Command Arc hit testing before its hide fade starts, and restore it
  only after the show fade completes. The viewport follows the canonical target
  immediately, so transition-time ghost buttons would otherwise overlap page
  controls.
- On the 466-pixel round display, reserve 73 pixels for each visible arc. Keep
  fixed diagnostic geometry centered inside the released space, and let
  scrollable content grow only inside the wide circular band instead of
  extending 300-pixel rows to the top or bottom edge.
- Arm BOOT actions only after a stable released interval at startup and after
  light sleep. A sampled-low boot strap or held wake press must not become a
  Fullscreen toggle or soft reset.
