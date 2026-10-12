# Power acquisition, standby and wake

The power owner controls inactivity and resource transitions. Optional services
declare their requirements; consumers do not read hardware or guess retention.
These are source contracts. Whole-board current, battery life and complete wake
latency require measurements on the identified board and image.

## Standby and wake

The shared inactivity timer starts at the last real touch or qualified movement.
Its first-boot default is **60 seconds**. The display fades out over a nominal
**800 ms**, then eligible non-Cody Release profiles choose sleep after successful transactional standby.
The power owner keeps software standby when the confirmed radio transaction
retains the Cody/Codex association or wake-word standby listening is enabled.
It permits Deep-sleep only with neither live service and a touch-enabled profile.
The build mode grants permission to sleep; it does not override requested services.
Debug uses the same timeout and fade but keeps the CPU running and USB available.
Leg Edition uses this shared timer too; its former fixed 25-second panel-only
policy no longer overrides device inactivity.

| Resource | Active Debug | Software standby | Release Deep-sleep |
| --- | --- | --- | --- |
| CPU / USB Serial-JTAG | Awake; normal frequency scaling | Debug stays awake; Release keeps its existing workload-governed Light-sleep policy | CPU powered down; USB disconnects |
| Logging | INFO default; VERBOSE compiled | Errors and real input/power diagnostics remain available | WARN/ERROR compiled; no host polling or serial work |
| Display | Configured brightness | 800-ms fade to off | Off; wake startup stays dark until the first boot frame and 800-ms fade-in |
| Touch | IRQ-driven acquisition | Available for software wake | Powered; GPIO11 EXT1 low-level wake |
| IMU | Canonical measured snapshots | 10-Hz host motion monitor, six-axis software-standby profile | Accelerometer-only low-power WoM on INT2/GPIO21; gyro off |
| Wi-Fi / BLE | Available according to explicit service settings | Retained for the admitted Cody/Codex association; otherwise quiesced | Connections lost; owners reinitialize on boot |
| Audio / AFE / SD / TOF / full snapshots | Selected/requested services | Suspended except explicitly enabled wake-word listening | Host services stopped |
| PWR | Existing PMU event handling | Existing software wake | No PWR/PMU wake source; no polling timer |

Deep-sleep is a **firmware restart**, so 800 ms specifies the fade, not total
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


## Acquisition policy

## Unchanged functional cadences

- Full Snapshot still defaults to 1000 ms (1 Hz). The user-selected 300 ms to
  10 s value remains persisted and authoritative.
- The Diagnostic Stream still defaults to 300 ms (about 3.3 Hz), runs only
  while a diagnostic route requests IMU or microphone data, and keeps its
  existing persisted 100 ms to 2 s cadence range.
- Device Standby still suspends periodic Full Snapshots and requested
  diagnostic UI streams. The retained adaptive-motion consumer is separate
  from those streams and follows the standby wake policy.

The optimizations therefore remove work inside or between existing acquisition
cycles. They do not change the configured cadence values or the shared
monitor's existing consumer arbitration.

## Event-driven idle hardware monitor

The shared hardware monitor serves four independent consumers: diagnostic IMU,
diagnostic microphone, adaptive UI motion, and auto-rotation. When none is
active and no rotation application is pending, its task blocks indefinitely on
a FreeRTOS notification instead of waking on the former idle delay.

Every setter that can make work eligible notifies the task after publishing the
new state. This includes diagnostic-stream selection, adaptive-motion enable or
standby state, UI-sampling power suspension/resume, auto-rotation, diagnostic
cadence changes. The task re-reads all consumer state after
it wakes and again under the refresh lock before sampling, so the notification
does not bypass the existing quiescence barrier.

## PMU battery acquisition

AXP2101 battery detection is explicitly enabled during PMU initialization and
the `BAT_DET_CTRL` setting is read back. A write or readback mismatch is an
initialization error; it is not converted to a successful PMU state.

Each periodic PMU sample reads battery presence before cell telemetry:

- with a detected cell, percentage and voltage are read and published only
  after their individual reads succeed;
- without a detected cell, the cell percentage and voltage reads are skipped,
  and their field-level validity flags remain false;
- in a PMU sample that succeeds as a whole, absence of a cell does not make the
  USB input, system rail, charge state, or temperature unavailable.

Consumers must require current PMU health plus `battery_present`,
`level_percent_valid`, and `battery_mv_valid` before rendering the two cell
values. An absent or failed cell sample is `UNAVAILABLE`, never a plausible
zero-voltage or zero-percent measurement.

## RTC oscillator-stop latch

The PCF85063 oscillator-stop flag is a persistent integrity failure. After the
first periodic read observes it, the firmware keeps the real
`ESP_ERR_INVALID_STATE` and unavailable RTC state visible but latches the
condition so later periodic Full Snapshots do not repeat the same RTC I2C read.
Direct RTC reads remain explicit hardware operations.

Only a trusted RTC write can clear the periodic latch. When recovery is
required, that write is followed by one immediate hardware readback; success is
published and the latch is cleared only if the readback matches the requested
calendar (allowing the next second) with clock integrity restored. Normal
writes do not add a readback, because a valid GNSS fix may legitimately update
the RTC at the Full Snapshot cadence. A write or recovery-readback failure
remains the returned error and the RTC stays unavailable. A write-generation
guard prevents an older concurrent oscillator-stop observation from relatching
the RTC after a trusted write. Its generation comparison and latch update share
the RTC operation mutex, so that stale observation also cannot clear a newer
oscillator-stop latch. The recovery requirement becomes visible before the
staged snapshot commits, so a GNSS write later in that same Full Snapshot cannot
skip readback. The same operation mutex prevents concurrent GNSS and SNTP
writers from interleaving a second write inside the recovery transaction.

## Immutable system metadata

Flash capacity, running application-partition size, running image length, and
ESP32 core count do not change during a boot. They are cached only after the
complete first successful read and copied into later Full Snapshots. A failed
first read is not cached and is retried, preserving the original error.

Dynamic heap, PSRAM, allocation-failure, current-core, task, stack-watermark,
and CPU-utilization observations are still sampled on every Full Snapshot.
Caching immutable metadata must not turn those runtime measurements into stale
copies.

## Logging policy

Successful periodic PMU, RTC, and GNSS acquisition details use `DEBUG` level.
Rejected data, hardware failures, component/service unavailability caused by
an error, publication failures, and recovery errors keep their visible warning
or error level. A successfully observed absent battery remains routine DEBUG
state with invalid cell-field guards. The canonical snapshot remains the
source of truth; lower success verbosity does not change `esp_err_t`, health,
validity, or timestamps.


## Power key, farewell and PMU shutdown

Cody reserves PWR in both Debug and Release: one completed short click enters
standby or wakes software standby. A valid continuous three-second hold requests
shutdown. Screenshot, double-click restart and PWR restart no longer consume this
key. BOOT diagnostics retain their existing behavior. Failed/stale/unknown key
samples invalidate the hold until a fresh release; long IRQs suppress short-click
actions but do not independently prove a valid continuous hold.

Cody keeps software standby because this board cannot route PMU PKEY to an ESP
Deep-sleep wake input. This preserves short-Power wake alongside enabled touch,
shake and Sophia wake-word services. Other profiles retain their sleep selection.
Away remains an animation policy, not a radio/CPU/voice energy-saving mode.

Automatic Cody standby requires a valid, current, unambiguous battery-only sample
from the canonical acquisition owner. Valid VBUS prevents automatic inactivity
standby while charging, fully charged or battery absent. Unknown, stale or failed
power observations inhibit the automatic decision without asserting either power
source. Battery-only use retains the saved timer (first-boot default 60 seconds)
and scene overrides. Explicit PWR standby and shutdown bypass this source gate.
The AXP2101 battery activation bit is not a battery-source indicator.

Shutdown first retires any screenshot operation, presents ASCII `Good night`
with the existing Sleep character, waits for complete-frame retirement and a
visible dwell, then uses the existing gradual display fade. The ordered owner
transaction stops recording, AFE/audio, storage/log writers, acquisition and
radios instead of retaining standby services. The settings owner serializes and
seals new requests after earlier commits finish. Original errors abort the PMU
request, are retained in canonical power health and are displayed after recovery
when the display itself can be restored. No storage formatting or setting reset
is part of shutdown.

The PMU owner checks configured emergency shutdown behavior and writes
AXP2101 REG10H[0]. A successful I2C write means command acceptance only. If the CPU
still executes after the bounded 1.5-second wait, firmware reports a timeout and
attempts restoration; it never declares physical power-off from an ACK.

REG27 readback supplies the configured power-on hold duration to battery status
and startup logs; no physical duration has yet been measured for this revision.
The emergency hardware hold is configured/read back at 10 seconds and performs
power-off rather than reset. It can cut power independently if the key remains
held, including during a failed firmware drain. Release the key after the farewell
starts; exact hardware behavior requires device evidence.

After successful PMU shutdown, firmware, classifier, touch, motion and wake-word
processing do not run. Power-on uses the PMU hardware key. The
[AXP2101 datasheet](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
also describes customized VBUS/battery/IRQ startup conditions and does not expose
a documented runtime mask to disable them. Therefore Power-only restart across
USB reinsertion, battery recovery or those hardware conditions is **not yet
guaranteed**. No undocumented register is changed to pretend otherwise. Test
USB-connected and battery-only shutdown separately. The 40-uA typical shutdown
figure describes one PMU condition, not whole-board current or zero consumption.

## Evidence and deferred policy

Build modes are in [toolchain and profiles](../toolchain-and-profiles.md#debug-and-release).
Current measurements/checks are in [TODO.md](../../../TODO.md); future cadence,
ULP, PCB and power experiments are in [planned-todo.md](../../../planned-todo.md).
The earlier 706-byte/s serial estimate lacked a retained raw capture and was not
an acceptance/electrical baseline; its original image/source context remains
in [done.md](../../../done.md). Source, host policy, build, residency, wake and
electrical observations are separate evidence levels.
The [Waveshare power reference](../boards/waveshare-esp32s3-touch-amoled-1.75/power-reference.md)
records datasheet ranges and shared rails. Disabling GNSS firmware does not
disconnect its supply. The service-aware standby/EVENTACK revision of 2026-10-10
requires its own target evidence; prior Debug evidence does not validate Release.
