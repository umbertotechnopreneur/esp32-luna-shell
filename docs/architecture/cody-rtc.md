# Cody RTC: existing support, board wiring and useful schedules

Source review and profile activation: 2026-10-04. No configure/build, tests,
simulator, serial access or flash was requested or performed. This enables
source configuration; it does not change the firmware currently on the device.

## Enabled implementation

`config/sdkconfig/v1/profiles/cody-companion.defaults` now explicitly selects
`LUNASHELL_BUILD_RTC_DRIVER_PCF85063` and enables `LUNASHELL_BUILD_ENABLE_RTC`.
The RTC driver, time service and snapshot fields already exist; activation
does not add a task, polling loop, framebuffer or component dependency.
Debug and Release both inherit this profile setting.

- `LUNASHELL_hardware.cpp` owns PCF85063 initialization and calendar acquisition
  on the BSP's existing shared I2C bus, plus serialized trusted UTC writes.
- `LUNASHELL_time.cpp` restores a valid calendar into the system clock at boot.
  After Wi-Fi obtains an IP, the existing SNTP callback synchronizes system UTC
  and persists it to the external RTC. GNSS synchronization is supported by the
  general service but GNSS remains disabled in the Cody profile.
- The existing Full Snapshot refresh samples RTC when its initialized owner
  is available. UI/character consumers must use `DeviceSnapshotService`, not
  `LUNASHELL_rtc_read()`, a BSP accessor or another hardware reader.
- `RtcSnapshot` publishes calendar, clock integrity, source, synchronization,
  system-time status, actual errors and original sample timestamps. Disabled
  optional HTTP diagnostics already have a read-only `/api/v1/rtc` endpoint;
  this change does not enable the server.

An oscillator-stop indication or an uninitialized calendar remains unavailable.
No build date, invented epoch, default alarm or corrupt-setting repair is
written. A trusted SNTP/GNSS update uses the existing recovery/readback path.
Offline operation therefore requires a previously valid RTC and maintained
backup power; first boot without trusted time does not promise a usable clock.
RTC storage is UTC. The persisted clock preferences default to `UTC0`; the
chat's timezone does not automatically configure the device. Offline routines,
quiet hours and measured drift are now implemented in source; see
[trusted clock and routine policy](cody-clock.md) for runtime APIs and limits.

## Chip and schematic facts

The published board schematic identifies **U4 PCF85063ATL**, a 32.768-kHz
crystal, I2C address `0x51`, SDA GPIO15 and SCL GPIO14. Its VDD is `VCC-RTC`
from the AXP2101 power subsystem, not an independent always-present coin cell.
CLKOUT is unconnected and CLKOE is tied low. Retention with USB removed, battery
removed or PMU shutdown still needs measurement on the actual board.

The chip provides one alarm comparator with independently enabled second,
minute, hour, day-of-month and weekday matching, plus an 8-bit auto-reloading
countdown (maximum 255 seconds at 1 Hz, or 4 h 15 min at one tick/minute).
There is no month/year alarm field or multiple-alarm bank. Multiple user
reminders need a software schedule and next-deadline selection. Calendar,
oscillator-stop detection and frequency-offset correction are also available.
Typical chip timekeeping current is 0.22 microamp at 3.3 V/25 C with CLKOUT off;
this is not the total board current or an accuracy guarantee.

The interrupt path is:

```text
PCF85063 INT -> EXIO3 / TCA9554 P3 -> TCA9554 INT -> pull-up only, no ESP GPIO
```

Consequently this PCB cannot directly wake ESP32 Deep-sleep from an external
RTC alarm. While awake, the acquisition owner could observe the alarm flag or
expander input without introducing a second reader. Deep-sleep wake requires
another path: the existing ESP sleep timer is a practical software option;
wiring an interrupt to a free RTC-capable GPIO would be a separate hardware
change, not enabled here. PMU power-off is different from ESP Deep-sleep.

## What is not implemented yet

The firmware calendar pipeline does not program alarm/countdown registers,
publish alarm status, persist a reminder schedule or provide alarm UI. Merely
enabling RTC does not turn any of these on. Normal Release inactivity passes
zero to the existing optional sleep timer; there is no scheduled wake deadline
until a scheduler explicitly supplies one.

## Product uses worth considering

1. **Offline routine companion (implemented in source):** greetings, softer evening animation and
   configurable quiet hours based on trusted local time. Keep UTC storage and
   timezone conversion separate; invalid time must not pick a plausible routine.
2. **Gentle wake/reminder:** a pearl face wakes gradually, shows one short
   English prompt and plays a bounded cue. Touch acknowledges; snooze creates
   a new deadline. For Deep-sleep, compute the next UTC deadline from valid
   published time and arm the existing ESP wake timer through the power owner.
3. **Focus sessions:** Pomodoro, stretching or hydration reminders. Use a
   monotonic deadline during an active session so an NTP correction cannot
   shorten or lengthen it unexpectedly; persist the intended UTC deadline when
   restart recovery is required. External RTC countdown auto-reload must be
   handled explicitly if a one-shot alert is chosen.
4. **Small persistent rituals:** remember the last successful daily activity
   or show an overdue reminder after restart, with an explicit missed-event
   policy instead of replaying every missed alert.
5. **Lower-connectivity clock (implemented in source):** preserve useful time between Wi-Fi sessions
   and measure drift across trusted synchronizations. Later decide whether
   less frequent network synchronization is appropriate; no power saving or
   automatic frequency correction is claimed from this source-only change.

Deferred next increment: one reusable fixed-capacity deadline service with
typed alarm IDs, the shared timezone/quiet-hour preferences, next-deadline
selection and canonical alarm snapshots. Route sound/display/power actions
through their current owners. Agree on snooze, offline validity and missed
events before implementing or scheduling anything automatically.

## Primary sources

- [Waveshare 1.75-inch schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf),
  page 1: RTC, I/O Extensions, AXP2101 and GPIO mapping. Visually reviewed.
- [NXP PCF85063A datasheet, Rev. 7.3](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf),
  sections 2, 7.5 and 7.6: clock, alarm matching and timer behavior.
  The manufacturer's current PDF was read online; the
  [Rev. 7 copy distributed by Waveshare](https://files.waveshare.com/wiki/common/PCF85063A.pdf)
  was also rendered locally for its alarm and timer tables (sections 8.5/8.6).
- [Waveshare hardware reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md),
  shared I2C addresses and expander signal table.

Local firmware evidence: `LUNASHELL_hardware.cpp`, `LUNASHELL_time.cpp`,
`LUNASHELL_wifi.cpp`, `LUNASHELL_device_snapshot.hpp`, `main_headless.cpp` and
the current `LUNASHELL_sleep.cpp`. Hardware operations and final validation
remain deferred until separately requested.
