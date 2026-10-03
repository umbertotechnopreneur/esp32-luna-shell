# Planned work

This file tracks deferred work that is worth retaining but is not a current
priority. Promote an item to `TODO.md` when it becomes active.

## Cody product increments

- [ ] After choosing the UX, add a reusable fixed-capacity reminder/deadline
  service with typed IDs, shared clock timezone/quiet preferences, snooze and missed-event
  policy. Consume canonical trusted time and arm the existing ESP sleep timer
  through the power owner for scheduled Deep-sleep wake; external RTC INT is
  not connected directly to the ESP. Use cases: gentle alarms,
  focus/break sessions. See `docs/architecture/cody-rtc.md`.

- [ ] Finish the English circular OOBE screen 10 preferences and make it the
  normal first-setup caller for screen 11. The screen 11 `Cody's look` selector,
  stable-ID persistence, live preview and five internal-flash looks are now
  implemented; build, simulator and device behavior remain unverified. Keep
  manual 0/180 orientation, auto-rotation, shake-to-wake and sound preferences
  separate and validity-aware.
- [ ] After the local cloud-rig PoC, select antialiasing/tessellation and
  dirty-region policy using measured target timings. Add state-to-channel
  composition from real, validity-aware environmental events. Keep the shared
  256-slot palette, independent facial channels, and smooth time-based animation.
- [ ] Define/version the companion session/event contract and implement a real
  MCP transport, context/status/messages and confirmed question responses.
  The inherited read-only diagnostics server is not this transport.
- [ ] Add push-to-talk capture, PC-side transcription/confirmation and session
  targeting, plus precomputed sound cues. Keep credentials on the PC.

## Board compilation and BSP

- [ ] Remove the indirect FatFS/SDMMC dependencies pulled by `esp_board_manager`
  when `sd: off`. The BSP already excludes its SD implementation, but the
  dependency graph still contains these components. Explicitly deferred by the
  user: record the limitation without changing SD code or dependencies now.
- [ ] Split the monolithic Waveshare BSP into shared bus, display/touch, audio,
  SD, and I/O-expander units, then add reviewed adapters and dependency locks for
  other boards. The current YAML supports only the Waveshare BSP.

## Power management

- [ ] Identify the exact AMOLED module, MIC1/MIC2, speaker, user-added microSD
  and battery; obtain missing current specifications and enabled-PMU overhead.
  Review LC76G AB low-power command/rail behavior before assigning a Backup
  current to the assembled board.

- [ ] After the explicit Debug/Release split receives host/build/device
  validation, evaluate a safe USB-session-aware sleep policy and measure current,
  reconnect and standby/wake behavior. Keep the known ESP-IDF 6.0.2 USB SOF
  cache-error lesson explicit; do not re-enable that hook without evidence.

- [ ] Evaluate `WIFI_PS_MAX_MODEM` against the current `WIFI_PS_MIN_MODEM`
  policy using measured current, latency, and connection reliability.
- [ ] Evaluate a 40 MHz minimum CPU frequency and CPU-domain Light-sleep power
  down using latency, internal-memory, stability, and current measurements.
- [ ] After target timing instrumentation is available, evaluate a Dimmed
  sampling profile that keeps Full Snapshot at the requested cadence but lowers
  unleased motion/rotation work to 2 Hz without exceeding a 600 ms P99 wake
  latency.
- [ ] Evaluate disabling the AXP2101 cell-voltage ADC while no battery is
  detected; require verified BAT_INSERT re-enable, conversion settling, and
  present/absent current measurements before adoption.
- [ ] In a future PCB revision, route the AXP2101/TCA9554 interrupt to an ESP32-S3
  RTC GPIO so PWR can wake asynchronously without periodic software polling.
- [ ] Execute the target-device evidence column in
  `docs/architecture/low-power-management.md`: present/absent battery,
  oscillator-stop recovery, metadata across reboot, updated-image serial
  traffic, task wakeups, and current measurements with image hash and state.

## Repository maintenance

- [ ] After the 35 existing leading source comments receive an explicit banner
  policy, make `add-source-banner.ps1 -Apply` a staged multi-file transaction
  with verified rollback so an I/O failure cannot leave a partially normalized
  tree.
- [ ] Replace the pinned ESP-LVGL adapter manifest with an upstreamed or
  versioned override that conditions decoder, filesystem, and FreeType
  dependencies on their Kconfig features; disabling the code paths alone does
  not remove that managed-component checkout from disk.
- [ ] Make the Brookesia GUI LVGL default feature bundle configurable so this
  application can disable unused image-font, stdio filesystem, and POSIX
  filesystem support without carrying a private submodule patch.
- [ ] Extract the GNSS NMEA parser only together with host tests for GGA, RMC,
  checksums, malformed frames, and partial input so the maintenance split does
  not change live fix validity semantics.

## Remaining Luna presenter organization

- Extract hardware/audio/Leg/settings and other remaining presenters one family
  at a time, preserving navigation, cached bindings and scoped connections.
  Logs and pairing are already separate. This is R7 maintenance work; it is not
  a promised Cody runtime RAM optimization. Move a family into `TODO.md` when active.
