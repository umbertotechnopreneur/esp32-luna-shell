# Cody clock presentation

Trusted time and persistence belong to the [shared time service](../bsp/services/time.md).
Cody reads its canonical clock projection, including health, original timestamp,
timezone, period and quiet state. It never reads RTC, BSP or the system clock directly.

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

## Evidence and future reminders

Routines choose presentation; they do not create an alarm or wake scheduler.
Reminder, snooze and scheduled-wake ideas remain in [planned-todo.md](../../planned-todo.md).
Source and physical clock/audio evidence remain in [done.md](../../done.md),
with outstanding observations in [TODO.md](../../TODO.md).
