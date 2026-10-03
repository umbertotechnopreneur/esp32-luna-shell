# Release sleep and Debug observation

Implementation checkpoint, 2026-10-04. Source review only: no build, automated
check, simulator, flash, serial access or electrical measurement was performed.

The shared inactivity timer starts at the last real touch or qualified movement.
Its first-boot default is **60 seconds**. The display fades out over a nominal
**300 ms**, then Release enters Deep-sleep after successful transactional standby.
Debug uses the same timeout and fade but keeps the CPU running and USB available.
Leg Edition uses this shared timer too; its former fixed 25-second panel-only
policy no longer overrides device inactivity.

| Resource | Active Debug | Debug standby | Release Deep-sleep |
| --- | --- | --- | --- |
| CPU / USB Serial-JTAG | Awake; normal frequency scaling | Awake; no automatic Light-sleep | CPU powered down; USB disconnects |
| Logging | INFO default; VERBOSE compiled | Errors and real input/power diagnostics remain available | WARN/ERROR compiled; no host polling or serial work |
| Display | Configured brightness | 300-ms fade to off | Off; wake startup stays dark until the first boot frame and 300-ms fade-in |
| Touch | IRQ-driven acquisition | Available for software wake | Powered; GPIO11 EXT1 low-level wake |
| IMU | Canonical measured snapshots | 10-Hz host motion monitor, six-axis software-standby profile | Accelerometer-only low-power WoM on INT2/GPIO21; gyro off |
| Wi-Fi / BLE | Available according to explicit service settings | Quiesced by the existing standby transaction | Connections lost; owners reinitialize on boot |
| Audio / AFE / SD / TOF / full snapshots | Selected/requested services | Suspended by the existing transaction | Host services stopped |
| PWR | Existing PMU event handling | Existing software wake | No PWR/PMU wake source; no polling timer |

Deep-sleep is a **firmware restart**, so 300 ms specifies the fade, not total
touch-to-screen latency. The normal product startup runs again: Cody starts its
35B / Wake presentation followed by 16 / Idle and startup-managed radio services;
Luna keeps its existing
explicit Settings-based radio startup. Neither current route nor a live radio
session is claimed to survive Deep-sleep. Saved NVS settings remain available.

## Runtime setting

`LUNASHELL_user` NVS key **`idle_sleep_s`** is the typed
`LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE` setting. Supported values are
**30, 60, 120, 300, 600, 900 seconds, or 0 to disable inactivity sleep**.
The setter persists the value before applying it and restarts the activity timer:

```cpp
const esp_err_t error = LUNASHELL_power_manager_set_timeout(
    LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE, 120);
```

Callers must handle the real error. The getter returns the live value; canonical
snapshots and the optional HTTP diagnostic response publish
`power.screen_idle_timeout_seconds`. This diagnostic GET is not a settings-write
endpoint. Luna's existing Settings dropdown now edits this timer. Cody can use
the same platform API; this checkpoint does not introduce a new Cody settings UI.

The key is independent of `dim_idle_s` and the older `sleep_delay_s`. That older
delay remains stored and reported but no longer controls standby; values are not
migrated, overwritten, added to the new timeout or used as a fallback. Dimming
can still occur before sleep if explicitly configured with an earlier threshold.
Only an absent `idle_sleep_s` key receives the logged, persisted default. Invalid
values, failed reads or failed writes return their actual errors.

## Physical wake and recovery

Both direct interrupts use EXT1 **ANY_LOW**, through the ESP-IDF 6.0.2 native API.
The PMU and RTC interrupts reach an expander whose INT is not wired to the ESP,
so they are not added as Deep-sleep wake sources. Normal inactivity arms no timer.
No ULP program is needed for touch plus IMU interrupt wake.

QMI8658C WoM uses an initial **200-mg acceleration-slope threshold**, INT2 initially
high and three startup blanking samples. The gyro is disabled; low-power
accelerometer ODR is selected at the driver's nominal 21 Hz. Debug uses a
200-mg-equivalent measured acceleration delta plus quiet-dwell qualification;
its software sampling/filtering is not identical to the sensor's WoM algorithm.
Threshold suitability and false/missed wakes require physical tuning.

The implementation follows the manufacturer's
[QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
sections 5.10 and 12: write the WoM settings, execute CTRL9, wait for
STATUSINT.CmdDone, send ACK and verify controls. It checks the documented
revision ID `0x7C` before changing wake configuration and rejects unknown
revisions. The Waveshare 2.0.0 convenience function omits the command/ACK and
selects the unwired INT1, so the acquisition owner performs this configuration.
All sensor reads still belong to that owner. Wake-only mode never publishes
gyroscope data or a fabricated fresh measurement. Startup explicitly exits WoM
because the sensor's common supply stayed powered while the MCU reset.

Touch stays powered and out of reset. The CST9217 datasheet describes automatic
idle monitor scanning; the firmware does not issue an undocumented mode command
or put the controller into its non-detecting 2-uA sleep state. Actual panel-firmware
monitor entry/current still needs measurement. GPIO holds preserve panel/touch
reset high, QSPI chip select high and the physical amplifier disabled.
Wake startup releases holds and RTC pad ownership before driver initialization.
Rejected SDK sleep entry releases the wake inputs' RTC holds and mux too, so
software touch interrupts can resume. In Debug's GUI path an ISR generation
observer remains alive while LVGL is parked; it publishes only wake intent,
and the existing acquisition owner remains responsible for actual touch samples.

Entry refuses an already asserted wake IRQ or touch activity arriving during
standby suspension. A rejected sleep entry removes wake configuration and WoM,
then restores the active transaction. If physical recovery fails, the published
transition remains incomplete and active operation stays blocked; the error is
not replaced with success or a silently selected sleep mode.

## Remaining evidence

Verify revision identity, touch and IMU wake, held-finger behavior, repeated
sleep/reboot cycles, configured timeout persistence, startup frame/fades, actual
wake latency, USB reconnect and service behavior on the built image. These checks
require separate explicit authorization under `AGENTS.md`.

The [hardware census](../../hardware-inventory.md) lists component currents and
rail limits. The GPS VCC, touch, IMU and other loads share 3.3 V. In Cody, disabling
the GPS firmware feature does **not** disconnect or establish Backup mode on the
physical LC76G. MCU Deep-sleep alone therefore does not guarantee a microamp
whole-board budget. Measure the assembled board and review GNSS/codec/panel power
states before estimating battery life.
