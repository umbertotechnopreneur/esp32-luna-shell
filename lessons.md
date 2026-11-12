# Engineering lessons

## Startup and stack budgets

- Check compiled stack usage and measure minimum free stack on the worker after
  its deepest call. A 1,264-byte snapshot crowded BLE init on a 4,096-byte pairing
  stack. Use bounded projections and budget internal stacks first. PSRAM leaves
  linked static data in place.
- Cache-disabling writes need internal stacks. Match task deletion to its allocator.
  Keep allocation failure hooks allocation- and log-free. Use wide, saturating
  active clocks.
- Gate frames that need freed logger memory on the post-free archival ACK.

## Display and radios

- Hold DMA buffers to completion. Quarantine uncertain transfers, invalidate
  partial-frame comparisons and commit geometry on success. Erase old and new
  bounds. Keep native pixel byte order.
- CO5300 bounce strips need contiguous internal DMA memory. Measure the largest
  block and actual overlap of slots, commands and deferred touch.
- Reserve BLE before SD/display startup. Park and drain DMA before radio init or
  pairing telemetry teardown. Record parked frames under the gate lock. Retain
  reservations through standby/errors. Rearm only after SDK closure. Report closure
  and re-reservation failures separately. Assembly offsets cannot size allocations.
- Profile completed stationary frames by stage, payload and CPU. Shorten captions
  without shrinking hit targets. See [rendering](docs/cody/runtime.md#experiments-and-source).

## Power, wake and acquisition

- Paired LAN may require software standby. Keep routes, pose, active clocks, scene
  timeouts and pairing dim override. Rebuild RAM owners after Deep-sleep.
- Finish Sleep before closing display writes. Restore consumers before producers
  while dark. Lock rotation/brightness before settings, await fresh orientation and
  recheck mic ownership. Roll back or report partial transitions. Preserve radio teardown errors.
- Check wake wiring per board. On the 1.75 GPS prototype, touch GPIO11 and IMU
  INT2/GPIO21 reach ESP, PWR goes to PMU PKEY, and PMU/RTC lack direct wake. Keep
  touch scanning, honor the QMI revision's wake ACK and restore RTC mux/holds on
  sleep rejection.
- Timestamp samples, reject gaps and wait only for the remainder of the period.
  First float use may pin an IDF task. Keep 20-Hz activity on CPU1 during voice.
- Measure board current with codec/radio states. Keep the measured 80-MHz floor.
  Verify USB reconnect after Light-sleep and disable unsupported SOF/retention hooks
  on IDF 6.0.2. A permanently powered GPS rail rules out GNSS Backup.
- Clear RTC oscillator-stop by trusted write/readback. Keep UTC pending without
  an RTC until trusted sync. Honor LC76G ACK timing and bounded backoff. Report
  failures and recovery immediately and count coalesced repeats.
- Log outside touch critical sections. Filtered calls can take the log-level mutex
  and disrupt event-list operations.

## Audio, UI and protocol

- Separate AFE fetch from TX waits. Await pause ACK before route close. Convert
  boot PCM in bounded chunks with identical speaker/AEC samples.
- Cue typewriter audio from visible text in retired frames. Re-prime silently
  after cancel, replacement or resume. Share ticket/audio generation and
  propagate playback/codec errors.
- Wait for authenticated EVENTACK before replacing a terminal question. Complete
  transfers, frames and credentials on the owner's ACK. Keep buffers and Task-WDT
  ownership until worker exit. Recovery deadlines must beat the watchdog.
- Unload Brookesia route documents, subscriptions and maps together. Destroy
  callbacks outside registry locks. Coalesce bindings and use documented schemas.

## Builds and diagnosis

- Keep OTA images and ESP-SR models within the lower 16 MiB of 32 MB quad flash.
  Writes can verify above the 24-bit mapping limit and fail at boot.
- IDF discovers components before Kconfig. Supply roots/defaults, check build-local
  locks, gate optional headers and isolate assets/stamps. Codec support and boot WAV
  are separate.
- Find VS Insiders with `vswhere -prerelease` and load the x64 host/target shell.
- Capture the first failure and match the ELF fingerprint. Identify USB by unit
  and monitor in-use devices passively to avoid reset. A switch-time canary and
  saved panic stack pointer cannot locate the original write.
- Count log drops and distinguish SD/USB errors. Capture before best-effort USB,
  unlock storage before send and advance replay after flush. Keep optional logging
  off startup/watchdog paths. Identify transferred bytes by CRC. Preserve native errors
  if logging fails.
