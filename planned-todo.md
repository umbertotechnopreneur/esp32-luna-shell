# Planned work

This file tracks deferred work that is worth retaining but is not a current
priority. Promote an item to `TODO.md` when it becomes active.

## Power management

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
- [ ] Make the headless profile exclude `LUNASHELL_app` and split or condition
  the BSP UI dependency closure so a clean headless build does not discover or
  compile the Brookesia/LVGL application graph.
- [ ] Replace the pinned ESP-LVGL adapter manifest with an upstreamed or
  versioned override that conditions decoder, filesystem, and FreeType
  dependencies on their Kconfig features; disabling the code paths alone does
  not remove that managed-component checkout from disk.
- [ ] Make the Brookesia GUI LVGL default feature bundle configurable so this
  application can disable unused image-font, stdio filesystem, and POSIX
  filesystem support without carrying a private submodule patch.
- [ ] Remove the per-call `changed_updates` heap allocation from Brookesia's
  batched binding runtime, with host coverage for unchanged values, missing
  targets, binding errors, and dirty-node reapplication before updating the
  pinned submodule revision.
- [ ] Extract the GNSS NMEA parser only together with host tests for GGA, RMC,
  checksums, malformed frames, and partial input so the maintenance split does
  not change live fix validity semantics.
