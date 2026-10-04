# Deferred work

These items are not active commitments. Move an item to [TODO.md](TODO.md) when
activated; authorization still follows [AGENTS.md](AGENTS.md).

## Cody product

- [ ] Finish English circular screen 10 preferences and make it the normal
  first-setup caller for implemented screen 11. Keep manual 0/180 orientation,
  auto-rotation, shake-to-wake and sound separate and validity-aware.
- [ ] After UX selection, add a fixed-capacity reminder/deadline service with
  typed IDs, timezone/quiet preferences, snooze/missed-event policy and trusted
  canonical time. Schedule Deep-sleep wake through the existing power/timer
  owner; external RTC INT is not directly connected to ESP.
  [RTC](docs/architecture/cody/clock.md).
- [ ] Define/version a real companion session/event contract and MCP transport
  for context/status/messages and confirmed responses; diagnostics HTTP is not
  that transport. Add push-to-talk, PC transcription/confirmation/session
  targeting and precomputed cues; credentials stay on the PC.
- [ ] Choose rig antialiasing/tessellation/dirty-region policy from target
  timings; extend validity-aware environmental composition while retaining
  shared palette, independent face channels and time-based animation.
- [ ] Review the opt-in PNG/triangle cloud with the user before native port:
  texture sampling/CPU, extreme motion/navigation clearance and visual approval.
  Retain the host-only 99-vertex/138-triangle study; the approved pearl scanline
  renderer is distinct. [Experiment](assets/cody/experiments/png-mesh/README.md).
- [ ] Resume cloud-volume/jelly-IMU default exploration only when activated.
  The billows-v5 height-field study was rejected; synthetic previews are not
  native performance evidence. Review final motion/clearance/assets before a
  requested target measurement.
  [Cloud evidence](docs/architecture/cody/rendering.md).

## Board and dependency graph

- [ ] Remove indirect FatFS/SDMMC dependencies from `esp_board_manager` when
  `sd: off`. BSP implementation exclusion is already present; dependency removal
  is explicitly deferred, not permission to change SD now.
- [ ] Split the Waveshare BSP into shared bus, display/touch, audio, SD and
  expander owners, then review other-board adapters/locks. YAML currently
  supports only this BSP, including the pending rectangular ES8311-input path.

## Power research

- [ ] Identify the actual AMOLED, microphones, speaker, added SD/battery and
  missing current/PMU specifications; establish LC76G command/rail limits before
  assigning Backup current.
- [ ] After Debug/Release validation, investigate USB-session-aware sleep with
  measured reconnect/current/wake behavior. Do not re-enable the known unsafe
  IDF 6.0.2 USB SOF hook without evidence.
- [ ] Measure `WIFI_PS_MAX_MODEM` versus `MIN_MODEM`, a 40 MHz CPU floor and
  CPU-domain Light-sleep power-down against latency, memory and stability.
- [ ] With target timing available, evaluate unleased Dimmed motion/rotation at
  2 Hz while preserving requested Full Snapshot cadence and 600 ms P99 wake.
- [ ] Evaluate battery-absent AXP2101 cell-ADC disable only with BAT_INSERT
  re-enable, conversion-settling and present/absent-current evidence.
- [ ] Future PCB: route PMU/expander IRQ to an ESP RTC GPIO for asynchronous PWR
  wake. Execute the deferred target evidence matrix: battery presence, RTC
  oscillator-stop, reboot metadata, serial/task wakeups and image-tagged current.
  [Low-power](docs/architecture/low-power-management.md).

## Maintenance

- [ ] After an explicit banner policy for existing leading comments, make
  `add-source-banner.ps1 -Apply` a staged multi-file transaction with rollback.
- [ ] Replace the pinned LVGL-adapter manifest with an upstream/versioned feature
  override for decoder/filesystem/FreeType dependencies; code exclusion alone
  does not remove managed checkouts.
- [ ] Make the Brookesia LVGL default feature bundle configurable without a
  private patch for unused image fonts, stdio and POSIX filesystems.
- [ ] Extract GNSS NMEA parsing only with requested host coverage for GGA/RMC,
  checksums, malformed/partial input and unchanged fix validity.
- [ ] Extract remaining hardware/audio/Leg/settings presenters one family at a
  time, preserving navigation, cached bindings and scoped connections.
  Logs/pairing are separate; this R7 maintenance is not a promised Cody RAM gain.
