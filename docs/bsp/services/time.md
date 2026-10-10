# Trusted time, clock preferences and RTC

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

Default routines are enabled and quiet hours are disabled, with local interval 22:00–07:00.
Minutes must be 0–1439; same-day and overnight intervals are supported. Equal
endpoints while enabled are rejected; explicit disable means no quiet interval.
Morning is 06:00–12:00, afternoon 12:00–18:00, evening 18:00–22:00, night otherwise.

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
zero drift; endpoint details belong in [HTTP API](http-device-api.md).

## Board capability and consumers

RTC wiring and alarm limits belong to the
[Waveshare hardware reference](../boards/waveshare-esp32s3-touch-amoled-1.75/hardware.md#rtc-and-wake-limits).
[Cody clock presentation](../../cody/clock.md) owns greetings and artistic routine
effects. Reminder/snooze scheduling is deferred. RTC retention, SNTP recovery,
fault handling, timezone/DST and physical cue gating remain device observations
tracked in [TODO.md](../../../TODO.md).
