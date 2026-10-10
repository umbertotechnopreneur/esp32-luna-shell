# Cody behavior

This guide owns the implemented artistic mood, petting and idle policy.
The 2026-10-04/05 exploration also produced a disabled native wellbeing model;
its integration remains deferred. Routes belong to [navigation](navigation.md),
cue raster/resources to [personality channel](personality-icons.md), and clocks/
standby to [runtime](runtime.md). Future product decisions belong to
[planned-todo.md](../../planned-todo.md).

## MVP happiness authoring

`assets/cody/personality.yaml` is the canonical source of the test rules.
Its strict `cody.personality.v2` contract defines session-only artistic state,
positive-only artistic face binding, happiness bounds 0..100, initial value 50,
a ten-point caress gain and one-point decay per 60 awake seconds. Initial value, gain and
decay timing are authored test choices. Sadness is always `100 - happiness`;
there is no independent sadness counter or mood transition enum in this MVP.

The portable model accumulates elapsed microseconds and consumes whole decay
intervals, retaining the remainder. Caresses do not restart the minute. Arithmetic
intentionally saturates at 0 and 100; invalid configuration/state is rejected
instead of repaired. Decay at zero leaves no unpaid point debt. The awake clock
continues across routes and animation pause, excludes actual standby/park, and
rebases on resume. State is RAM-only and starts again at the authored initial
value after restart. Sleep outside the active renderer is not guessed from
wall time. This is the chosen MVP policy, not a persisted/offline mood service.

Only a recognized eligible upper/lower body stroke applies gain, once per
contact and under the existing petting cooldown. Apply that event even when
another cue suppresses the decorative heart. Happiness and derived sadness
retain their existing numeric rules. The minimal character director projects only
happiness above the authored initial value into a gentle smile and raised cheeks;
lower values do not create an unattended frown. Each accepted caress also leaves
a bounded 30-second active-time warmth tail after its existing heart cue. This
does not change breathing, acquired motion or cue priorities. See
[runtime](runtime.md#minimal-character-direction---2026-10-10) for admission,
pause and debug isolation. The same native director is integrated into Studio; driver compilation and
named synthetic character cases passed on 2026-10-11. Physical behavior remains open.
The native getter exposes an artistic snapshot under the existing display
lock. The Windows source reads the same YAML and displays synthetic diagnostic
values; it does not write firmware observations.

Generate firmware rules through the sole public CLI:

```powershell
.\scripts\device.ps1 generate -Generator cody-personality-rules
```

The generator rejects duplicates, unknown fields, remapped IDs, ambiguous
references, invalid numeric/resource bounds, unsupported clocks, scope or facial
bindings. It emits `personality_catalog.hpp`, `personality_rules.hpp`,
`CODY_petting_config.hpp` and `CODY_personality_config.hpp` under
`assets/cody/personality/generated/`, plus canonical source provenance in
`manifest.json`. Firmware uses those flash constants rather than
parsing YAML or evaluating arbitrary expressions at runtime. After editing the
YAML, regenerate through the console before expecting a rule change on Cody.
The `positive_only` revision was generated and its named native/Studio tests
passed on 2026-10-11. Dated counts, earlier failed fixture expectations and
firmware evidence live in [done.md](../../done.md); physical petting, audio and
stack headroom remain in [TODO.md](../../TODO.md).

## Permanent identities and configurable policy

Catalog entries explicitly carry both identity and name. Storage order never
defines meaning; generation sorts typed enums by the permanent IDs. `None = 0`
is reserved internally. Changing an existing ID/name pairing is rejected.

| Domain | Permanent ID | Canonical name | Meaning |
| --- | ---: | --- | --- |
| Event | 1 | `caress` | One recognized gentle stroke above the eyes OR below the mouth |
| Zone | 1 | `above_eyes` | Upper body region excluding the eyes/face middle |
| Zone | 2 | `below_mouth` | Lower body region excluding the mouth/face middle |
| Metric | 1 | `happiness` | The sole stored artistic level, 0..100 |
| Metric | 2 | `sadness` | Derived complement, never a second counter |
| Metric | 3 | `fatigue_minutes` | Dormant active-session minutes since an acknowledged screen break |

The fourteen icon identities remain Sleep1, Fear2, Joy3, Hunger4, Thirst5,
Heat6, Cold7, LowBattery8, Charging9, NoWifi10, Curiosity11, Confusion12,
Affection13 and Frustration14. Their precise snake-case names and policies are
listed in YAML. The gesture event `caress` and the displayed `affection` cue
are separate typed domains.

YAML authors initial/gain/decay values, gesture durations/travel/speed/margins,
reaction expiry, all icon priorities, interrupt policy, per-icon repeat cooldown,
fade/hold/stability, bounded following, Nose-relative anchors, low-battery enter/
exit thresholds, and global/per-icon sound enable, sound cooldown and expiry.
Fixed sensor ownership, lifecycle gates and renderer/PCM capacity are code
invariants. A YAML value outside those resource limits is rejected.

Current priorities are Sleep1000, LowBattery900, Affection200 and other cues100.
Only canonical, valid current device facts can enter arbitration. An admitted
urgent low-battery cue releases the heart's request, bypasses stability/minimum
hold and completes the existing fade before entering; it never cuts opacity
abruptly. Charging remains the current baseline until the automatic heart
expires. Replacement baselines overwrite it, so there is no queued charge.

Repeated requests for a displayed identity do not restart its fade or sound.
A per-identity admission timestamp prevents a recently dismissed cue from
reentering before its authored cooldown. Heart defaults to15 active seconds;
Charging/NoWifi20; other decorative cues15; Sleep/LowBattery0 to avoid delaying
an urgent current state. Cooldown persists across hidden/Micro/layout changes,
while paused animation freezes its clock. An expired heart is discarded instead
of replayed later. Gesture cooldown and icon cooldown are independent: a new
eligible caress may increase happiness while its decorative heart is suppressed.
Battery hysteresis enters at<=15% and releases at>=20%; unavailable/stale facts
clear the latch and cannot support a numeric alert.

## Dormant wellbeing logic

The build-time YAML includes an explicitly disabled wellbeing engine and a
disabled delivery gate. Portable native and host models implement internal
state/deadline logic; no session adapter, runtime timer, icon selector or UI is
connected. This preserves the one-active-mood MVP. Enabling the YAML engine
alone does not make an open chat a work session or start visible reminders.

Permanent session event IDs are `work_session_start = 2`,
`work_session_pause = 3`, `work_session_resume = 4`, `work_session_end = 5`,
`reminder_acknowledge = 6` and `reminder_snooze = 7`. Reminder identities are
`screen_break = 1`, `movement_break = 2`, `eye_rest = 3` and `drink_water = 4`.
Caress1 never acknowledges a reminder.

| Reminder | Active-work interval | Authored invitation | Source/basis |
| --- | ---: | --- | --- |
| `screen_break` | 60 minutes | 5-minute break | [HSE example](https://www.hse.gov.uk/msd/dse/work-routine.htm): frequent 5–10-minute breaks each hour |
| `movement_break` | 30 minutes | Get up and move; duration unspecified | [NHS sitting guidance](https://www.nhs.uk/live-well/exercise/why-sitting-too-much-is-bad-for-us/) |
| `eye_rest` | 20 minutes | Look 20 feet away for 20 seconds | [National Eye Institute](https://www.nei.nih.gov/eye-health-information/healthy-vision/nei-for-kids/healthy-vision-tips) |
| `drink_water` | 60 minutes | Optional water invitation; no fixed quantity | Cody product default; [NHS recommends drinking regularly](https://www.nhs.uk/live-well/eat-well/food-guidelines-and-food-labels/water-drinks-nutrition/) without an hourly interval |

These are configurable reminder defaults, with their source and interval basis
recorded in YAML. The proposed 120-minute break interval is superseded by the
HSE-based 60-minute default. Eye and movement reminders retain separate cycles.
The deferred delivery policy can combine simultaneous due reminders rather than
showing several icons in succession; it has not been implemented in the UI.

`fatigue_minutes` represents active session duration, not observed bodily
fatigue. Explicit Start begins a cycle; Pause freezes it, Resume continues and
End closes the session. Each enabled reminder becomes Due once at its deadline
and stays Due without repeated event emission. Acknowledgement starts its next
cycle; snooze installs one active-time deadline. Only acknowledging
`screen_break` resets fatigue. This acknowledges a user's explicit choice and
does not establish that a physical break, drink or movement actually occurred.
No elapsed-time update reads sensors, infers host liveness or catches up after
unknown/disconnected work time. Runtime integration remains deferred.


## Idle scenes

Screen 16 retains the same route and front-facing character. Breathing is the
continuous base presentation; Yawn, Whistle and Greeting are short artistic
accents, identified by `IdleSceneId`, never navigation indices or sensor values.

The mouth shares the existing breathing clock. Exhalation removes 75 percent of
the current opening and inhalation adds 16-24 percent of the remaining range.
The extra reopening boost fades out by the Worried base opening, preserving the
quiet expressions' existing tongue threshold. Width moves 18 percent toward its
authored minimum on exhalation and 18 percent toward its maximum on inhalation.
The thinner rest lip visibly closes into a smile; the tongue follows the lower
lip and remains clipped to the mouth. A zero-opening Stressed preset therefore
reaches 0.24 during inhalation even without measured motion. The
selected expression, contour timing, skin and tongue clipping remain composed
through the same evaluated rig.

### Timing and priority

The existing YAML `response.idle` section owns timing: `after_seconds` is the
minimum wait; a session PRNG draws a wait between one and two such intervals.
`duration_seconds` applies to each of the three gestures and `cooldown_seconds`
separates a completed/interrupted gesture from the next wait. Current authored
values give a 20-40-second eligible wait, a 3.2-second gesture and a 40-second
cooldown. A session shuffle bag presents each of the three identities once
in random order before refilling, excluding repetition across bag boundaries.
Firmware seeds once from ESP-IDF randomness; Windows
seeds its separate synthetic preview. Drawing another frame consumes no random
number unless a scheduling decision occurs.

Only the normal Idle/Main view schedules gestures, including its automatic
clock title. Setup, settings, explicit greeting, Debug, lifecycle transitions,
paused animation, sleeping/working/stressed expression overrides and current
personality/caress presentations are excluded. Touch activity, a held
body touch, authored motion stress, microphone activity and unrelated speaker
playback immediately release an active gesture. Active time excludes parked
intervals. Scheduling never updates device activity, timeout preferences,
navigation history or sensor timestamps; device standby may occur before a
random gesture is due.

- Yawn uses the existing broad opening and eyelid closure, with no sound.
- Whistle narrows the mouth to a small opening after breathing composition.
  Its width targets the rig's authored minimum (currently 0.5); it must not
  interpolate toward a value outside the accepted facial-channel range.
  At its first completed visible frame with weight at least 0.8 it attempts one
  original 300-ms two-note sound, stored as immutable flash PCM.
- Greeting adds a smile, viewer-right wink and cheek lift; it has no new text,
  limb, sound or route. Completion restores the underlying expression.

### Audio ownership

The existing personality audio owner performs admission, quiet-hour/volume
checks, codec writes, cancellation and real error publication. There is one
attempt per visible Whistle, with no queue or retry for busy, cooldown or muted
outcomes. The attempt belongs to the gesture generation, so pausing or temporarily
hiding the same gesture cannot replay a clip. Symbol sounds keep their existing
catalog and sound eligibility.

`PlaybackSnapshot.cue_id` carries the actual application identity supplied by
the write owner. Generic PCM uses zero; personality symbols retain their IDs;
Whistle uses the typed `IdleSceneId::Whistle` value at the API boundary. Measured
levels are published only after successful writes. Cody reads this identity from
the canonical observation, so its own whistle does not cancel the gesture or
turn the pursed mouth into speech. Other playback still has priority. No consumer
reads codecs or microphones. Windows mirrors visual dynamics and requests the
same native whistle PCM through its PC-only fake audio owner. It never publishes
PC playback as firmware samples or physical-device evidence.


## Status and evidence

Native-source and Studio integration do not establish gesture feel, physical
audio, performance or stack headroom. Current gates are in [TODO.md](../../TODO.md).
Historical generated-artwork, host and device results retain their dates and
checkpoints in [done.md](../../done.md). The original exploration and idle-atlas
production history are preserved at Git checkpoint `9956446`.
