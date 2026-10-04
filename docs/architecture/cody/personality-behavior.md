# Personality behavior exploration

This proposal records the 2026-10-04 product discussion. The petting experiment
is implemented separately in the existing personality channel; the needs,
mood and agent-session model below is deferred, not firmware behavior.

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
