# Engineering lessons

## Startup and stack budgets

- Read the compiler's stack-usage files for every caller that holds data across
  display, radio, logging or flash calls. A 1,264-byte observation frame on
  Cody's 4,096-byte pairing worker left too little room for BLE controller
  initialization. Project the needed snapshot fields at the acquisition owner
  and measure the worker's minimum free stack after the deepest call returns.
- Account for repeated full-snapshot copies, GATT callbacks and graphics scratch.
  A frame drawn by the startup task can overflow a smaller display worker.
  Budget mandatory internal stacks before optional services. Placing an allocation
  in PSRAM does not move the component's linked static data.
- Run cache-disabling writes such as NVS commits on internal-stack executors.
  Delete capability-allocated tasks with their corresponding API. Keep allocation
  failure hooks allocation-free and log-free. Use wide, saturating counters for
  long-running active clocks.
- Wait for the logger's post-free archival acknowledgment before admitting a
  ready frame that depends on its internal memory.

## Display and radios

- A frame owns its DMA buffers until completion. Quarantine ambiguous transfers,
  invalidate partial-frame comparisons and commit geometry only after success.
  Erase old and new bounds and preserve the panel's native pixel byte order.
- The CO5300 path needs bounded internal bounce strips. Measure the largest
  contiguous block, the admitted slots and their actual overlap, including strip
  commands and deferred touch. Free PSRAM cannot supply an internal DMA block.
- Reserve the BLE controller block before SD and display startup. Park and drain
  display DMA before radio initialization or pairing telemetry teardown. Separate
  total budget from largest-block demand. An assembly offset cannot establish
  the size of an allocation.
- Record a parked frame while holding the gate lock. Retain BLE reservation
  ownership across standby and errors and rearm only after confirmed SDK closure.
  Distinguish failure to reserve the next session from failure to close this one.
- Measure completed frames on a stationary route with stage times, payload and
  CPU. Font and safe-circle bounds apply to complete captions and touch targets.
  Shorten captions that exceed their viewport while preserving the complete
  touch target. [Rendering](docs/cody/rendering.md).

## Power, wake and acquisition

- Reapply policy explicitly and keep the observed state accurate. Paired LAN
  service can require software standby. Preserve routes, pose and active clocks
  across software standby and reconstruct RAM owners after Deep-sleep. Apply
  scene timeouts locally and preserve the explicit pairing override for dimming.
- Complete Sleep before closing display writes. Restore consumers before producers
  while dark. Lock rotation and brightness before settings, wait for fresh
  orientation and recheck microphone ownership. Roll back partial transitions or
  publish inconsistency. Preserve radio teardown errors even if graphics resume.
- Verify wake wiring per board. On the 1.75 GPS prototype, PWR reaches PMU PKEY,
  touch uses GPIO11 and IMU INT2 uses GPIO21. PMU and RTC interrupts lack a direct
  ESP wake route. Honor the QMI revision's wake command ACK and restore RTC mux
  and holds after rejected sleep. Keep touch scanning active.
- Schedule the remainder of each acquisition period after measurement. Timestamp
  real samples and reject gaps. An IDF floating-point task may acquire affinity on
  first use. Keep the 20-Hz activity owner on CPU1 with voice active.
- Measure whole-board current, include codec and radio state and retain the
  measured 80-MHz floor. Native USB reconnect matters after Light-sleep.
  Keep unsupported SOF/retention hooks disabled on IDF 6.0.2. A permanently
  powered GPS rail prevents a claim of GNSS Backup.
- An RTC with its oscillator-stop flag needs trusted write and readback. On an
  RTC-less board, publish pending UTC until a trusted source succeeds. Respect
  LC76G ACK timing and bounded read backoff. Publish errors and recovery immediately
  and coalesce unchanged errors with counted reminders.
- Avoid logging inside touch critical sections. Even filtered log calls can take
  the log-level mutex and interfere with event-list operations.

## Audio, UI and protocol

- Give AFE fetch a receive owner independent of TX waits. Acknowledge pause before
  closing a route. Convert boot PCM in bounded chunks and supply identical
  speaker and AEC reference samples.
- Base typewriter sounds on visible text in retired frames. Re-prime silently
  after cancellation, replacement or resume. Keep ticket and pending audio under
  the same generation and propagate playback and codec errors.
- Wait for authenticated EVENTACK before replacing a terminal question. Count a
  transfer, frame or credential exchange as complete at its owning protocol's
  acknowledgment. Keep buffers and Task-WDT ownership until the worker exits.
  Allow recoverable deadlines to expire before the watchdog.
- A Brookesia route owns its document, subscriptions and maps. Release them at
  unload and destroy callbacks outside registry locks. Publish bindings once
  after coalescing changes and use documented declarative schemas.

## Builds and diagnosis

- On the 32 MB quad-flash board, keep each full OTA image and ESP-SR model
  partition within the 24-bit cache mapping window. A write above 16 MiB can
  verify successfully while startup fails when the image tries to map it.
- IDF discovers component roots before Kconfig selection. Supply the chosen
  roots and defaults, check build-local locks and gate optional headers. Each
  build tree owns its assets and stamps. Codec support and boot WAV are separate.
- For authorized Windows native builds, locate Visual Studio Insiders with
  `vswhere -prerelease` and load the x64 host/target development shell.
- Capture the first failure and match the ELF fingerprint before decoding.
  Identify the USB unit independently of COM ordering and use passive capture
  for an in-use device. A reset while opening a monitor obscures the trigger.
  A stack canary reports damage observed at the context switch. The saved panic
  stack pointer does not locate the original write.
- Queue pressure counts dropped log records. Keep SD failure and USB errors
  distinct. Capture before best-effort USB output, avoid holding storage locks
  during transmission and advance replay after flush. Keep optional logging
  independent of startup and watchdog progress. A CRC identifies the transferred
  bytes. Preserve native errors when logging also fails.
