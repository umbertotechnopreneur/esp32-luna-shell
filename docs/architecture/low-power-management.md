# Low-power runtime acquisition

The current inactivity/product policy is documented in
[Release sleep and Debug observation](release-deep-sleep.md): runtime 60-second
default, 800-ms sleep/wake fades, Release touch/IMU Deep-sleep and Debug CPU/USB retention.
The acquisition optimizations below remain separate from electrical evidence.

This document defines the bounded low-power acquisition changes. They reduce
avoidable wakeups, I2C traffic, register reads, and routine serial output
without changing configured healthy-stream cadences or weakening error
visibility. Suppressing repeated RTC reads while its explicit oscillator-stop
fault is latched is the documented exception.

They are software optimizations, not electrical-savings claims. Current at the
battery or USB input must be measured on the target board before assigning a
power or battery-life improvement to any item below.

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

## Deferred policy changes

The current package deliberately does not change requested acquisition
cadences. The deferred backlog retains optimizations that need timing or
electrical evidence before they can become policy: a 2 Hz unleased Dimmed
motion profile, coalesced GNSS-to-system/RTC synchronization, an event-driven
empty SD logger and event-based FAT-space refresh, diagnostic-only microphone
FFT work, and dynamic cell-ADC gating after battery insertion/removal tests.
Each item must preserve real errors, measurement timestamps, freshness, and
transaction rollback before it can move to `TODO.md`.

## Observational baseline

An interactive pre-change session produced an approximate 706-byte-per-second
serial estimate and showed repeated 1 Hz PMU, RTC, GNSS, and Full Snapshot
logging. The raw capture was not retained as a repository artifact, so this is
a non-reproducible session note that identifies an optimization opportunity,
not an acceptance baseline or electrical measurement. That device reported
application image `117d2dad`, while the source audit started from `40384e63`;
it therefore cannot validate this updated source tree.

## Validation and measurement matrix

| Area | Host acceptance evidence | Target-device evidence still required |
| --- | --- | --- |
| Functional cadence | Settings and snapshot contract checks retain the 1000 ms Full Snapshot default, persisted selection, and 300 ms on-demand Diagnostic default | Timestamp a capture on the updated image and confirm requested active cadences |
| Idle monitor | Static power-policy checks require indefinite notification wait and notifications from every consumer-changing setter | Observe task/runtime counters with all four consumers off, then enable each consumer independently |
| PMU battery | Battery contract/UI checks require detection enable/readback, presence-first sampling, skipped absent-cell reads, and field-level validity guards | Capture with cell present and absent; confirm detection transitions and no false numeric cell telemetry |
| RTC latch | RTC contract checks require first-failure publication, periodic I2C suppression, and trusted write plus readback before recovery | Start with oscillator-stop set, count periodic RTC bus/log activity, then apply a trusted time and verify recovery |
| Static metadata | Snapshot-integrity checks require retry after failure and reuse only after a complete successful metadata read | Compare published values across active snapshots and one reboot |
| Logging | Static checks keep error paths visible and routine PMU/RTC/GNSS success details at `DEBUG` | Compare serial bytes per second on the same log configuration and updated image |
| Firmware integrity | Clean and incremental ESP-IDF builds, size report, simulator checks, diff checks, dependency/legacy scans, and changed-file secret scan | Flash and smoke-test the exact validated image when authorized |
| Electrical consumption | No host test can establish this | Measure current with the same supply and instrument in active idle, active diagnostics, dimmed, screen-off, and Device Standby states; repeat each state and report mean, spread, image hash, radio state, and battery state |

Open PKEY-cycle, IMU-wake, GNSS-standby, codec-current, and whole-device current
measurements remain tracked in `TODO.md` and `planned-todo.md`. They are not
closed by this software package.

## PWR software restart and retained log files

Holding the physical PWR key continuously for 3 seconds requests `esp_restart()`
without resetting the AXP2101 or erasing NVS, settings, or log files. The existing
hardware owner enables/decodes PWRON negative (press) and positive (release)
edges, queues them before verified IRQ clear, and publishes the acquired level
and press timestamp through `InputSnapshot`, with explicit `power_pressed_valid`
to distinguish an unknown level from a real release. Consumers never read PMIC registers
or sensors directly. Both edges in one acquisition cancel the ambiguous hold.

The power policy checks current canonical sample health and a 500-ms freshness
bound. An unknown press start, failed/stale sample or early release cannot
trigger reset; recovery requires a fresh release. A key held before initialization
does not retrigger software resets. The PMIC's earlier long IRQ now waits for
release to enter standby, leaving the owner running until the 3-second reset.
In Cody, an active short PWR tap starts the 3, 2, 1 screenshot countdown while
its session flag is enabled by default. The Cody worker refreshes PWR activity
during the countdown and save so inactivity cannot enter standby mid-capture.
Disabling that flag restores the active short-tap standby behavior. A short PWR
tap in software standby still wakes in one press. The 3-second PWR and BOOT
restart paths retain their log-drain behavior.
An acquired current PWR hold inhibits automatic inactivity sleep so Release
cannot park the power worker in the middle of this gesture.

The reset helper logs the reason, requests the existing bounded one-second SD
writer drain/close when mirroring is enabled, flushes stdout, and gives USB 50 ms
to transmit before restarting. The PMIC's emergency hardware power-off is set
and read back at 10 seconds to leave room for this drain. Errors remain visible;
a failed SD or disconnected host cannot guarantee delivery. Serial capture
files on the host and successfully persisted SD lines survive. The ordinary
RAM log ring does not survive an ESP software reset, and USB may re-enumerate.

This firmware gesture is available while the power worker runs, including
Debug software standby. It is not an electrical wake source from Release
Deep-sleep: the board still has no verified ESP-routed PWR wake pin.
