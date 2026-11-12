# Cody runtime and lifecycle

The device-first firmware uses a single display worker and a separate device
model. Canonical platform owners publish network, power, account and clock data.
The model owns the selected screen, visibility policy, gestures and acquired
projections. The renderer owns prepared glyphs, layouts and the last retired
frame. Partial writes never become the next displayed-state reference.

Cold startup retires the original VibeWare floppy-disk animation. The saved
Counter mode can replace the Muse Home/dashboard presentation with its reset,
context and model pages; an absent preference defaults to enabled in source.
With Counter mode off, Muse wakes and settles
into Idle and then draws one different showcase pose every ten seconds of visible
time. Each shuffled cycle shows Idle, Listening, Thinking, Working, Reply, Error,
Sleep and Waking. The Sleep preview stays dimly visible. Hidden routes and
standby pause the showcase. These poses demonstrate animation and do not represent
real microphone, task, reply, error or power state.
The native CODY2D profile excludes LVGL.

The Cody display worker
handles touch and requests the existing touch sound. PWR hands display ownership to the power service for standby.
An authenticated question or Chat waiting event can wake software standby when
the LAN link remains available. Optional audio follows Sounds and quiet hours.
See [navigation](navigation.md) for current device routes.

The sections below also describe the shared transport, power and audio
contracts and the older character authoring paths in Studio.

## Minimal character direction

The shared character director provides restrained, continuously composed
Thinking/Editing/Running accents,
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

Character observations are bounded to 96 bytes and their display-owned state
to 208 bytes, including turn identity and the reaction clock. Compiled nested
frames and physical stack headroom remain acceptance gates.

Python only transports typed source observations into the native C++ director;
the new host code adds no operating-system observers or companion service.
Current acceptance gates are in [TODO.md](../../TODO.md).

`CODY_character_direction` composes three artistic contributions after the
native response: gaze towards acquired body touch or visible information,
positive warmth after a recognized caress, and concentration/release during genuine
compaction. Rendering and touch keep the existing logical coordinates. Gaze can
follow the last fully retired manga bounds without a second face/icon evaluation.
An approval faces forward. No person tracking or new sensor sampling is introduced.

The display-lock owner retains bounded inputs (at most 96 bytes) and direction
state (at most 208 bytes). Existing native motion, blinking/protective eye closure,
audio-driven mouth opening, breathing, quiet hours and day/night routine
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
unverified.
Studio now binds this same director through its native mood/caress owner. The
ordinary rig supplies fresh acquired desktop pointer targets and recognized
caresses. SIMULATED CCP supplies visible text/pointer targets and coherence-qualified
compaction sequence IDs; changing the source or rotation breaks completion
continuity. Debug, explicit presets, setup and questions other than PC approval
disable the contribution.
The existing Long compaction and Refreshed / pending preview scenarios retain one
rendering-only sequence; opening the latter alone cannot fabricate an observed end.
Python does not duplicate the director's animation equations, and synthetic facts
never enter firmware snapshots. Optional extra gestures stay in
[planned-todo.md](../../planned-todo.md); authorized generation/build/device
work is tracked in [TODO.md](../../TODO.md).

## Startup and expression sequencing

Cold boot or software reset displays the first VibeWare frame while services
initialize. The Ready checkpoint starts the floppy insertion again from frame
zero, after startup DMA parking ends. Each of the 42 original frames retires
before its 80/90-ms dwell begins. The full 3.5 seconds of authored dwell precedes
the first interactive route. The unchanged GIF and shared flash RLE package live
in `cody-device/assets/branding/VibeWare/`. Deep-sleep and ordinary standby wake omit the
introduction.

The first retired frame of the active cycle requests BootRise through the shared
audio worker. A short confirmation also accompanies opening Options with BOOT
and starting a timer. These cues use the saved Sounds setting, the current
volume and quiet hours. Audio failure leaves the visible boot sequence usable.

During saved Wi-Fi startup, four highlight steps animate the Wi-Fi arcs in a
72 x 72 logical area above the connection heading. Once the route is steady,
each step invalidates only that area. The actual offline route keeps its static
icon and setup actions.

A new touch contact requests a short click. A confirmed local Wi-Fi lease or
settled connection failure requests its own rising or falling cue. These
observations are independent of the Codex link. Repeated retries, held touches
and pending DHCP do not repeat audio. The shared sound policy also protects
voice capture and timer alarm playback.

The device-first firmware enters **Not connected** on screen 04 when a saved
Wi-Fi profile is absent. **Tap to pair** opens **Wi-Fi setup**. **App download
QR** opens screen 06. A tap returns to the guide, where **Show pairing QR**
admits the Bluetooth session. The retained-profile startup choices come from the canonical
network and link state. See [navigation](navigation.md).
Each actual saved Wi-Fi attempt can show `Wi-Fi connecting` for up to 15 seconds.
After a failed attempt the station schedules the next try for about one minute
later. Between tries, the offline screen shows the saved network under
`Previous Wi-Fi` and offers `Tap to pair`.
Valid association and IP advance immediately; the interval adds no polling task
or forced minimum delay and does not infer Internet or authenticated PC presence.
Only explicit development force-first-boot replays 04 regardless of that state.
Returning devices with validated first-boot false receive one four-second
random Affection or Joy request on entering Idle, using the existing icon and
completed-frame SFX owner. Urgent symbols and real interactions retain priority;
hidden requests are dropped. No pairing or credential keys are rewritten by this
presentation policy.

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
the shared 800-ms panel fade, then gives Waking 350 ms and Ready 250 ms. The authored
software-wake sequence totals 1.4 seconds instead of 4.3 seconds; hardware restoration,
frame scheduling and reconnection are additional, unmeasured costs. It does
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
user timeout, a positive value replaces it only for the active scene. The separate
confirmed-PC-absence policy below replaces ordinary inactivity during its grace interval.
Invitation 05 and installation 06 use a 60-second standby override. In the
device-first interface, pairing 07 uses a 330-second standby override to cover
the pairing service's five-minute deadline and radio startup and cleanup.
The unused scan definition, Online 09, Debug-only 39, 10, 36 and 13-15 use 60 seconds.
Connect Codex 12 uses 180 seconds; successful canonical code delivery refreshes
its anchor once, so preparation time does not consume the code-reading interval.
Hello 04, error 08
and look 11 inherit. Scene entry supplies a fresh
inactivity baseline and touch refreshes it. Departure or parking clears the
override. The pairing service owns the active Bluetooth deadline and reports
its result through the canonical session snapshot.

Standby retains the active route. In particular, wake from screen 09 returns to
09; the wake contact does not invoke its Continue action. Screen 39 is available
only in Debug builds and one awake tap dismisses it. Release contains no generated
scene definition or expression-catalog mapping for 39.

### Standby without a connected PC

Cody waits 120 seconds after a valid canonical observation first reports no
authenticated CCP PC connection. Real touch/button activity and completed wake
grant a fresh interval; background motion and repeated discovery do not extend
it. Connected transport clears the timer even when no chat is active or Codex
reports Closed. Unknown/stale link evidence cannot establish PC absence. Explicit
network pairing, pending Codex pairing and confirmation defer this timer.

This policy applies without confirmed USB power, independently of the ordinary
battery-only inactivity gate. While awake, Cody presents a larger character above a centered
ASCII `Bye bye`, waits for completed Sleep/caption frames, plays the existing
220-ms Sleep cue through the audio owner, and keeps a minimum 400-ms visible
dwell before the shared 800-ms whole-panel fade. Sounds Off, authored sound disable
and quiet/time policy suppress the cue explicitly; real playback/preparation
failures abort standby and retain their original errors. A link restored or real
input acquired during the farewell cancels the transition before radio shutdown.

After the transaction, radios and voice input are suspended. The wake-word build
keeps Sophia listening in this software standby; the feature-off build suspends
voice acquisition. Saved credentials/pairing are retained. Movement and queued
network notices cannot wake offline standby. Touch, a physical button, an expired
local timer or a fresh Sophia detection when enabled restores the ordinary owners
and reconnects. The wake-only build does not start voice recording.
If an existing dark standby loses its PC, the same timeout sheds its retained
services without waking the panel for a farewell. No periodic reconnect wake is armed.

The power owner reads link and setup state through a canonical projection
bounded to 64 bytes. Offline standby still requires verification of timeout,
rollback, sound, touch and wake behavior. Whole-board current, stack headroom
and wake latency require device measurements.

The local timer uses a monotonic deadline, including elapsed software standby.
The display worker retains the timer model and can request an alarm wake even
while panel writes are parked. Playback uses the shared audio owner and honors
Sounds, volume and quiet hours. The timer remains active across navigation and
ends on restart or shutdown.

The CCP owner retains the latest authenticated allowance, dated usage and
credit observations in RAM and checkpoints them through the settings executor.
The first write is coalesced for two seconds and subsequent writes are spaced
by at least ten minutes. Observations use the device clock and the provider's
reported age to retain the original UTC time. Saved data is scoped to the
retained pairing and removed by Forget, replacement or factory reset.

### Pending-question sound cadence

The device-first pending-question alert plays one soft 1.8-second Question bell
with a long fading tail. While the canonical question remains fresh, Pending
and online, one quieter 300-ms QuestionReminder is eligible every sixty awake
seconds. The effects model owns this cadence independently of the selected
Info, statistics or Options page. Sending, terminal outcomes or unavailable,
stale or offline evidence stop future attempts. Each attempt uses the shared
four-voice audio owner and existing Sounds, volume, quiet-hours and standby
gates. Cues rejected as busy or suppressed do not accumulate for later playback.
Both samples derive from the purchased Soft Chime recording at -24/-27 dBFS.
The shared owner streams their flash PCM in bounded chunks;
single Codex notices admit up to two seconds, while activity/personality and
repeated clips retain their 350-ms bounds. The shared audio worker plays the
existing immutable PCM; UI redraws do not request or restart sounds.

### Device diagnostics

Both customer and development builds expose the same authenticated CCP device
diagnostics. The `character` selector opens the Cody Device diagnostics card,
which shows measured CPU utilization and free heap from the canonical snapshot.
Unavailable or stale observations appear as unavailable.
`normal` restores the retained route. Build mode is not a required user choice.
The owner removed the facial-channel list, its PSRAM buffer and scroll handling
to save device space. Internal channels still drive character animation.

Tap **Memory** to open heap details for internal RAM and PSRAM. Each section
shows used/free capacity, the largest allocatable free block, the SDK-reported
minimum free amount and the free-block count. Blue means used capacity, green
the largest free block and amber all other free capacity. These are capacity
bars, not an address map or an ordered allocation trace. The fragmentation index
is `100 * (free - largest) / free`, rounded to the nearest whole percent;
it is `N/A` when no free space remains. Separate allocator regions also contribute
to this index, so it is an indication of noncontiguous free capacity rather than
a count of damaged or unusable bytes. See the official
[ESP-IDF heap information guidance](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-reference/system/heap_debug.html).
The page uses the already-published canonical measurements, with a display-owned
48-byte projection; it adds no allocation tracing, heap walk or acquisition task.
Unavailable/stale heaps show labels and gray bars instead of numeric readings.
The Memory action appears fully drawn, without typewriter timing, and touch is
admitted only after the corresponding frame completes. Back first returns to
the diagnostic overview, then to the retained normal route. The same native
layout is used by Studio, visibly SIMULATED with device heap readings unavailable.

The existing signed DEBUGCAP/DEBUG/DEBUGACK exchange, live paired endpoint,
fresh nonce, request deadline and application acknowledgement remain required.
The display worker owns all changes; consumers read the canonical snapshot
without acquiring sensors. The reserved wire scroll field must remain zero.
Release renders the readout and refreshes a paused view while an explicit overlay
is active. Back restores normal presentation and its ordinary power behavior.
An applied ACK proves the rendering worker accepted the view, not visible pixels.

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
| Stop | Chat waiting, then idle commands and retained statistics | A completed response does not prove task success |
| PermissionRequest | Waiting for you / Approve on your PC | Native approval remains on the PC |
| Transport expires | Connecting while retained discovery is active | Preserve the canonical timeout error; do not request a new pairing |

Lifecycle diagnostics retain event names, phase, compaction sequence and hashed
chat/turn identities. They exclude prompt text and raw identities. Host fixtures
exercise the real activity owner, authenticated packets, Studio projection and
native panel pixels; these synthetic captures do not prove physical sound or
that a particular live compaction hook was delivered.

### Semantic questions and other-chat replies

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
mint **Chat waiting** headline, the chat title, subordinate project and an **OK** button with a check mark.
Generic notices also omit identical title/body repetition. No GUID, transcript,
task-success inference or polling is introduced. A prompt retires native notices
only for that chat; dismissal still requires the authenticated owner acknowledgement.

Native permissions and `request_user_input` prompts already open in Desktop
stay on the PC. Those requests belong to the connection that created them.
Cody accepts replies through its own authenticated question flow. See the
[App Server protocol](https://learn.chatgpt.com/docs/app-server).

An asynchronous tool can return `accepted` before the user answers. Question
completion requires the acknowledged answer for the originating request.
Parent and subagent turns retain their distinct identities.

### Companion information panels

Home retains the character, actual activity title and independently aged model
row above four lower panels. Tokens shows a Free token hero, occupied-context
progress arc and Last/Window. Model & Context shows
qualitative catalog price and reasoning effort; the shared header carries the
model name once. Device shows connection state, the device's own canonical
Wi-Fi IP and device identity, independently valid IMU activity, compilation date
and time in the compiling PC's local timezone, that actual timezone identity and
the compiling computer's name. Chat shows the actual native title, the measured
elapsed time of the current turn and its authenticated activity description.
It does not infer a whole-chat lifetime or inspect raw commands/transcripts.
Long titles, activity and build labels scroll within clipped native rows, and
rotation allows enough visible time to read them.

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
[the model-cost asset](../../cody-device/assets/cody/audio/model-cost/README.md).

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

A shared native carousel cycles through Tokens, Model & Context, Device and
Chat & Activity after at least eight visible seconds per panel. Chat & Activity
shows the canonical chat title. Long titles
scroll within (73,276,320,28), with a 1.2-second start hold, 40 pixels per second
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
second server. Instead, the existing broker verifies the exact genuine hook-selected
session and turn against local native journal metadata, using the original
locator or a read-only state-index lookup. The working directory is mutable
metadata, not session identity. Only allowlisted session, turn-context
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

### USB power context

Cody has no manually selected usage mode. Its power owner observes USB/VBUS
through the canonical battery snapshot; confirmed external USB power is treated
as desk power, including when the battery is full. Missing, stale or failed
observations do not establish either presence or absence. USB power does not
change pairing, voice settings or the underlying motion sensors.

### Counter mode

Settings > Behavior > More behavior > More... > Counter mode selects the
counter presentation. The setting defaults to On only when its persisted key
is absent. Horizontal swipes cycle through reset, context and model pages;
the model price band is an indicative standard reference. Fields without a
current, coherent source are unavailable. The counter does not show chat
titles, messages, event details or the mascot. BOOT and the settings route
remain available so the mode can be switched off. Studio labels its independent
loopback preview SIMULATED and does not change a physical device.

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

When Synth Voice is enabled in a TTS-enabled Cody build, the speech owner can
read a current authenticated question and its choice labels and details. After
delivery it can read the selected answer only when the corresponding actual
choice is still present in the canonical event. The request is tied to the
event identity and revision, and is cancelled if the event, connection, Sounds
or Synth Voice preference becomes invalid. Busy speech is retried only while
the same event remains current. Synth Voice is initially off.

With Synth Voice enabled, a current ReplyReady event can also carry encrypted,
authenticated segments of the actual assistant response. Cody acknowledges a
segment only after its speech reports completion; the desktop retains the next
segment until that acknowledgement. The ordinary chat title is never spoken as
the answer. Disabled, invalid, oversized, stale or unavailable content is not
replaced by an inferred response. Playback and transport still require physical
verification.

The shared audio owner mixes up to four voices from mono PCM16 or generated
waveforms. Effects, boot and system PCM, feedback and synthesized speech share
the output; a speech stream reserves one voice. PCM sources retain their own
sample rates, request identities and completion results, and remain borrowed
until terminal acknowledgement. The owner applies the relevant Sounds,
quiet-hours, volume and standby policies. A full set of voices returns busy.
Summed samples saturate at PCM16 limits, so overlapping loud voices can clip.
Hardware mixing, latency, memory headroom and playback quality remain unverified.

### Device-first notifications

The device runtime prepares the shared effects output explicitly. Its
`DeviceEffectsModel` consumes canonical observations and owns deduplication,
connection continuity and reminder deadlines. The renderer neither selects
effects nor initializes the playback worker. The existing Sounds preference,
volume, quiet hours and power admission remain authoritative.

| Observed event | Existing effect |
| --- | --- |
| New pending question | Question; quieter QuestionReminder after sixty awake seconds |
| Authenticated ReplyReady | ResponseReady |
| Selected answer confirmed Delivered | AnswerReceived |
| Approval required on the PC | ApprovalRequired |
| Completed checks passed or failed | ChecksPassed or ChecksFailed |
| Codex connection lost for three continuous seconds | ConnectionLost, once |
| Confirmed recovery after the observed loss | ConnectionRestored |
| Observed Wi-Fi provisioning attempt succeeds and Wi-Fi is online | ConnectionRestored |
| Observed provisioning attempt fails | Interrupted |
| Battery 50%, 20%, 10% | QuestionReminder, Paused, ConnectionLost respectively |
| Battery 5% or measured empty | Two or three ConnectionLost clips with short gaps |
| Dictation enters its start countdown | Clear ResponseReady chime before microphone capture |
| Dictation sends its complete END datagram | Short Running pulse |
| Dictation cannot confirm the destination | Two short Interrupted pulses while the result is visible |
| Dictation delivery remains unconfirmed | Descending ChecksFailed chime while the result is visible |
| Dictation closes after another result | SessionEnded on confirmed queue admission, Interrupted otherwise |

The first connection observation establishes a silent baseline. Generic notices,
ongoing work, running checks, navigation and statistic refreshes are silent.
The bounded recent-event ledger prevents repeated frames or brief reconnection
from replaying the same event. One priority-selected request is admitted per
update. A new question, a ready reply or confirmed delivery can wait up to
2.5 seconds for the shared speaker, as long as the same event is current.
Muted, expired and failed requests are consumed. Other busy effects are
consumed as well. A blocked dictation destination or unconfirmed delivery can
retry audio admission while its terminal screen is visible. Recording and
delivery suppress competing effects so they cannot be captured by the
microphone. The matching delivery acknowledgement remains authoritative.

Studio's device-first preview shows the corresponding routes but does not
reimplement this controller in Python or provide speaker validation. Hardware
sound and standby/wake behavior require their own device verification.

### Character authoring audio

Boot cues start with original procedural `audio-ready` (0.18 s), followed by the
explicit `audio.eyes_open_cue: boot-rise` in `cody-device/assets/cody/runtime.yaml`. BootRise
is a 1.8-second rising chime: 79,380 mono PCM16 bytes at 22,050 Hz.
Source/rendered hash mismatch or stale authoring fails preparation rather than selecting another sound.
See [boot and Codex action authoring](../../cody-device/assets/cody/audio/codex-actions/README.md)
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
optional voice-AFE feature has its separate matched-rate configuration. The
mixer converts source PCM to the shared transport rate and does not change an
open microphone clock to match a clip.

`CODY_codex_audio.hpp` selects typed Codex action cues from immutable mono
16 kHz PCM. The current cue set, durations and byte totals are owned by the
[authoring manifest](../../cody-device/assets/cody/audio/codex-actions/README.md), within
the existing audio-owner limit. A single audio worker mixes up to four active
cue voices and owns the speaker transport without an event replay queue. Independent
channel generations cancel hidden Codex presentation and hidden personality
symbols without cancelling each other; standby invalidates both. Codex starts
suspended and its presenter enables only visible companion transitions.
Working/Reading/Editing/Running requests have a 1.5-second activity rate limit.
Other notices skip that limit and can overlap when an audio voice is free.
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
trusted time suppresses keyboard microclicks when quiet hours are enabled;
disabling quiet hours permits cues without that clock, independently of routines. Unavailable policy
settings still suppress with their real error. Audio/speaker-excluded profiles
skip preparation while preserving visual startup. Runtime playback failure is
logged/published without stopping this optional presentation effect.

The mixer uses bounded voice and transport buffers. Its linked-image RAM,
worker stack headroom, speaker loudness and tone require measurement on the device.
Candidate cues for separate audition are documented in the
[audio asset guide](../../cody-device/assets/cody/audio/gentle-sfx/README.md).

## Windows preview

The [simulator guide](../../cody-device/simulator/windows/README.md#host-audio-and-inspection-ownership)
owns the host audio DLL, PC playback, inspection typography and browser links.
These are synthetic host capabilities with separate execution evidence.

The Home tool caption is a presentation of the genuine `PreToolUse` identity.
The PC-owned table in `ccp_tool_labels.py` maps known native/MCP names to short
English actions, such as `Tool: GitHub: read file`. Unknown names retain every
server/method word with underscores rendered as spaces. Two tools may share a
caption while retaining distinct correlation IDs. Wrapper arguments are never
parsed to invent an inner tool or activity. No firmware protocol change is needed.

## Device diagnostics

Debug FPS uses completed frames over real elapsed time. CPU1/core0 and CPU2/core1
come from canonical idle/IPC measurements with their own timestamps; invalid or
stale data shows unavailable, never an estimated/clamped value. Measurement
windows begin after startup at 04 or in the ordinary main companion view when
retained pairing skips that first-run greeting. The automatic Idle clock greeting
and a manually selected sleeping face in the main view remain eligible. The
compact rows below the routine title show FPS/CPU, IPv4 and free internal
RAM/PSRAM. SD-capable boards add card presence/access and free-space rows;
the 1.75C omits them. The upper overlay uses a quieter
date, larger `HH:MM` without seconds and consistent battery, Wi-Fi and Bluetooth
icons. On the GPS prototype, which has no card-detect pin, the SD row distinguishes
a verified card response from `SD UNCONFIRMED`. Personality icons keep their
independent anchor above the cloud. The CPU values
represent total busy time per core, including tasks other than Cody. No extra
sensor polling is introduced. The HUD preference is session-scoped under the
display lock and gated by scene `readout_visible`. Authored scenes admit the HUD
unless a visible bottom action occupies its area; Release excludes it.

Power is dedicated to standby/wake and deliberate shutdown in Debug and Release.
A short click toggles software standby; a valid three-second continuous hold
presents `Good night` with `Release Power` directly below it, retires the complete
Sleep presentation, and requests the
power owner's gradual fade and coordinated PMU shutdown. A short BOOT press
requests Home and a two-second hold requests Options. The Power-screenshot setter returns `ESP_ERR_NOT_SUPPORTED` and
its getter reports disabled.
SD-backed screenshot diagnostics apply only to removable-storage profiles and
retain the existing storage owner and real failures. They do not establish an
internal-flash screenshot destination on 1.75C.

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

`CONFIG_LUNASHELL_CODY_START_AUTO_ROTATION=y` defaults `auto_rotate` to on when
the key is absent. Cody restores existing values as saved. Turning auto-rotation
off holds the current panel orientation until an explicit manual change.

Debug disables automatic light sleep to preserve native USB Serial/JTAG diagnosis;
Release/wake-on-word follow canonical platform policy. Cody on 1.75C uses native
USB for live firmware logs; its 32 MB flash does not imply a retained log file.
On SD-capable boards, diagnostics observe the storage owner and never alter
saved logging preferences. See [USB logging and optional removable-card readback](../bsp/services/storage-and-logging.md)
for capture and, where supported, log rotation/readback. Those device operations
still require approval. The intermittent mandatory display/touch stop
has not been resolved by short successful captures.
## One-way voice input source increment

The current `wake-word` build selects Sophia for standby wake only and excludes
the separate Codex voice-input owner. The following describes that owner's
behavior when it is explicitly enabled in a future build configuration.

**Options → Behavior → Dictation** controls whether Cody admits a new voice
recording. The device stores the choice. The default is On for a voice-enabled
Cody build. Off prevents voice input from opening after Sophia, a character
double tap or a physical standby wake. Wake behavior has its own controls.

When dictation is enabled, the firmware accepts an explicit voice-input event
for **Sophia** and an acquired double tap on the visible character. A single
character tap waits 420 ms before changing the selected Home lower panel.
Movement, stale/broken touch, rotation and explicit controls disqualify a double
tap. Before publishing Preparing for a double tap, the voice owner requires
current Wi-Fi with an IP address and a fresh authenticated CCP READY from the
paired PC. The audio path uses that local network connection.
The existing power owner handles standby wake; speech inference and enhanced
mono PCM remain owned by the sole existing ESP-SR AFE.
Preparing is admitted for wake-word, character double tap,
and a completed physical standby wake from a short power press, touch or motion.
Internal recovery, power-off farewell and Codex-notification wake do not start
recording. A wake-word event already admitted before wake is not duplicated.
No wake-word block or audio from preparation is recorded. Preparing waits for
fresh authenticated chat metadata. It advances the existing discovery poll once
when the previous exchange is complete. Cody freezes the destination title and chat
identity together and shows the title throughout the recording and transfer.
Long titles display their beginning and end. A fresh unsupported title fails
immediately. A complete Preparing frame showing the destination admits the
three-second Recording starts in countdown. Cody sends START during that countdown.
If the countdown ends without START acceptance or a current link, Cody shows
Waiting for PC and keeps trying within the original ten-second ACK budget.
PCM requires the countdown deadline, authenticated START acceptance for that
destination, a current link and a fresh successful AFE Listening observation.
No wake-word cache is prepended. Physical timing still needs observation.

Voice input has presentation priority while retaining the interrupted route and
its real question deadlines. The native device UI shows the owner-published
3, 2, 1 sequence as a centered number with a red Cancel button. During preparation, listening and
sending, a four-color ribbon occupies a 384 x 58 logical-pixel band at (41,179).
While listening, gain follows valid, fresh microphone levels. Unavailable samples
decay the visual response. Actual frame rate and energy cost require target measurements.
On the native device screen, Cancel is available during Preparing, Waiting for PC
and the countdowns. Listening offers Send and Cancel. Session and orientation guards
reject late gestures, and only a fully retired view admits input. Cancellation remains possible
while sending before END, but cannot revoke END already transmitted or a turn
accepted by Codex; no late gesture claims that remote audio was discarded.
Five seconds of successfully acquired 16-kHz VAD nonspeech PCM finish capture,
including initial silence. A straddling block admits only the exact five-second
quiet prefix; scheduling gaps and failed reads do not count. Capture then enters
Sending in for three actual seconds before END may leave. Buffered PCM
can continue its authenticated transfer, but no native submission is committed
during that countdown.
The maximum utterance is
60 seconds from actual capture admission, with a 90-second overall session budget.
Preparation that cannot retire a complete frame fails after 15 seconds. New
triggers never start a second simultaneous capture.
Explicit sleep and microphone pauses stop this input even when standby retains
the recognition microphone. If END may already have left the device, the result
is `Unconfirmed`, requiring review on the PC rather than claiming cancellation.

The component-owned PSRAM ring is 160,000 PCM16 samples: 320,000 bytes, or ten
seconds of **unacknowledged** 16 kHz mono audio. It is not an utterance-duration
limit. Full buffers fail explicitly without overwriting unheard words. Neither SD
nor internal flash stores audio on the device. Canonical `voice.input` contains
typed phase/trigger, actual counters, original request/capture times, canonical
countdown seconds, displayed destination, END transmission, failure category
when known and original error. Its bounded projection is at most 64 bytes. No consumer
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
delivery are distinct. Repeated temporary CCP losses preserve the same session
and buffered PCM. Each authenticated ACK renews the ten-second allowance, while
the total session deadline still applies. Recovery requires the same credential,
IP and audio endpoint, with the receiver's in-memory recording still present.
After END, retries can recover the receipt without another Codex submission.
Transport failure after END transmission remains Unconfirmed.
The PC checks the displayed chat's digest against the exact chat selected by the
latest genuine Codex prompt. A changed or missing selection fails admission.
Opening a different chat in Desktop alone does not update this selection. A
change during dictation also blocks queue admission. Both Cody and the plugin
need the matching START contract.

The plugin receives bounded PCM, writes a private WAV under its runtime
`artifacts/voice` and submits `localAudio` to the public queue of the exact
followed Codex chat. Setup selects the installed Codex executable explicitly.
The receiver re-reads the selection at each readiness check; a changed executable
must pass its own queue probe before a new START can be admitted. A new followed
chat is probed on the delivery worker's next tick. An existing
recording never moves to a replacement executable. The readiness lease lasts ten
seconds and does not certify audio/model capability. The native device UI shows
`PC rejected audio` for an authenticated rejection; the PC status retains the cause.
Recording IDs, returned native turn IDs, artifact paths and uncertainty are
available through audio status; ambiguous input is never automatically retried. Native approvals
and questions are never answered by the device or plugin.

Device dictation remains unverified end to end. Native desktop integration and
measured stack/CPU/PSRAM headroom remain in `VOICE-01` in [planned-todo.md](../../planned-todo.md).
Physical voice trials remain deferred.

The optional installation step, `scripts/device.ps1 codex-audio`,
checks an explicitly supplied loopback listener against the running Windows
Store Desktop app-server process, then initializes a client and reads the genuine
followed chat without resuming or submitting input. Only a live loaded chat with
stable followed identity admits the atomic `audio-config.json` save under the
existing per-user CCP storage owner. The capability remains in its existing file.
The audio owner loads this configuration at startup; invalid storage is an explicit
optional failure, and an intentional endpoint environment override is separately
identified. Dependency installation is opt-in. Restart Codex after a successful
save; read-probe readiness is distinct from genuine dictation/model acceptance.
See the [installation instructions](../../cody-desktop/plugins/cody-codex-plugin/README.md#install-and-review-hooks).

If the running Desktop app-server exposes only stdio, the installer reports
that it cannot attach. Dictation requires a reachable supported listener on
the process that owns the followed chat.

## Clock presentation

Trusted time and persistence belong to the [shared time service](../bsp/services/time.md).
Cody reads its canonical clock projection, including health, original timestamp,
timezone, period and quiet state. It never reads RTC, BSP or the system clock directly.

### Presentation and audio policy

After non-onboarding boot or genuine wake, trusted local time selects Good morning!,
Good afternoon!, Good evening! or Hello, night owl!; unavailable/stale time uses
Hello!. Period changes can retitle the awake scene without replaying entrance,
missed greetings or creating a wake schedule. Morning selects Happy and night
Drowsy, otherwise Calm. Explicit expression, boot/lifecycle and onboarding take
priority. Quiet evening/night softens contour/breath behavior with continuous
phase; it does not silently change touch/IMU gains, brightness, radio power or
saved volume.

The cue policy is allowed without a trusted clock when quiet hours are disabled,
independently of the routines preference. When quiet hours are enabled,
unavailable/stale time suppresses cues; a valid quiet interval also suppresses
them. Unavailable settings remain a real
error with conservative suppression. The original microclick also obeys its zero
volume/standby/hidden-scene gates. Both cue types share clock policy; see
[runtime](runtime.md). It is not a claim of safe audible loudness on the speaker.

### Future reminders

Routines choose presentation; they do not create an alarm or wake scheduler.
Reminder, snooze and scheduled-wake ideas remain in [planned-todo.md](../../planned-todo.md).
Outstanding observations are in [TODO.md](../../TODO.md).
