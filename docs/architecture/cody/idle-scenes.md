# Cody idle scenes and visible breathing

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

## Timing and priority

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

## Audio ownership

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

## Atlases and evidence

The 2026-10-05 appendices add breathing/gesture sequences, visible stressed-mouth
examples, scheduling and interruption rules to the Manga Status and Navigation
atlases. Their original pages/addenda are retained, and each revised PDF ends
with dated **Errata and corrections**. Repository copies are under `docs/Cody`;
the requested originals remain in the Cody Companion Obsidian atlas folder.

After static source review, the requested public CLI flash compiled checkpoint
`09ea3b70` with ESP-IDF 6.0.2 and eight jobs, then wrote it on COM10 with all three
hash checks passing and a final reset. No tests, standalone size check, simulator,
monitor/capture or physical visual/audio observation accompanied that operation.
Appearance under stress, gesture cadence, interruption, native/host parity,
quiet/mute suppression and physical whistle acoustics await
fresh command-specific authorization recorded in `TODO.md`.
