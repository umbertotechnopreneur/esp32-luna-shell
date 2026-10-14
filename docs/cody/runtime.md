# Cody runtime and lifecycle

The display worker owns scene/rig state under the shared display lock. Hardware,
settings, radios, power and audio retain their platform owners. A frame borrows
immutable state until every display transaction retires; partial writes never
become the next displayed-state reference. See [rendering](rendering.md) for the
damage/DMA contract and [onboarding](navigation.md) for route gates.

## Minimal character direction - 2026-10-10

The 2026-10-11 increment stays within the Codex companion. The shared director
adds restrained, continuously composed Thinking/Editing/Running accents,
one double blink on an observed attention edge, a smile and contour squash/stretch
on an observed same-turn reply, and a short puzzled reaction on interruption.
The PC approval route permits the attention performance while retaining its
existing controls. Other questions, setup and exact Debug/preset poses keep their
own presentation ownership.

Reply/interruption acting requires the exact current chat and turn identity from
coherent ACTIVITY/TECH publications. A missing/stale/mismatched turn, source gap,
pause, hidden route or rebase discards completion continuity. Repeated Ready or
Attention frames never restart a performance; animation elapsed time is not
evidence of task completion. Existing explicit compaction presentation, including
its Home indicator and exact sequence-end release, remains independent.

Character observations remain bounded to 96 bytes and their display-owned state
to 208 bytes after adding the exact turn identity and reaction clock. Transaction
copies remain bounded; compiled nested frames and physical stack headroom require
the final validation recorded in TODO.md. No task stack was enlarged.

Python only transports typed source observations into the native C++ director;
the new host code adds no operating-system observers or companion service.
Prepared native/Studio regression cases have not yet been executed for this
increment. Earlier 2026-10-11 results describe the preceding revision.

The initial `CODY_character_direction` revision added three artistic contributions after the
existing native response: gaze towards acquired body touch or visible information,
positive warmth after a recognized caress, and concentration/release during genuine
compaction. Rendering and touch keep the existing logical coordinates. Gaze can
follow the last fully retired manga bounds without a second face/icon evaluation.
An approval faces forward. No person tracking or new sensor sampling is introduced.

The display-lock owner retains bounded inputs (at most 96 bytes) and direction
state (at most 208 bytes). Existing native motion, blinking/protective eye closure,
audio-driven mouth opening, breathing, quiet hours, day/night routine and Desk/Away
policy retain their owners. Debug, explicit expression presets, setup and the
startup/Sleep/Wake lifecycle suppress these contributions and consume hidden events.
This work does not change expression debug screen 38 or Codex transport admission.

Compaction requires current canonical link/activity and technical observations with
matching chat and ACTIVITY revision. Concentration slightly narrows the mouth,
changes eye curves and raises one brow; the existing Confusion manga cue is requested
only when its current channel is free. A two-second stretch/smile requires an
observed current `Active -> Refreshed` transition of the same compaction sequence.
Source gaps, stale data, another chat, interruption and resume cannot manufacture
completion. They can suppress this optional release gesture. Existing Home
compaction text/status remains independently visible through the established UI.

Pause freezes artistic time while consuming source edges. A caress leaves a
30-second active-time warmth tail, blending back gently; positive happiness above
the authored initial value contributes additional warmth. Low/unattended happiness
does not create a sad expression. All numeric mood rules and their awake clock stay
unchanged. Physical timing, visual strength and nested stack headroom remain
unverified. The registered `cody-runtime` character cases and Studio execution
have dated passing evidence in [done.md](../../done.md).
Studio now binds this same director through its native mood/caress owner. The
ordinary rig supplies fresh acquired desktop pointer targets and recognized
caresses. SIMULATED CCP supplies visible text/pointer targets and coherence-qualified
compaction sequence IDs; changing the source or rotation breaks completion
continuity. Debug, explicit presets, setup and questions other than PC approval
disable the contribution.
The existing Long compaction and Refreshed / pending preview scenarios retain one
rendering-only sequence; opening the latter alone cannot fabricate an observed end.
Python does not duplicate the director's animation equations, and synthetic facts
never enter firmware snapshots. The preceding three-contribution revision's drivers
and named synthetic cases passed on 2026-10-11; this does not validate the later
Codex acting/power-cue increment or establish device behavior. Optional extra gestures stay in
[planned-todo.md](../../planned-todo.md); authorized generation/build/device
work is tracked in [TODO.md](../../TODO.md).

## Startup and expression sequencing

Cold boot or software reset first shows the approved VibeWare floppy insertion
animation on black: 42 original frames with 3.5 seconds of authored visible dwell
per cycle. Each native frame retires before its 80/90-ms dwell begins; transfers
and intentional startup DMA parking can add wall time. The current cycle finishes
only after the real Ready service checkpoint. The original Sleeping/Waking/Ready
clocks remain frozen under the introduction, including first-run greeting timers.
The unchanged GIF and shared flash RLE package live in
`assets/branding/VibeWare/`; the native path adds no LVGL or GIF-decoder dependency.

After the introduction, startup follows Sleeping 01 → Waking 02 → Ready 03. The animated
character fades in over 2 seconds, then Waking receives 1.5 seconds and Ready
receives 2 seconds (5.5 active seconds altogether, excluding real service waits).
Real startup checkpoints gate advancement: a service wait does not consume the
next phase's active duration. The VibeWare introduction precedes Cody, with no
Debug HUD or numeric progress invented from animation time. Actual deep-sleep
and ordinary standby wake keep their existing lifecycle and omit the logo.

First setup selected by an absent saved Wi-Fi profile enters Hello 04 with Happy and `Hi, I'm Cody!` at
`(233,225,130)`. Its 13 characters use 70 ms each; after the complete text has
actually retired to the panel it remains readable for one active second before
05. There is no second bitmap intro. Normal startup now selects network setup,
the optional Codex choice (40), reconnect waiting (42), or authenticated PC search
from the canonical saved-profile/link state; see [onboarding](navigation.md).
An existing saved Wi-Fi profile receives a passive `Connecting to Wi-Fi` /
`Please wait` presentation for up to 15 seconds before recovery choices appear.
Valid association and IP advance immediately; the interval adds no polling task
or forced minimum delay and does not infer Internet or authenticated PC presence.
Only explicit development force-first-boot replays 04 regardless of that state.
Returning devices with validated first-boot false receive one four-second
random Affection or Joy request on entering Idle, using the existing icon and
completed-frame SFX owner. Urgent symbols and real interactions retain priority;
hidden requests are dropped. No pairing or credential keys are rewritten by this
presentation change. The requested 2026-10-06 Navigation Atlas revision documents
this boot sequence, with earlier pages retained and dated errata appended.

`cody_firstboot` is a validated persisted boolean. Only a genuinely absent key
logs and persists the default `true`; invalid type/value or storage failure stops
that operation. `CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT` is an explicit `y`/`n`
development boolean. The Cody profile sets it to `n`, so a valid saved false is
authoritative. After the current pairing attempt has healthy Wi-Fi and a nonempty
IP, `persist_first_boot(false)` writes and reads back the value before success UI;
write, readback or mismatch failure stays visible and retryable. Completing Hello
text alone never clears the key.

Expressions are separate from routes. Stable, case-sensitive catalog keys include
`calm`, `happy`, `sleeping`, `drowsy`, `embarrassed`, `boot.sleep`, `boot.waking`
and `boot.ready`; ambiguous `35` is rejected, while Sleep 35A and Wake 35B are
distinct. A session expression change preserves breathing/blink phase and is
available after boot reaches idle. Power/lifecycle phases take priority; choosing
a sleeping face alone does not request physical standby. Happy keeps both eyes
open between natural blinks.

## Standby and wake

Sleep preparation closes the eyes over 400 ms, then holds the closed breathing
pose for 300 ms. Acknowledgement follows the completed frame, not a queued draw.
The power owner parks producers and applies the shared 800-ms panel fade. Its
3-second preparation deadline and the renderer's 500-ms failure deadline are
failure bounds, not frame-rate promises. Failed sleep rolls back with its real
error; failed wake leaves the panel dark rather than presenting a partial success.
Standalone wireless restoration is optional for graphical wake: a failed restore
must first stop both radios successfully. Only that known-off state permits Cody
and touch to resume; uncertain cleanup still rejects wake. The original radio
error remains in the power snapshot, return value, trace and logs until verified
radio recovery. Ordinary touch/motion publication cannot erase it. A subsequent
standby reasserts radio shutdown through its owner after any explicit pairing.

For a retained Cody/Codex association, the radio owners normally stay alive and
authenticated traffic continues during the display transition. Completed wake
prompts an existing Wi-Fi retry and CCP discovery once; acquired IP prompts CCP
again. This does not increase periodic standby traffic or fabricate Online.
See [wake reconnect](../bsp/services/connectivity-pairing.md#cody-wake-reconnect).

Software wake first renders Sleeping behind the dark panel, exposes it through
the shared 800-ms panel fade, then gives Waking 1.5 seconds and Ready 2 seconds. It does
not multiply a second 2-second character fade over the panel fade. Route, pending
operation, debug/pause state and scene entrance/text clocks stay in RAM; hidden
routes do not update during the lifecycle wake sequence. Before showing the
retained route, reconcile the canonical pairing/Wi-Fi state. A BLE session closed
while asleep must not resurrect its old QR. Rebase the IMU response baseline.

Cold boot/reset still uses normal startup and first-boot policy. Actual deep sleep
resets RAM and starts Wake 35B rather than the cold-boot introduction, then
re-evaluates the same saved network/link admission. Ordinary standby resumes its
retained route; there is no persisted route-resume contract. The power implementation and
profile-specific silicon-sleep policy remain in
[power management](../bsp/services/power-management.md).

Every authored scene supplies `standby_override_seconds`: zero inherits the saved
user timeout, a positive value replaces it only for the active scene. Invitation
05 and installation 06 use 60 seconds; real pairing 07 uses 90 seconds. The unused
scan definition, Online 09, Debug-only 39, 10, 36 and 13-15 use 60 seconds.
Connect Codex 12 uses 180 seconds; successful canonical code delivery refreshes
its anchor once, so preparation time does not consume the code-reading interval.
Hello 04, error 08
and look 11 inherit. Scene entry supplies a fresh
inactivity baseline and touch refreshes it. Genuine canonical progress in the
current pairing attempt also refreshes its 90-second anchor without fabricating
a touch event. Repeated unchanged snapshots do not refresh it.
Departure/parking clears the override.
This never writes NVS or extends the backend's five-minute pairing deadline.

Standby retains the active route. In particular, wake from screen 09 returns to
09; the wake contact does not invoke its Continue action. Screen 39 is available
only in Debug builds and one awake tap dismisses it. Release contains no generated
scene definition or expression-catalog mapping for 39.

## Reusable scenes and text

### Event-to-screen contract

| Admitted source event | Visible result | Boundary |
| --- | --- | --- |
| Retained link enters Discovering | Connecting / Retrying PC connection | Discovery already retries every two seconds; no idle timer can claim Connected |
| Authenticated READY and COMMITTED | Connected, then current followed activity | Eight-second transport expiry remains independent of activity freshness |
| UserPromptSubmit | Working and the exact newly followed chat | Background tools cannot select another chat |
| PreToolUse / PostToolUse | Working, Editing code or Running command | Ordinary tool changes do not cancel active compaction |
| Native token_count | Exact Last call, Window, Free, Total, Input and Output where available | Same verified chat/turn, original source time, no count inferred from prose |
| PreCompact | Orange Compacting on Home and orange technical heading/banner; compaction character cue | One sound admission per visible chat/turn/sequence, subject to audio policy |
| PostCompact | Context refreshed; wait for a subsequent token sample | Missing start, interruption or lost source never implies successful completion |
| Stop | Reply ready, then idle commands and retained statistics | A completed response does not prove task success |
| PermissionRequest | Waiting for you / Approve on your PC | Native approval remains on the PC |
| Transport expires | Connecting while retained discovery is active | Preserve the canonical timeout error; do not request a new pairing |

Lifecycle diagnostics retain event names, phase, compaction sequence and hashed
chat/turn identities. They exclude prompt text and raw identities. Host fixtures
exercise the real activity owner, authenticated packets, Studio projection and
native panel pixels; these synthetic captures do not prove physical sound or
that a particular live compaction hook was delivered.

### Semantic questions and other-chat replies, 2026-10-14

The synchronous `UserPromptSubmit` hook uses the documented
[`additionalContext` output](https://learn.chatgpt.com/docs/hooks) to tell the
agent to route semantic choices through `ccp_ask_question` when a retained
physical Cody has recently advertised the authenticated events extension.
The context is fixed instruction text; prompt, response and tool-argument
contents are discarded before hook ingestion. An explicit PC-only preference
takes precedence, and simulator routing still requires explicit user intent.

The agent creates the actual question with two or three complete English ASCII
choices, retaining stable IDs and original meaning. Labels admit 63 characters
and details 255; Cody shows two choices at a time and scrolls to the third.
Questions needing free text or content outside the bounds remain on the PC.
The originating caller keeps its private ticket/capability, polls for the real
answer, and acknowledges the returned `answer_id` before acting. Receipt alone
does not mean **Reply sent**. Expiry, cancellation and disconnection never select
an answer, and an ambiguous enqueue is not automatically repeated.

The bounded interaction owner retains multiple genuine MCP questions. Its queue
publishes actual request identities and two pending summaries at a time. Explicit
navigation is signed and does not change the followed PC chat; a reply being sent
or awaiting recipient acknowledgement cannot be displaced. Every caller retains
ownership of its own answer and cancellation capability. A genuine `Stop` from
another chat queues the typed **ReplyReady** event once per native turn. For an
eligible physical target, its existing title owner reads the exact indexed chat
metadata once and revalidates the original session, turn, checkout and ready
phase after IO. It preserves the followed title cache and chat selection.
The event uses its project and complete current title when at most 63 characters;
otherwise it says **Open the other chat in Codex**. The native screen has one
mint **Reply ready** headline, the chat title, subordinate project and **Got it**.
Generic notices also omit identical title/body repetition. No GUID, transcript,
task-success inference or polling is introduced. A prompt retires native notices
only for that chat; dismissal still requires the authenticated owner acknowledgement.

Native `request_user_input` prompts already open in Desktop are not imported.
The current Desktop app-server uses stdio without a verified public attach
endpoint; starting another server would not own its requests. The official
[App Server protocol](https://learn.chatgpt.com/docs/app-server) describes
`item/tool/requestUserInput` and `serverRequest/resolved`, but those messages do
not establish access to this running Desktop connection. Native permissions and
existing native questions stay on the PC. No private UI automation, transcript
question scraping, invented request registry or fabricated native reply is used.

The earlier 2026-10-14 audit established that asynchronous tool `accepted` can
precede a user response, so `PostToolUse` cannot prove question completion.
It also identified two rejected turn identities as subagents: parent/child
correlation must keep exact identity checks. These retained findings contain no
chat text or raw session identifiers and do not constitute a live question test.

### Companion information panels

Home retains the character, actual activity title and independently aged model
row above four lower panels. Tokens shows a Free token hero, occupied-context
progress arc and Last/Window. Model & Context shows
qualitative catalog price and reasoning effort; the shared header carries the
model name once. Session & Link shows connection state, the device's own canonical
Wi-Fi IP and device identity, independently valid IMU activity, compilation date
and time in the compiling PC's local timezone, that actual timezone identity and
the compiling computer's name. Chat shows the actual native title, the measured
elapsed time of the current turn and its authenticated activity description.
It does not infer a whole-chat lifetime or inspect raw commands/transcripts.
Long titles, activity and build labels scroll within clipped native rows, and
rotation allows enough visible time to read them. The former full-screen Activity
page and numeric page footer are no longer part of passive Home navigation.

A display-owned, bounded model identity latches one Expensive-price notice per
model change. It waits for current model/cost facts and a completely presented
Model panel; late price metadata can complete the same pending decision.
Stale data, reconnects, standby and carousel rotation do not rearm an attempted
notice. A newer model supersedes an unseen notice. Busy, mute, quiet hours and
recording retain the existing audio-owner admission policy without delayed replay.
The notice repeats one 1,593-sample CC0 Kenney plink five times with four 100-ms
gaps: 897.8 ms authored playback and 3,186 flash bytes. The existing audio worker
resamples each bounded chunk into its existing scratch buffer and keeps its
current duplex clock, cancellation generation and error reporting. Source,
license and reproducible generation belong to
[the model-cost asset](../../assets/cody/audio/model-cost/README.md).

The footer shows "Use @CCP in Codex and ask for the available commands" in three
complete right-to-left passes, then disappears for that display-owner lifetime.
It enters fully off the right edge and exits fully off the left, moving two
pixels every 50 ms with the existing active scene clock. Hidden presentation
pauses it; panel/chat changes, standby and short reconnections never replay it.
Ready for More shares that same finite introduction and has no replacement
footer after its third pass;
after 25 active seconds its title becomes Ready to team up. This changes no link
state or measurement timestamp. Compaction and attention suppress the hints
without creating a task, poll or wake lease.

The hero uses the `TT` unit. Values below 1000 stay exact; larger values display
completed decimal thousands with `K` and period grouping, such as `Free 319K TT`.
Free is subtracted from the original coherent counters before abbreviation.
Canonical and protocol counters retain full precision. The arc requires a
coherent current pair; compaction uses an orange indeterminate accent rather
than an invented progress percentage. Missing, stale, invalid and over-limit
observations keep explicit nonnumeric states.

A shared native carousel cycles Tokens, Model & Context, Session & Link, then
Tokens after at least eight visible seconds per panel. Session & Link shows the
actual canonical chat title without a `Chat` prefix or opaque ID. Long titles
scroll within (73,315,320,28), with a 1.2-second start hold, 40 pixels per second
in 50-ms steps and a 1.8-second end hold. The native glyph width extends the
carousel dwell until the entire title has been visible. Same-title metadata
refreshes preserve position; a new title receives a complete reading interval.
The original title age plus local time since receipt expires after 45 seconds;
missing, unsupported, failed and stale metadata remain explicit. No task or
source polling is added for glyph movement. A short released passive tap
advances immediately and restarts the interval. Hidden views, standby and
current compaction pause automatic rotation, without accumulating catch-up
transitions. Compaction selects Tokens and starts a fresh interval when it ends;
active touch gestures also suspend the automatic advance. A deliberate shake
selects Tokens without changing a remote request, chat,
approval or read acknowledgement. These passive panels have no lateral Back.
Setup, voice input, remote questions, PC approvals and Debug retain their real
controls and presentation priority. The retained panel resumes when the
interrupting route ends. A panel selection never resets the shared character,
activity title or compaction sound latch.

Touch recognition retains the contact's origin, displayed orientation and original
timestamps. More than 12 pixels of displacement, a contact longer than 450 ms,
a discontinuity, a control action, a caress or a long settings hold invalidates
navigation. A view opens only after the acquired release; that contact never
reaches controls on the new view. Eye reactions retain their existing action.

Shake recognition consumes only distinct, fresh canonical measured acceleration.
The acquisition owner publishes a separate bias-corrected gesture projection
before software smoothing, from the same successful read and original timestamp.
Filtered animation/fusion inputs retain their existing smoothing. Current component
health, gesture validity and timestamp agreement are required; there is no driver
read or filtered-data fallback in the renderer.
Three alternating acceleration peaks within 900 ms require a previously settled
baseline; hysteresis, a 1.5-second cooldown and a new calm interval prevent replay.
Strong opposing samples qualify even when the 100-ms acquisition cadence misses
their intervening zero crossing. Sustained same-direction samples do not qualify.
Wake suppresses navigation recognition for two seconds. Missing, stale or invalid
IMU data cancels recognition. These initial thresholds require physical calibration;
source review and serial startup do not establish gesture quality while walking.
Pausing artistic animation does not pause passive navigation sampling.

The dashboard shares native formatting, validity, typed panel identities, fonts
and raster code with Studio. Panel content and coherent observations belong to
persistent renderer storage, never nested snapshot copies. Dashboard labels and
numbers bypass typewriter timing and its audio. The common character transform
is (233,122), scale 73; the title begins at y=190, model at y=236 and lower panel
at y=268. Fonts are isolated from setup and event typography. Native damage
tracks upper identity, lower panel and ticker revisions independently; hiding
the dashboard invalidates its previous bounds. The semantic scene data remains
available to the event and sound owners while its superseded footer is hidden.

| Metric | Actual source and meaning | Availability in this integration |
| --- | --- | --- |
| Model | Current exact native turn-context model, or a fresh genuine hook when native context is absent/expired | Original source age; no model-name alias or execution/rerouting claim |
| Reasoning effort | Native turn-context effort, not a global default | Available only when the exact followed turn supplies it |
| Last call / Window | Native last-token total and explicitly supplied model context window from one token sample | Independently present; Last call is not cumulative chat spending |
| Free / percent / colored bar | Window minus Last call, only from the same valid sample | Not a guarantee about the next request's usable budget; over-limit and stale states stay explicit |
| Session cumulative tokens | Native total-token total; subcategories are not added again | Exact observed cumulative value, separate from the current window |
| Automatic compaction threshold | Explicit source threshold, distinct from capacity | UNAVAILABLE |
| Turn elapsed | Host monotonic duration since genuine UserPromptSubmit, transported as a duration | Available only for the admitted observed turn; unobserved time is not operation work |
| Observed work | Native accumulated active observation intervals | Explicitly labelled Observed, never operation elapsed |
| Compaction | Admitted PreCompact / PostCompact for the followed chat | Separate lifecycle; no progress percent or timeout success |
| Link and activity age | Authenticated link presence versus genuine activity update age | Independent; a heartbeat does not refresh model/context provenance |

Official App Server exposes `thread/tokenUsage/updated`, but the installed desktop
App Server uses its own stdio channel; the plugin does not subscribe through a
second server. Instead, the existing broker verifies the genuine hook-selected
session and checkout against local native journal metadata, using the original
locator or a read-only state-index lookup. Only allowlisted session, turn-context
and token-count metadata contributes. Prompt, message and tool-output bodies do
not provide measurements. Reads, paths, line sizes and source identities are
bounded; partial, replaced, truncated or contradictory sources fail closed.
Native token timestamps keep their original age; repeated FIND replies do not
refresh them. Token samples expire after 120 seconds, independently of the
eight-second transport lease. Missing fields and thresholds are not inferred.
A fresh hook model keeps its own timestamp when an older native context expires;
native effort is unavailable in that projection rather than inheriting the newer
model age. Read-only CCP connection status exposes the same atomic activity and
technical bundle without creating a new observer.
Unsupported optional fields remain unavailable internally and are hidden in the
passive UI, rather than filling the page with unavailable labels.

Compaction remains visible in orange on Home and all panels. Its genuine current
character cue takes precedence over ordinary authored working cues, with sound
admitted once per genuine chat/turn/compaction sequence through complete-frame
retirement. Attention or hiding the view does not replay the same sequence.
Ordinary tool phases cannot
cancel it. A genuine completion may show Context refreshed while context counters
remain unavailable or awaiting a coherent new measurement. Loss of updates,
interruption, an orphan completion and disconnected state remain distinct.
After PostCompact, a token sample predating that genuine completion stays
unavailable until a subsequent native observation; the boundary never renews
the old sample or model timestamp. Source ages remain independent;
elapsed time alone never proves compaction completed.

Current technical values also require the exact ACTIVITY revision, not merely
the same chat. ACTIVITY and TECH may arrive separately: during that gap, model,
context and turn duration are UNAVAILABLE. A previously known same-chat Active
compaction remains explicitly STALE until coherent metadata arrives. A different
chat cannot borrow its lifecycle. An orphan boundary says `Compaction not fully
observed`, covering both a missing start and a missing completion. A genuine
native permission request retains `Waiting for you` / `Approve on your PC`,
its notification and the frozen project row while the amber compaction indicator
remains visible. Native approvals still happen only on the PC.

The source lease is 120 seconds since the last genuine lifecycle hook. The
current hook source has start/end boundaries, without an ongoing confirmation
signal; repeated broker replies do not renew this lease. A compaction lasting
longer than that remains STALE, never completed by timeout. A future supported
ongoing observer must provide a distinct confirmation age before a long-running
operation can remain current beyond that lease. The explicit Studio synthetic
long-compaction scenario exercises presentation, not actual hook reachability.

### Desk and Away

The existing Make it yours screen includes a `Usage` row, toggling `Desk` and
`Away` through the settings owner. Its five 34-pixel rows begin at y=190 with a
38-pixel step; the last ends at y=376, before the compact footer at y=390. The
40-pixel title begins at y=146 and ends before the controls. These rectangles
remain inside the central square; the separate lateral Back target is intact.

`cody_usage` is a strict persisted uint32 boundary value (Desk=0, Away=1).
Only an absent key receives a logged Desk default followed by exact readback.
Storage/type/value/readback failures publish independent mode validity and the
original error; the row shows N/A rather than a guessed mode. Explicit mode
changes serialize with other preferences and preserve sound, wake, rotation,
clock, preferred name and their independent provenance. Consumers use only the
canonical bounded options projection, with no NVS or sensor read in rendering.

The shared `CODY_companion_mode.hpp` policy calms artistic breathing and outline
motion, and suppresses optional idle accessory scenes on awake passive Home and
panel routes. Setup, semantic question controls and manually selected Debug
expressions retain their existing behavior. Activity headlines, important sounds,
the compaction indicator, tap/shake, source freshness and service decisions stay
with their existing owners. Studio provides a clearly SIMULATED preview toggle
using that same native helper and resets it before live or Debug/event views.

Away makes no radio, CPU frequency, voice, standby or new transport change.
The regular gesture service cadence remains intact; lowering it without
separating canonical motion sampling could miss deliberate shake peaks. Reduced
accessory motion is implemented; CPU time, energy use and walking rejection are
not measured by source review. Reachability continues to follow the actual link.

The options projection is guarded at 256 bytes in the render path. Mode fields
add one enum, one validity flag and one esp_err_t to the canonical preferences;
padding and compiled structure size still require the next authorized build.
One additional preference control belongs to the existing persistent content
owner. Host Away state is one boolean; no task stack was increased. Previously
recorded boot stack headroom and compiled frames predate these changes and do not
validate the new nested render/projection path.

`CODY_scene` resolves immutable definitions by `SceneId`, prepares cloud/title and
an optional bounded content slot, advances fixed-capacity state and plans old/new
damage. Authored background, title visibility, entrance placement/timing and text
cadence are explicit; unsupported schema/IDs fail rather than select a fallback.
Entrance uses smoothstep before snapshot-driven response is composed.

Entrance and 64-bit `SceneFrame::text_elapsed_us` are separate active clocks;
pause/standby advances neither. Retitling or replacing progress restarts text
without replaying the cloud entrance. Title/body/footer and look captions use
captured typed prefixes, normally 45 ms/character after their preceding text.
Long-lived Saving/error/hint text anchors a new local sequence to the current
clock. Look scroll/save does not restart the preview entrance or row clocks.

Content callbacks borrow prepared immutable state and only the slot/tile
intersection, including original stride/capacity/byte order. Bounds stay below the
title and inside the circle, including QR quiet zones. No per-frame allocation,
hardware read, display transaction or retained tile is permitted. `revision`
changes for visual mutations; removing content dirties old bounds. Captured
`visible_characters` counts only displayed/clipped prefixes. `text_generation`
changes on entry/replacement/scroll, not ordinary prefix progress, so newly exposed
old labels prime a silent audio baseline. Commit state only after full retirement.

`set_scene_title()` restarts only text; `scene_content_characters()` prepares bounded
prefixes. Look 11 calls `update_look_text(scene, control_revision, changed)` once
after scene advancement, then captures content before drawing; it preserves an
already-true `changed` flag and does not attach mutable state on every tile.

## Optional audio

Boot cues start with original procedural `audio-ready` (0.18 s), followed by the
explicit `audio.eyes_open_cue: boot-rise` in `assets/cody/runtime.yaml`. BootRise
is a 1.8-second rising chime: 79,380 mono PCM16 bytes at 22,050 Hz. On 2026-10-10
this replaced the earlier chime-plus-murmur greeting; historical vocal auditions
remain separate and are not embedded. Source/rendered hash mismatch or stale
authoring fails preparation rather than selecting another sound.
See [boot and Codex action authoring](../../assets/cody/audio/codex-actions/README.md)
for transformations and owner-confirmed commercial-use licence provenance.
Preparation validates the
flash PCM; completed boot requires a verified canonical clock before applying
the quiet-hours policy. An unavailable/stale clock admits one pending boot request,
not a playback completion. The existing companion worker checks only that pending
request's canonical clock at most once per second, including static/paused routes.
A temporary 4 KiB PSRAM audio worker rechecks clock policy and active power state
before the real PCM operations, then releases itself after completion, failure or
a definitive quiet-hours skip. The request may remain unfulfilled through standby;
completed/skipped/failed requests never become ordinary software-wake sounds. Clock
waiting has no timeout that fabricates completion, and startup keeps an optional
audio admission/playback failure visible without aborting visual startup.
The base assets use 22,050 Hz and authored volume 100; the
optional voice-AFE feature has its separate matched-rate configuration. Shared
transport configuration is authoritative: a conflicting generic playback rate
returns the real error rather than reconfiguring an open microphone unnoticed.

`CODY_codex_audio.hpp` selects typed Codex action cues from immutable mono
16 kHz PCM. The current cue set, durations and byte totals are owned by the
[authoring manifest](../../assets/cody/audio/codex-actions/README.md), within
the existing audio-owner limit. The same optional personality-audio worker and one-slot mailbox own
playback; there is no new worker, I2S owner or event replay queue. Independent
channel generations cancel hidden Codex presentation and hidden personality
symbols without cancelling each other; standby invalidates both. Codex starts
suspended and its presenter enables only visible companion transitions.
Working/Reading/Editing/Running requests have a 1.5-second activity rate limit.
Other notices skip that limit but never preempt a cue already in flight.
Busy, expired, disabled and quiet-hours dispositions remain explicit, while
actual failures return their original error and publish through the canonical
snapshot. `Dispatched` does not mean playback completed. A response-ready cue
means the assistant finished its response, without asserting task success.
The reserved Reading asset requires an actual observed event before use.

The optional keyboard microclick is an original rounded noise impulse: 320 mono
PCM16 samples at 16 kHz / 20 ms, 640 flash bytes. Completed-frame character deltas
send bounded notifications only; rendering performs no codec I/O or audio wait.
The audio owner coalesces at most 20 taps/second, drops busy or expired
notifications (150 ms), and never replays a backlog. Entry, replacement, scroll
and wake reset the silent baseline; unchanged frames cannot create taps.

It preserves actual codec volume and the already shared transport clock, converts
only this cue to the active rate, and does not reconfigure the microphone. Zero
volume, quiet hours, hidden/parked scenes and standby suppress output. Unavailable
trusted time suppresses keyboard microclicks only when both routines and quiet-hours policy are
enabled; disabling either allows cues without that clock. Unavailable policy
settings still suppress with their real error. Audio/speaker-excluded profiles
skip preparation while preserving visual startup. Runtime playback failure is
logged/published without stopping this optional presentation effect.

The authored fixed audio RAM budget is approximately 6.5 KiB: 4,096-byte task
stack + 2,400-byte transport scratch + small fixed state, not measured linked-image
RAM. The stack and PCM scratch now use external BSS in PSRAM, as does the
separate 4-KiB personality-audio stack; task controls/mailboxes and codec DMA
remain internal. Speaker loudness/gentleness remains a listening check. The separate ten
[archive cue candidates](../../assets/cody/audio/gentle-sfx/README.md) exist only
as private `artifacts/audio/cody-gentle-sfx/` previews/audition/manifest. They are
not firmware or automatic cues. Perpetual commercial-use licences were confirmed
by the owner on 2026-10-04; perceptual review is pending.

## Windows preview

The [simulator guide](../../simulator/windows/README.md#host-audio-and-inspection-ownership)
owns the host audio DLL, PC playback, inspection typography and browser links.
These are synthetic host capabilities with separate execution evidence.

## Device diagnostics

Debug FPS uses completed frames over real elapsed time. CPU1/core0 and CPU2/core1
come from canonical idle/IPC measurements with their own timestamps; invalid or
stale data shows unavailable, never an estimated/clamped value. Measurement
windows begin after startup at 04 or in the ordinary main companion view when
retained pairing skips that first-run greeting. The automatic Idle clock greeting
and a manually selected sleeping face in the main view remain eligible. The
five compact rows below the routine title show FPS/CPU, IPv4, free internal
RAM/PSRAM, SD presence/access and free space. The upper overlay uses a quieter
date, larger `HH:MM` without seconds and consistent battery, Wi-Fi and Bluetooth
icons. Because this board has no card-detect pin, the SD row distinguishes a
verified card response from `SD UNCONFIRMED`. Personality icons keep their
independent anchor above the cloud. The CPU values
represent total busy time per core, including tasks other than Cody. No extra
sensor polling is introduced. The HUD preference is session-scoped under the
display lock and gated by scene `readout_visible`. Authored scenes admit the HUD
unless a visible bottom action occupies its area; Release excludes it.

Power is dedicated to standby/wake and deliberate shutdown in Debug and Release.
A short click toggles software standby; a valid three-second continuous hold
presents `Good night` with `Release Power` directly below it, retires the complete
Sleep presentation, and requests the
power owner's gradual fade and coordinated PMU shutdown. Screenshot and restart
no longer consume PWR; BOOT diagnostics remain unchanged. The old Power-screenshot
setter returns `ESP_ERR_NOT_SUPPORTED`, and its getter reports disabled.
Other screenshot diagnostics retain the existing SD owner and real failures.

The shutdown presentation blocks competing touch/scene/voice overlays, suspends
character/typewriter audio, and waits for any existing screenshot operation with
a bounded deadline. It acknowledges only complete retired frames, including a
400-ms greeting dwell after retirement. The passive `Release Power` instruction
shares that retired frame and asks the user to release the admitted hold before
the configured 10-second emergency hardware cutoff can interrupt owner drains. A failed shutdown restores a visible
`Shutdown failed` caption with the original hexadecimal error when display
recovery succeeds; a subsequent touch dismisses it. This is source implementation,
not evidence that power physically disappeared. See the
[power transaction and hardware limitations](../bsp/services/power-management.md#power-key-farewell-and-pmu-shutdown).

The CST9217 path retains 8-bit panel commands, 2-ms address-write settling/configuration
before IRQ registration and real zero-contact release reports. ISR work is only
timestamp/readiness signalling. Bounded acquired reports are dispatched in order
after complete frame retirement, preserving acquisition timestamps; invalid
marker/count/transport failure stops the mandatory consumer. When no report is
pending, the result is `ESP_ERR_NOT_FOUND`, invalid with timestamp zero, not a
fabricated release.
Rotation waits for outstanding frame/contact ownership; drawing and touch share
one cardinal basis without mirroring or a second transform.

`CONFIG_LUNASHELL_CODY_START_AUTO_ROTATION=y` explicitly enables the shared policy
after strict saved-setting restoration and persists `auto_rotate` only if off.
This is a requested startup policy, not an absent-key default or corruption repair;
disable it to honor a saved off preference. Cody adds no second IMU acquisition loop.

Debug disables automatic light sleep to preserve native USB Serial/JTAG diagnosis;
Release/wake-on-word follow canonical platform policy. SD diagnostics observe the
storage owner and never alter saved logging preferences. For log capture, rotation
and readback use [offline USB/SD logging](../bsp/services/storage-and-logging.md); those device
operations still require approval. The intermittent mandatory display/touch stop
has not been resolved by short successful captures.
## One-way voice input source increment

The 2026-10-11 firmware source adds an explicit voice-input event for **Sophia**
and an acquired double tap on the visible character. A single character tap waits
420 ms before changing the selected Home lower panel.
Movement, stale/broken touch, rotation and explicit controls disqualify a double
tap. The existing power owner handles standby wake; speech inference and enhanced
mono PCM remain owned by the sole existing ESP-SR AFE.
Wake-word admission skips its detection block and begins with the next real AFE
block. No wake-word cache is prepended; wake-word tail exclusion and preservation
of an immediately following command still need physical observation.

Voice input has presentation priority while retaining the interrupted route and
its real question deadlines. The character, geometry evaluation and normal scene
animation are suspended behind a procedural ribbon with a 30-FPS scheduling
target in a 384 x 58 logical-pixel band at (41,179), retaining its y208 center.
Partial DMA refresh covers even rows [178,238), while artwork clips to [179,237).
Four filled colored layers use fixed-point
interpolation and native RGB565 alpha composition. A display-owned 784-byte knot
buffer avoids per-frame allocation, video assets, SD access and runtime trigonometry.
The visual gain follows valid, fresh microphone levels already published by the
AFE owner; unavailable levels decay the artwork without inventing measurements.
After the first full retirement, only the ribbon transfers at animation cadence;
the identity and timer bands refresh at 5 Hz, and instructions remain static.
Actual frame rate and energy cost require target measurements. One measured,
fading/scrolling row cycles model, catalog cost band, thinking effort, branch,
then project; unavailable fields disappear. No numeric price or bill is invented.
The bottom timer shows elapsed and remaining seconds within the sixty-second
budget, starting at `00s elapsed / 60s left`. Recording keeps all instructions
visible: `Tap once to send`, `Pause over 2s to send`, and
`Shake or double tap` / `to discard`. A single completed tap waits 350 ms for
double-tap arbitration; a second held contact postpones that decision. Double tap
or deliberate shake requests discard. Session and orientation guards reject late
gestures, and only a fully retired view admits input. Cancellation remains possible
while sending before END, but cannot revoke END already transmitted or a turn
accepted by Codex; no late gesture claims that remote audio was discarded.
End of speech requires more than two seconds of successfully acquired 16-kHz
VAD nonspeech PCM after speech; scheduling gaps do not count as silence. There is an eight-second
no-speech deadline and a 60-second maximum utterance. The overall capture/delivery
budget is 90 seconds. New triggers never start a second simultaneous capture.
Explicit sleep and microphone pauses stop this input even when standby retains
the recognition microphone. If END may already have left the device, the result
is `Unconfirmed`, requiring review on the PC rather than claiming cancellation.

The component-owned PSRAM ring is 160,000 PCM16 samples: 320,000 bytes, or ten
seconds of **unacknowledged** 16 kHz mono audio. It is not an utterance-duration
limit. Full buffers fail explicitly without overwriting unheard words. Neither SD
nor internal flash stores audio on the device. Canonical `voice.input` contains
typed phase/trigger, actual counters, original event times and errors; no consumer
reads the microphone. The CCP worker borrows the transport with a 5-ms cadence
only while connected input needs service (at least one RTOS tick). Idle and
disconnected input use the normal 100-ms cadence; the last 100 ms before an ACK
or session deadline return to the fast cadence to preserve timeout granularity.

The dedicated Wi-Fi channel is UDP 42119, separate from CCP metadata. It uses
AES-256-GCM, fresh per-utterance keys derived from the paired link and current
authenticated discovery nonce, sequence numbers and independent data/ACK nonce
domains. A block stays in the ring until its authenticated acknowledgement.
Four DATA packets may be in flight with cumulative acknowledgement of the
contiguous prefix. Retransmission repeats identical ciphertext; START/END and
delivery are distinct. Temporary CCP loss consumes the ten-second ACK budget,
preserving buffered PCM and resuming only with the same authenticated key and IP.
Transport failure after END transmission remains Unconfirmed.
The PC binds START to the exact genuinely followed chat, rejects a changed chat
before submission and never selects a different destination on retry.

Plugin 0.6.5 receives bounded PCM, writes a private WAV under its runtime
`artifacts/voice` and passes `localAudio` to the exact native Codex chat. The
installed CLI's generated schema confirms `localAudio`; account/model support,
desktop endpoint availability, native approval routing and actual delivery remain
unverified. The explicit loopback `CODY_CODEX_AUDIO_ENDPOINT` must identify the
intended desktop/app-server host; the implementation does not launch a second
host or guess a chat. A periodic read-only native probe rejects START early if
the endpoint or followed chat is unavailable; readiness expires after ten seconds.
This does not certify audio/model capability. Steering uses the current turn in
the frozen chat at submission; native conflicts preserve the WAV for inspection.
Recording IDs, returned native turn IDs, artifact paths and uncertainty are
available through audio status; ambiguous input is never automatically retried. Native approvals
and questions are never answered by the device or plugin.

This does not yet establish working device dictation. The 2026-10-12 recording
increment passed the selected Debug wake-word/activity build/size, real owner and
receiver host tests, native raster/gesture checks and individual compiled stack
frames. It was not flashed over the ongoing battery experiment. Native desktop
integration and measured stack/CPU/PSRAM headroom remain in `VOICE-01` in the root TODO. The atlases retain
their planned audio flow until end-to-end verification establishes its behavior.

The 2026-10-14 recording audit found a healthy authenticated CCP link and an
available UDP receiver, but no configured native loopback audio endpoint. Two
real START requests were rejected with "Configure an explicit loopback Codex
audio WebSocket endpoint" before PCM/WAV receipt or native submission. Installed
plugin 0.6.8 matched the receiver source. Separate AES-GCM authentication failures
also occurred; the available logs cannot tie them to those two recordings.
A later passive device capture contained no recording attempt and therefore does
not capture the original failure. Firmware now logs authenticated PC rejection
explicitly. Neither that log nor the taller recording ribbon completes native
audio delivery; a verified endpoint remains required in VOICE-01.
