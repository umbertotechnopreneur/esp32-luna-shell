# Personality behavior exploration

This note records the 2026-10-04/05 product discussion. The petting experiment
and the single-value happiness MVP are implemented in source. The broader
needs, mood categories and agent-session model below remain deferred.

## MVP happiness authoring

`assets/cody/personality.yaml` is the canonical source of the test rules.
Its strict `cody.personality.v2` contract defines session-only artistic state,
no facial binding, happiness bounds 0..100, initial value 50, a ten-point
carezza gain and one-point decay per 60 awake seconds. Initial value, gain and
decay timing are authored test choices. Sadness is always `100 - happiness`;
there is no independent sadness counter or mood transition enum in this MVP.

The portable model accumulates elapsed microseconds and consumes whole decay
intervals, retaining the remainder. Carezze do not restart the minute. Arithmetic
intentionally saturates at 0 and 100; invalid configuration/state is rejected
instead of repaired. Decay at zero leaves no unpaid point debt. The awake clock
continues across routes and animation pause, excludes actual standby/park, and
rebases on resume. State is RAM-only and starts again at the authored initial
value after restart. Sleep outside the active renderer is not guessed from
wall time. This is the chosen MVP policy, not a persisted/offline mood service.

Only a recognized eligible upper/lower body stroke applies gain, once per
contact and under the existing petting cooldown. Apply that event even when
another cue suppresses the decorative heart. Happiness and derived sadness
do not modify facial channels, breathing, motion or cue priorities yet.
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
YAML, regenerate and build before expecting a device rule change.

Generation has run. The focused suite compiled the portable native sources on
2026-10-05, then failed two new assertions that incorrectly expected a drawable
cue identity in an empty zero-opacity frame. The trace shows the intended cue
is owned by the FadeIn controller, while the rendered frame is correctly None.
Both assertions now inspect that controller identity and require an empty
frame. The corrected suite has not rerun; Python parity and independent host
fixtures were not reached. Combined firmware, interactive simulator and device
behavior remain unverified. The first-attempt report and trace are retained at
`artifacts/build-cody-companion-debug/evidence/personality-host-20261005-first-attempt/`.

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

## Independent finite states

Avoid one mutually exclusive enum containing Happy, Thirsty, Hungry, Sleeping
and Working. A character can be content, thirsty and working simultaneously.
Use a small hierarchical state model with separate typed domains:

| Domain | Candidate states | Authoritative input |
| --- | --- | --- |
| Activity | Idle, Listening, Working, WaitingForUser, Break, Sleeping | Explicit local interaction, host session events and existing lifecycle |
| Rest need | Rested, Due, Deferred, Recovering | Observed active session time and explicit pause/resume |
| Drink reminder | Quiet, Due, Deferred, Acknowledged | User-configured interval and explicit acknowledgement |
| Meal reminder | Quiet, Due, Deferred, Acknowledged | Opt-in schedule, trusted time and explicit acknowledgement |
| Mood | Neutral, Content, Uneasy, Frustrated | Bounded character reactions to named events |
| Reaction | None, Affection, Surprise, Fear | Recognized gesture or explicitly classified event with expiry |

Fear is a short reaction that may influence mood. Stress is an authored
character workload variable or a categorical state, not a measurement of the
person. Hunger, thirst and fatigue are character metaphors for reminders;
elapsed work time does not establish the user's physiological condition.
If continuous authored levels are later useful, document their units,
decay and thresholds, and use separate enter/exit thresholds to avoid chatter.

The current response composer already has bounded artistic `stress` and
`fatigue` fields: stress follows acquired motion/eye interactions, and fatigue
follows valid battery information. Preserve those causes. A future session
rest need must have its own named identity and a deliberate composition rule;
do not overwrite battery fatigue with an unrelated work-session timer.

## Session time

The host bridge should publish explicit identity, monotonic event order,
activity state and liveness. Decide whether the timer represents one task,
the selected conversation or a user-started work session. The first useful
definition is a user-started session, optionally informed by Codex activity.
An open conversation or a long-running background task alone does not prove
continuous work by the person.

Accumulate time only according to this documented session definition. Pause
on an explicit break; if the host disconnects, mark host activity Unknown
and stop counting host-derived activity. Reconcile genuine events on return
rather than guessing the missing interval. Device observations continue to
come exclusively from their canonical snapshot owner. Publish new host facts
through an explicit runtime owner before personality consumers use them.

Example rule to explore, not a health recommendation: after a configured
120-minute active session, move RestNeed from Rested to Due and offer one
combined break/drink invitation. A user-started local timer can work offline
under its own explicit policy.

## Selection and delivery

Keep domain states independent of the icon channel. A selector evaluates
currently eligible intentions and emits one latest desired cue. It retains no
backlog of decorative icons. Existing fade-in, minimum hold, fade-out and
bounded following remain the renderer's responsibility.

Honor the existing lifecycle and true device-state presentation rules first.
During normal idle/working presentation, consider direct user feedback before
ambient character needs; reevaluate device facts without letting an artistic
hold preserve invalid telemetry. Keep priorities explicit, stable and named.
Use expiry for reactions, cooldown for repeated suggestions, and hysteresis
or state transitions for persistent needs. A completed alert should not fire
again simply because its condition remains true.

Persist a due reminder as domain state when appropriate. This is different
from queuing icons. Snooze enters Deferred with one deadline; acknowledgement
enters Acknowledged and rearms only under the configured next-cycle rule.
Suppress delivery during dictation, quiet periods and sleep. On return,
reevaluate relevance and combine overdue drink/rest notices into one invitation.

Possible English device copy: "I've been working for a while. Break time?"
or "Let's take a break. Want some water?" Acknowledgement confirms the user's
choice, not measured drinking. A carezza reaction must not complete a reminder.

## First implementation sequence

1. Petting recognition and transient Affection in the existing symbol channel.
2. One explicit work-session clock with start/pause/resume/end and Unknown.
3. Rest and drink reminder states, one combined invitation, snooze and quiet.
4. Bounded mood reactions with explicit event causes.
5. Opt-in meal schedule and additional needs after the first flow is reviewed.

A finite-state policy runs only on relevant events or a low-rate deadline tick;
it does not need to run for every rendered pixel. Use fixed state and deadlines,
without a new framebuffer or sensor reads. Device performance remains a
measurement task requiring separate authorization.
