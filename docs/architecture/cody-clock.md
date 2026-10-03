# Trusted offline clock and routine policy

Implementation: 2026-10-04. Source review only; no build, test, simulator,
serial access or deployment has been performed for this increment.

## Ownership and offline behavior

The existing PCF85063 acquisition owner stores UTC and restores a valid clock
at startup. `LUNASHELL_time.cpp` observes the real system clock and converts it
to local time. The existing Full Snapshot refresh publishes that observation
after its hardware-stage commit. No new timer, task, sensor reader or cache is
introduced. Cody only consumes the canonical aged observation; it never calls
`gettimeofday`, `localtime`, RTC, BSP or I2C APIs.

`DeviceSnapshot::clock` contains UTC epoch, local minute, source, configuration,
period, quiet policy, health, original error and original sample timestamps.
Unknown or stale time is not morning, evening or night. Conversion is a
time-owner operation, not a fabricated new RTC measurement. A healthy system
clock previously established from RTC/SNTP/GPS can keep running offline even
when a separate RTC persistence operation fails; that error remains in RTC
state. Offline restart still requires a valid RTC and maintained backup power.

## First Internet connection and NTP

`LUNASHELL_time_notify_wifi_online()` is the explicit asynchronous NTP request
entrypoint already called by the Wi-Fi IP event. SNTP is initialized with
`start=false`, then started once on the first usable IP connection. Repeated
IP events do not start a second client. A rejected start returns its real
error and can be retried by a subsequent event.

DHCP is not proof of Internet access. Only the SNTP callback with a valid
2024..2099 UTC calendar confirms synchronization. The existing SNTP retries and
periodic cadence remain unchanged; neither Wi-Fi power nor synchronization
frequency is changed here. If Internet is unavailable, use valid restored RTC
time or publish unavailable time, never the build date or a plausible default.

## Persistent runtime configuration

Use `LUNASHELL::read_clock_preferences()` and
`LUNASHELL::configure_clock_preferences()` from `LUNASHELL_clock.hpp`.
Configuration changes use the existing internal-stack NVS executor, not the
Cody worker's PSRAM stack. One typed uint32 key, `clock_routine`, atomically
stores enabled flags, explicit timezone ID and both quiet-window endpoints.
An absent key logs and persists defaults; a corrupt, wrong-type or invalid
key returns its real error without rewriting it.

Defaults: routines enabled; quiet hours enabled, local **[22:00, 07:00)**;
timezone `Utc` / `UTC0`. The chat or workstation timezone is not a device
setting. Supported typed choices are `Utc`, `Rome` (EU daylight-saving rules)
and `HoChiMinh` (UTC+7, no daylight saving). Unsupported identities are errors,
not fallback UTC. RTC/system UTC is never rewritten by a timezone change.

Quiet endpoints are minutes since local midnight, 0..1439. Overnight and
same-day windows are supported. Equal endpoints are rejected when quiet hours
are enabled: disabling quiet hours is an explicit flag, not an ambiguous empty
or all-day interval. Quiet policy does not program hardware alarms or wake Cody.

Local routine periods are morning [06:00,12:00), afternoon [12:00,18:00),
evening [18:00,22:00), and night otherwise. Disabling routines also disables
their sound policy. Disabling only quiet hours explicitly permits normal cues
without needing a trusted local hour. Otherwise unavailable settings/time
conservatively suppress routine-controlled cues.

## RTC drift measurement

For each successful trusted SNTP synchronization, the time/acquisition owner
reads RTC **before writing the correction**, comparing the actual acquired
UTC calendar with the newly synchronized real system clock. The comparison
and original read timestamp are published as `DeviceSnapshot::rtc_drift`.
RTC read failure does not undo a successfully synchronized system clock: it is
an explicitly optional diagnostic, with original error in the drift snapshot.

A successful RTC correction is followed by a measured readback baseline.
The next SNTP comparison can publish signed ppm when both endpoints are valid
and their trusted UTC interval is at least 600 seconds. Positive offset/rate
means RTC is ahead/gaining. Baselines are session-local: boot, any new RTC
write, GPS synchronization or failed correction breaks the interval. The
first synchronization after restart reports offset only and starts a baseline;
no extra NVS writes are introduced for drift history.

RTC resolution is one second. Comparisons taking over one second are rejected.
The published offset uncertainty is two seconds; rate uncertainty is
`4 seconds * 1,000,000 / interval_seconds`. This is a read/quantization bound,
not a guarantee of Internet NTP accuracy. Short intervals are inherently poor
for judging a crystal's small ppm error. No oscillator calibration, automatic
frequency correction, network duty cycling or power saving is claimed.

Drift is historical: its original timestamps do not advance with refresh.
The optional read-only `/api/v1/rtc` and diagnostics JSON expose clock/routine
state and drift with null unavailable numeric values. HTTP remains disabled
by the Cody profile; this increment does not enable it.

Alarms, snooze, reminders, countdown registers and scheduled Deep-sleep wake
remain out of scope. See [RTC wiring and capabilities](cody-rtc.md).
