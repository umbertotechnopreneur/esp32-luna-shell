# Cody actions for Codex

This is the implementation plan for device-initiated interaction with Codex.
The batches are ordered by dependency, not by a claim that an action is already
available. Existing implementations still require their own acceptance.

The owner requested continued implementation while deferring voice and usage
screens. Current source work covers the existing hub's activity counts,
work-category acting, revised existing cues and the local chat bookmark's
confirmed state, pulse and dedicated sound. The read-only question preview can
open the existing signed Events inbox, with entry animation, navigation sound
and return to its originating page. A Turns-page shortcut opens the existing
PC-followed activity view, with explicit unavailable state, entry animation,
navigation sound and return to the selected local chat. Other new actions
remain open. No build, simulator, listening or device acceptance is implied by
source work.

## Shared interaction contract

- Identify the paired source, chat and, when relevant, the exact turn. Show a
  complete recognizable title or an explicit unavailable state; never act on
  a guessed chat. A tab switch alone is not an observed focus change.
- A control is enabled only when its actual provider capability and target are
  current. A local selection, send admission, provider receipt and observed
  result are distinct. Show offline, stale, unsupported, rejected and
  unconfirmed outcomes, retaining the target and allowing a safe exit.
- Use the native circular layout, lateral Back and the existing snapshot,
  display, animation, event and audio owners. Preserve explicit user choices,
  question tickets and the Sounds, volume and quiet-hours policies.
- Motion and sounds are driven by admitted semantic edges. Do not replay on
  redraw or reconnect, invent success, suppress errors, interrupt an existing
  question, or produce a backlog of delayed cues. Reserve stronger sounds for
  attention; do not turn ordinary progress into a repeating alarm.
- Native Codex approvals remain on the PC. A device question is a separate
  semantic request and must not impersonate an existing native approval.
- Changes need static review and separately authorized platform, simulator and
  physical acceptance according to the repository contract.

## Batch 1: Awareness and attention

| Action | UI and state | Muse and sound |
| --- | --- | --- |
| Current work / update | Codex hub shows current running and waiting turn counts when coverage is complete; incomplete or historical observations remain explicit. The followed chat retains its actual progress text. | Working uses a restrained focus pose; waiting uses an attentive look. Existing cues indicate real transitions only. |
| Open work | Browse active calls, chats, turns and events with source health, currentness and incomplete coverage. | A small navigation response; no repeated sound on refresh. |
| Questions and replies | Open the signed question flow, review the complete choices, send and show delivery, rejection or uncertainty. Show real reply-ready notices. | Attentive question and a quiet bell; delivery and response-ready have separate existing cues. |
| Usage | Open the existing statistics pages with real values and freshness. | Neutral glance; navigation feedback once. |

## Batch 2: Target and quick messages

| Action | UI and state | Muse and sound |
| --- | --- | --- |
| Follow a chat | The existing browser can save and reopen an exact local bookmark, with confirmed, saving and failed states. This does not change the chat followed by Codex; that selection still needs a supported end-to-end path. | The local saved result pulses and plays its dedicated cue after the frame retires. |
| Send a quick message / continue | Select a complete preset, inspect the destination and confirm Send. Show preparing, sending, accepted, rejected and unconfirmed; do not retry ambiguous admissions. | Gesture toward the message, subtle sending motion, separate receipt and failure cues. |
| Ask for an update, blocker, needed input or summary | Present these as explicit, reviewable messages with distinct intent. Show a returned response only when the provider actually makes it available. | Thinking while waiting; response-ready and attention use distinct cues. |
| Request a review | Show the exact review instruction and target before sending. A request is not a completed review. | Inspecting pose, then receipt or failure cue. |

The public [Codex app-server protocol](https://learn.chatgpt.com/docs/app-server)
documents turn start, steer and interruption for a client connected to its
server. These actions still require a supported connection to the intended
existing Desktop chat, with documented queue semantics and correlation. The
current audio-only experimental queue does not establish that connection. Do
not create a second Codex session or scrape the interface to simulate it.

## Batch 3: Work control and reminders

| Action | UI and state | Muse and sound |
| --- | --- | --- |
| Stop / pause | Identify the exact active turn, explain the chosen operation and confirm. Separate requested, accepted, observed stopped, rejected and unknown. | A deliberate stop gesture, then a soft acknowledgement or failure cue. |
| Open on PC | Select an exact chat and show requested, opened, unavailable or unconfirmed based on a supported Desktop mechanism. | Look toward the computer; short handoff cue. |
| Remind later | Select a duration for an exact notice or question. Show the due time and allow cancellation without resolving the underlying request. | Sleepy pause and a gentle single reminder when still eligible. |

The public protocol documents interruption, but the connection to the existing
Desktop session remains unverified. Pause and Desktop focus each require an
independently verified public capability; observing that a turn has stopped is
not a command API.
Reminder persistence, device sleep, clock changes and request expiry must be
specified with the event owner before implementation.

## Deferred voice and usage

The owner asked to wait on voice and usage screens. Keep the existing dictation
and consumption interfaces unchanged until a new explicit request.

## Sound direction

Use the owner's `Sfx` collection as source material, with distinct families for
navigation, work, receipt, attention, failure and handoff. Review perceived
quality on the actual speaker and preserve the existing quiet single-question
bell unless deliberately revised. Non-executable audio recordings do not
require a Defender scan. Authored digital levels do not establish perceived
loudness.
