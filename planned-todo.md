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
- [ ] Coalesce repeated valid-GNSS system/RTC synchronization after the first
  trusted fix, with explicit drift and maximum-persistence intervals plus RTC
  oscillator-stop recovery coverage.
- [ ] Make the empty SD logger queue notification-driven, and refresh FAT free
  space on mount/write/explicit Storage refresh instead of every health cycle.
- [ ] Evaluate computing microphone FFT bands only for an active audio
  diagnostic lease while preserving measured mean/peak telemetry at the
  requested Full Snapshot and Diagnostic cadences.
- [ ] Evaluate disabling the AXP2101 cell-voltage ADC while no battery is
  detected; require verified BAT_INSERT re-enable, conversion settling, and
  present/absent current measurements before adoption.
- [ ] In a future PCB revision, route the AXP2101/TCA9554 interrupt to an ESP32-S3
  RTC GPIO so PWR can wake asynchronously without periodic software polling.
- [ ] Execute the target-device evidence column in
  `docs/architecture/low-power-management.md`: present/absent battery,
  oscillator-stop recovery, metadata across reboot, updated-image serial
  traffic, task wakeups, and current measurements with image hash and state.
