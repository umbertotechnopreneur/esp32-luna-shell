# Cody clock, routines and RTC

The hardware acquisition owner restores trusted UTC from PCF85063 and publishes
the canonical clock. `LUNASHELL_time` observes the real system clock and converts
published time to the configured local zone; Cody never calls `gettimeofday`,
`localtime`, BSP or RTC reads directly. Timezone does not change stored UTC.

## Acquisition, configuration and failure

`DeviceSnapshot.clock` provides UTC epoch, local minute, source, configured period,
quiet status, validity/health, real error and original sample timestamp. Unknown
or stale time produces neutral/unavailable behavior, not a plausible hour.
A healthy system clock restored by RTC or confirmed by SNTP/GPS can continue
offline even if an RTC persistence write fails; that separate failure remains
visible. Restart retention still requires valid RTC contents and actual backup
power. No build date or guessed time is a fallback.

SNTP is not started at boot. The first usable Wi-Fi IP calls
`LUNASHELL_time_notify_wifi_online()` asynchronously; repeated IP events do not
create a second client. A rejected start retains its real error and may retry on
a later event. DHCP is not Internet/time confirmation. Only a valid 2024–2099 SNTP
callback confirms trusted system time and attempts trusted RTC write/readback.
Optional initial full-snapshot refresh failure is logged with its actual error,
not rewritten to success or used to create an RTC-unset boot loop; mandatory
driver initialization failure still stops startup.

Typed clock preferences use the existing internal-stack settings executor.
`clock_routine` is a single uint32 persistence value for routine/quiet flags,
timezone and quiet endpoints. Only an absent key logs and persists defaults;
wrong type, invalid encoding or storage failure leaves the value untouched and
returns an error. UTC (`UTC0`) is the default, independent of the chat timezone.
Rome has EU daylight-saving rules; Ho Chi Minh is fixed UTC+7. Unsupported zones
fail rather than selecting a substitute.

Default routines and quiet hours are enabled, with local interval 22:00–07:00.
Minutes must be 0–1439; same-day and overnight intervals are supported. Equal
endpoints while enabled are rejected; explicit disable means no quiet interval.
Morning is 06:00–12:00, afternoon 12:00–18:00, evening 18:00–22:00, night otherwise.

## Presentation and audio policy

After non-onboarding boot or genuine wake, trusted local time selects Good morning!,
Good afternoon!, Good evening! or Hello, night owl!; unavailable/stale time uses
Hello!. Period changes can retitle the awake scene without replaying entrance,
missed greetings or creating a wake schedule. Morning selects Happy and night
Drowsy, otherwise Calm. Explicit expression, boot/lifecycle and onboarding take
priority. Quiet evening/night softens contour/breath behavior with continuous
phase; it does not silently change touch/IMU gains, brightness, radio power or
saved volume.

The cue policy is allowed without a trusted clock when either routines or quiet
hours are disabled. When both are enabled, unavailable/stale time suppresses cues;
a valid quiet interval also suppresses them. Unavailable settings remain a real
error with conservative suppression. The original microclick also obeys its zero
volume/standby/hidden-scene gates. Both cue types share clock policy; see
[runtime](runtime.md). It is not a claim of safe audible loudness on the speaker.

## RTC drift diagnostics

At a valid SNTP synchronization, read RTC before correcting it and compare its
actual UTC against system UTC with the acquisition timestamp. A diagnostic RTC
read failure does not undo successful system synchronization. A trusted write
and successful readback establish an in-session baseline; the next confirmed
sync can estimate signed ppm (positive means RTC gains time) only with valid
endpoints and an interval of at least 600 seconds.

Boot, any intervening RTC write/GPS correction or failed correction breaks that
baseline. The first synchronization after restart yields an offset only: there
is no NVS drift history, oscillator calibration or automatic correction policy.
One-second RTC resolution bounds what can be inferred. Acquisition comparisons
exceeding one second are rejected; the conservative offset uncertainty is 2 s and
rate bound is `4e6 / interval_seconds` ppm. This is not NTP network accuracy.
Original diagnostic timestamps do not become fresh just because a consumer reads
them. The HTTP RTC projection exposes unavailable nullable fields rather than
zero drift; endpoint details belong in [HTTP API](../http-device-api.md).

## Actual board wiring and deferred alarms

On the ESP32-S3-Touch-AMOLED-1.75, U4 is PCF85063ATL with a 32.768-kHz crystal,
I2C address 0x51 on SDA15/SCL14. VDD is VCC-RTC from AXP2101, not an independent
coin cell. CLKOUT is unconnected and CLKOE is held low. Retention across removed
USB/battery or PMU-off conditions is not established by chip specifications and
still needs measurement on this board.

RTC INT goes to EXIO3/TCA9554 P3. The TCA9554 INT node has a pull-up but no ESP32
GPIO connection on the current PCB, so a PCF85063 alarm cannot directly wake
ESP32 deep sleep. ESP32 software timer wake is a different mechanism; normal
release sleep currently programs no schedule deadline. An RTC-to-free-GPIO wire
would be an explicit hardware change, not an existing firmware capability.

The chip has one alarm comparator (second/minute/hour/day/weekday), not month/year
or multiple alarms. Its 8-bit reload timer is limited to 255 seconds at 1 Hz or
4 hours 15 minutes at one tick/minute. Typical 0.22-µA chip current at 3.3 V/25°C with
clock output disabled is not total-board current or demonstrated time accuracy.
Alarm/countdown registers, reminders/snooze UI, alarm snapshots and an automatic
wake scheduler are not implemented. A future scheduler needs persisted UTC
deadlines, explicit wall-clock-change/error policy, active monotonic timing and
a supported wake source; it must not be inferred from the routine greeting.

Hardware detail is supported by the
[Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf),
[board hardware reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
and [NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf).
RTC retention, SNTP recovery, clock fault handling, timezone/DST transitions and
physical cue gating still require explicitly approved device observation. No
network-duty-cycle or battery-life improvement is established by this design.
