# Lessons

Concise engineering invariants learned from this board and runtime. These are
not substitutes for authorized device validation. Open work belongs in
`TODO.md`, not here.

## Build profiles and resources

- Debug/Release is a separate axis from the hardware graph. Default to Debug;
  require explicit Release, isolate each mode's sdkconfig/build state and retain
  the mode in recovery commands. Existing Release artifacts never imply intent.
  Debug keeps USB awake; Release permits sleep with the unsafe USB SOF hook still
  disabled. Source policy alone does not establish electrical savings.

- ESP-IDF resolves components before project Kconfig. Each legal
  board/profile/feature variant needs a pre-Kconfig catalog of admitted roots,
  local component directories, defaults, and a reviewed dependency lock.
  Conditional `idf_component_optional_requires` cannot discover a component
  omitted from that graph.
- Copy the variant lock into its build directory before configuration and fail
  on drift. Keep fresh build directories empty until ESP-IDF creates CMake
  state; `fullclean` applies only to a recognized non-empty build tree.
- Gate optional SDK headers and implementations at compile time for reduced
  profiles. Keep C++ exceptions enabled where allocation errors are caught and
  mapped to `ESP_ERR_NO_MEM`.
- Leg Edition selects the ES8311 output but does not inherit the full profile's
  boot-audio default. Enable `CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO` explicitly
  in its profile to embed and play the startup WAV; codec initialization alone
  does not start playback.
- Generated packages and LittleFS images need explicit Ninja inputs, outputs,
  stamps, and a build-local staging root. A source-tree-global Brookesia stage
  lets one profile silently rewrite another's app resources.
- Keep UI hot-path caches in the `App` lifecycle, release them in `on_stop`,
  and precompute invariant FFT paths, keys, and palette values once.

## Brookesia System Core and declarative GUI

- LUNASHELL is the only native app. Use System Core directly; do not stage or
  start System Super. Its keyboard/dialog services therefore need product-owned
  declarative replacements. The historical `SuperOS` frame belonged to System
  Super's startup overlay, not the product boot image.
- `components/LUNASHELL_app/gui/root.json` is the only editable GUI source.
  Generate a small resident background/chrome/Welcome bootstrap plus one
  independent document per other screen. Treat packaged JSON as generated;
  regenerate and verify every artifact after editing the canonical source.
- Keep one optional active route document. Before changing routes, remove its
  action subscriptions, unload it, then load and mount the new document. On
  failure, keep the real error, clean partial state, and explicitly restore
  Welcome. Hardware navigation must establish fragmentation behavior; static
  ownership checks alone cannot.
- Intern only static styles. Binding-driven opacity and transform variants
  belong to their node, not a document-lifetime intern pool.
- The current Brookesia style schema does not support `bgOpacity`; remove
  `bgColor` to make a declarative node transparent. An LVGL dropdown opens
  its options in a separate screen child, so its popup background needs an
  explicit backend style without changing option text or geometry.
- Declarative binding batches write changed final values silently, apply
  coalesced backend masks, then notify subscribers after backend state is
  coherent. Skip no-op values, use an inline changed-index bitmap, and keep
  missing targets and failed bindings visible.
- Per-node keyboard layout buffers must die with their node. Lazy unload must
  release maps in place, without allocating a second copy of long keys. A
  disconnected signal must release captures even if it never emits; destroy
  callbacks outside the registry lock to avoid reentrant deadlock.

## Flash/cache safety and memory

- An ESP-IDF 6 task with a PSRAM stack cannot safely perform a flash/cache-
  disabling operation such as NVS commit. Keep the three 64 KiB System Core
  workers in PSRAM, but route settings writes through the fixed internal-stack
  NVS executor and Storage work through one internal secondary worker. One
  Core worker deadlocks on synchronous GUI posts; a 32 KiB internal worker
  does not fit reliably after hardware startup.
- Keep firmware-owned counters, sizes, masks, and arithmetic 32-bit except at
  explicit 64-bit ESP-IDF API boundaries. Logger, SD owner, and diagnostic
  monitor stacks remain in PSRAM; delete such tasks with `vTaskDeleteWithCaps`.
- Keep logger ring and snapshot scratch aligned at 15 complete lines. For
  fragmentation evidence, publish raw internal/PSRAM free, minimum, largest
  block, and allocation-block counts. The allocation-failure hook must stay
  cache-safe and must neither allocate nor log.
- A Full Snapshot may reuse each freshly committed diagnostic IMU or mic
  sample only once, preserving its original timestamp. Freshness follows the
  active cadence plus bounded grace, then reverts to a short TTL when paused.
- Retained component failures remain real aggregate errors. Coalesce only
  duplicate scheduler logs (new error immediately, counted reminder every 60
  seconds, recovery immediately); do not slow healthy samplers.
- Gate diagnostic IMU and microphone streams independently. Before returning
  mic ownership to AFE, disable diagnostics under the shared refresh mutex and
  recheck the flag after that mutex is acquired.
- A valid PCF85063 calendar with its oscillator-stop bit set is unavailable
  until trusted SNTP or GPS UTC writes a new epoch and clears the bit.

## Display, rendering, and radios

- `network_provisioning` 1.2.4 emits SET before its Wi-Fi configuration write;
  CRED_RECV follows the accepted write and delayed connect timer. Adopt Wi-Fi
  connection intent from CRED_RECV, so first-boot GOT_IP is not discarded.
  Leave provisioning retries to its manager and suppress the station owner's
  reconnect timer until ownership is released. Never issue a second connect.
- Request admission is not pairing completion. Keep Start/Retry/Cancel away
  from the display worker, bind requests and QR copies to typed attempt/session
  identities, and navigate after observed cleanup. A stored SSID or credential
  event does not prove a current Wi-Fi lease.

- Display/touch selection and the Luna GUI are separate compile-time owners.
  UI-off consumers keep the mandatory physical devices through the standalone
  BSP path. Their frame and input lifecycle must use hardware APIs rather than
  an LVGL worker. Changing manifest gates requires a reviewed lock refresh;
  static source review is not dependency resolution or device validation.

- Cardinal rotation keeps LVGL at 466 × 466. Replay the last complete RGB565
  frame from PSRAM in bounded QSPI strips; only before that cache exists should
  rotation force a full LVGL redraw. Never use SD as a render cache.
- The CO5300 QSPI path underflows with direct PSRAM DMA on this board. Render
  into tall PSRAM tiles, then use a small transient internal-DMA bounce buffer.
  Split a logical flush into shorter strips when the largest DMA block is
  small; signal LVGL flush-ready only after the final strip.
- A transient two-slot pipeline can overlap copies and transfers. Each slot
  needs independent alignment and a logged one-slot fallback. Do not keep a
  permanent staging buffer that fragments RAM needed for Brookesia Storage.
- Before Wi-Fi, reserve the BLE controller's contiguous 0x7800-byte internal-
  DMA block; release it just before BLE startup. Reject the radio action with
  `ESP_ERR_NO_MEM` if post-Wi-Fi headroom fails. BLE provisioning needs two
  controller activity slots: advertising and connection.
- Keep `CONFIG_ESP_PHY_MAC_BB_PD=n` on this board: the controller requests
  unavailable retention RAM. Keep `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=n` on
  ESP-IDF 6.0.2: its USB SOF PM-lock hook caused cache-error reboots during
  flash operations. Native USB Serial/JTAG does not survive Light-sleep, so
  do not claim uninterrupted monitor access without a separate safe guard.
  The new Brookesia USB command bridge stays disabled by product defaults.
- Remove invisible full-screen vector gradients beneath opaque raster layers;
  both layers can otherwise repaint on every scrolling dirty region.

## Power and standby ownership

- A standalone face consumer must complete its visible Sleep presentation
  before the power owner closes the write gate. Restore its Wake frame while
  the panel is dark before exposing it. A completed-frame acknowledgment,
  rather than evaluated geometry, establishes the transition boundary.
- Atlas screen IDs and expression combinations are separate identities. Keep
  breathing/blinking as independent rhythmic channels when changing a base
  expression; ordinary boot intent must not silently replace Wake after an
  actual Deep-sleep reset.
- Release standby enters touch/IMU Deep-sleep after quiescence; Debug keeps CPU,
  USB and 10-Hz software motion observation awake. `idle_sleep_s` is measured
  from real activity and defaults to 60 seconds; do not add the previous dim and
  sleep-delay settings. Deep-sleep restarts owners and loses live radio sessions.
- QMI8658C Rev A WoM needs CTRL9/STATUSINT CmdDone/ACK; the vendor convenience
  helper does not perform that protocol and chooses unwired INT1. Check revision,
  use INT2 initially high with EXT1 ANY_LOW, exclude host reads while WoM is armed,
  and explicitly exit WoM after wake because the shared rail remains powered.
- Keep touch scanning and its reset high for wake. Controller sleep that cannot
  detect a finger is unsuitable. A powered GPS can dominate board current even
  when its firmware feature is excluded; MCU sleep numbers are not board numbers.
- EXT1 setup claims and holds RTC pads before the SDK may reject sleep. Release
  both the RTC holds and mux on rejection and wake before digital touch IRQs
  resume. Parked LVGL still needs an IRQ wake observer, without a second reader.

- The published PCB routes touch IRQ to GPIO11 and QMI8658 INT2 to GPIO21;
  both are RTC-capable wake candidates. IMU INT1, PMU IRQ and RTC INT go to
  the TCA9554, whose INT output has no ESP connection. Keep these routes distinct.
  The QMI8658 six-axis reference changes only from 757-758 uA at 500 Hz to
  750 uA at 31.25 Hz (1.8 V datasheet conditions). Sensor power reduction needs
  a gyro-off profile, not only slower host polling. See `hardware-inventory.md`.

- Performance Mode is session-scoped; it must not overwrite saved settings or
  combine high-rate snapshots, diagnostics, verbose logs, and SD mirroring in
  one UI command. Serial capture is the canonical crash source when SD is off.
- The physical PWR key is the AXP2101 PKEY, not GPIO41 (SDMMC chip select).
  The hardware owner decodes and queues IRQ events before verified clearing.
  Poll through a PMU-only mutex and bounded I2C transaction, on an internal-
  stack task that remains capable of waking the device.
- This PCB has no verified ESP wake GPIO for PMIC IRQ. PWR toggles software
  Device Standby while the poller remains alive; timer-driven tickless idle is
  not one continuous PWR-wakeable hardware sleep.
- Enter standby from producers outward: quiesce Full Snapshot, release TOF
  UARTs, park SD work, lower IMU rate, suspend AFE/audio, turn panel off,
  then pause LVGL. Resume LVGL while dark; restore producers and Full Snapshot
  before exposing the panel. Failed transitions require rollback or an
  explicit inconsistent state.
- Serialize brightness, panel state, and rotation through the display lock;
  acquire it before the settings mutex. Defer rotation while the panel is off.
  Display fades need LVGL, so panel-off precedes pause and LVGL resume
  precedes panel-on.
- Before acknowledging `EnteringStandby`, the app must gate input callbacks,
  cancel animations, and drain AppInput/GUIInput/GUI work. LVGL pause must
  either complete, recover, or retain an explicit faulted state; no ambiguous
  timeout becomes success.
- The App power timer is the current Power-to-App bridge. Keep it at 200 ms
  during quiescence/wake; drop to 1 Hz only after confirmed standby. Start a
  replacement timer before stopping the previous one.
- Touch and motion publish activity intent, not optimistic local chrome or
  display state. Arc visibility, hit testing, viewport, brightness, and manual
  Fullscreen derive from the canonical snapshot. Disable Command Arc hit
  testing before hide fades and enable it only after show fades.
- The former `Screen sleep after display dim` setting has been superseded by
  an independent `Screen inactivity timeout`. Its expiry still requests full
  transactional Device Standby. Preserve requested diagnostics and orientation while
  gating actual work; require a fresh active IMU sample before applying a
  pending rotation. Qualify motion with re-arm, consecutive samples,
  hysteresis, and transition cooldown.
- Brookesia scheduler workers block on work generation or the earliest timer
  deadline instead of polling at idle. `io_context::run()` is unsafe with this
  ESP Boost interrupter; the current upstream API no longer exposes a raw
  executor. Park idle SD and AFE workers on notifications.
- The LC76G VCC is tied to the board 3.3 V rail and STANDBY is not routed;
  commands alone cannot enter true Backup. Keep truthful GPS validity. Its
  I2C bridge needs at least 10 ms between 0x54 read and 0x50 control; a
  coalesced PAIR001 response must scan past the in-progress line to the
  terminal ACK. Repeated failed reads can monopolize the shared refresh lock:
  retain the real invalid sample and defer periodic retries for 15 seconds.
  Retain the validated 80 MHz CPU floor until repeated PKEY cycles pass.
- `esp_codec_dev_close()` does not propagate every codec disable callback
  result. Verify I2S shutdown at the BSP and measure ES8311/ES7210 current
  physically. CPU-domain Light-sleep power-down costs at least 8.58 KB of
  internal retention RAM; leave it off until heap evidence supports it.

## Watchdogs and recovery

- A software timeout cannot terminate an unbounded SDK call. Its worker
  retains its Task-WDT user and buffers until exit; publish completion only
  after removing that user. Deadlines for recoverable operations must sit
  below Task-WDT with explicit margin; a wedged call remains armed until reset.
- A watchdog proves progress, not success. Fast repeated errors stay visible
  in component snapshots and logs rather than being reclassified as a hang.

## Frame-level ownership and source splits (2026-10-02)

- Reuse standalone DMA only within a bounded owned frame. Release it at frame
  end/park; a permanent multistrip allocation competes with radio headroom.
  Uncertain completion still requires quarantine, exact retained-byte reporting
  and reset, even when the frame deadline has expired.
- Start the display watchdog before geometry/raster work and retire it before
  another writer can enter. Keep rotation/touch/power mapping atomic for the frame.
  Successful strip writes are not complete-frame success. Log timing from copied
  state after releasing the worker's display lock.
- Extract reporters around the existing canonical snapshot lock/staging owner.
  Do not create another sensor cache or allocate a type-erased callback wrapper.
  Keep source-contract readers aware of explicit implementation modules after
  moving definitions; missing parts must fail instead of disappearing from checks.

## Cody raster performance evidence (2026-10-03)

- Measure complete frames and layer costs separately. Removing debug can raise
  FPS while CPU usage stays high because more frames are drawn per second.
- Reuse a proven idle DMA slot for native raster work only inside its owned
  frame. Endian conversion consumes its pixels; redraw before native reuse.
  A partial frame invalidates the entire comparison cache.
- Conservative dirty candidates need both previous and current artwork bounds;
  movement, shrinking and overlay changes must erase old pixels too.
- Allocation degradation must not increase deferred touch storage unboundedly.
  Poll by logical rows rather than once per possibly one-row DMA tile.
- A user-reported static screen is not sufficient idle CPU evidence when the
  capture still logs active frames. Confirm the route transition in the log.
