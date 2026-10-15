# Cody layout on the Waveshare round display

Native `CODY2D` layout consumes the board's
[geometry](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md).
The repository contract is in [AGENTS.md](../../AGENTS.md). These coordinates
describe the current 466 x 466 adapter; they are not requirements of every future board.

## Content, actions and Back

Use the central [73,393) square as a baseline. Expand into the circle only with
local-chord admission of artwork, controls and complete hit bounds. Keep QR
quiet zones intact. Scene content and lower actions have separate rectangles.

Back occupies (10,203), 60 x 60, with one centered left chevron and no caption.
Show it with a valid previous route. Retain the complete circular touch target
and keep it separate from central content and primary/footer actions.
The native style is subdued graphite with a light chevron.

The 2026-10-14 action revision retains semantic colors and restrained gradients
with text-only captions on every Cody button. Setup actions keep their rounded
side-by-side cards or centered single capsule; captions are centered in the
complete control. Back retains its separate chevron. Complete controls appear after the title and main body line, with
touch admitted at the same time. Captions have no typewriter timing or character audio.

| Control | Logical rectangle |
| --- | --- |
| Two cards | (73,260,154,104) and (239,260,154,104), 28-pixel corners |
| Third action / Options | (153,376,160,52) |
| Ordinary single action | (113,352,240,72) |
| Compact QR/skin footer | (123,390,220,44) |
| Numbered reply / pending-question rows | (73,256,320,60) and (73,328,320,60) |
| Question-list hint | (99,394,268,25), separate from the wide content slot |

The corresponding extreme squared distances are 42,500, 44,036 and 52,100,
within radius squared 54,289. These are static calculations, not touch evidence.
Coverage is clipped to the rounded control; complete hit bounds remain admitted.

Reply rows contain one numbered line, with at most two visible rows. A vertical
swipe opens the page containing the third reply. Long row captions show an
explicit ellipsis; the tap opens their complete canonical label and description,
without sending anything. Review uses five 26-pixel-spaced lines at y=196..300.
Swipe moves backward/forward; a tap in that text viewport advances a page and
returns to the first page after the last. The separate `Send reply` capsule at
(113,352,240,72) is the only initial reply submission action. Scrolling never
replays text audio. Orientation, source changes and hidden controls cancel
uncompleted gestures.

The pending-question view shows up to two canonical question titles. Selecting
another request asks the event owner for that source and shows `Opening question...`
until its actual event arrives. The retained question can reopen immediately;
neither selection nor Back answers or dismisses a request. Opaque request IDs
remain internal. Queue counts and entries come from the authenticated owner.

`Reply on its way` retains the question and transport status above a single
read-only `Confirming delivery` line at y=320. While online and without a reported
delivery error, that line breathes in electric blue on a 1.6-second cycle. The
firmware animation uses pausable scene time and 50 ms color steps; it changes
neither the recipient acknowledgement requirement nor the request deadline.
Offline/error states retain a steady muted indicator, with any actual error at
y=354. The indicator has no touch target, typewriter cadence or character audio.
Firmware and Studio use the same native color policy. This source revision has
not yet been built, simulated or checked on the physical display.

A typed `ReplyReady` notification uses the compact character at (233,92), scale
42, and one mint `Reply ready` headline at y=150. The originating chat's complete
bounded title occupies up to three body rows at y=214/242/270. When its owner has
no admitted title, the body says `Open the other chat in Codex`; it never repeats
the headline or borrows the currently followed chat's identity. Project context
is subordinate at y=312, with explicit unavailable/offline/error text when needed.
The single iconless `Got it` action retains (113,352,240,72) and requests the
existing authenticated dismissal. Controls appear only after the full narrative;
reply readiness implies neither task success nor passed checks. Generic notices
also omit body text when it is identical to their title. Terminal request outcomes
retain their own expired, cancelled or rejected captions.

## Setup, QR and Skin

| Screen | Artwork/content |
| --- | --- |
| 05 | Cody moves from (233,225,130) to (233,105,65) over 1000 ms; wait 300 ms before text and action admission |
| 06 | White background, Cody (233,94,58), installation QR 240 x 240 at (113,140) |
| 07 | Cody (233,80,46), title y=131, session QR 204 x 204 at (131,182), only while ReadyToScan |
| 10 / Make it yours | 34-pixel option rows at y=202/240/278/316; separate compact footer at y=390 |
| 11 / Skin | Preview (233,172,44), title y=78, viewport (117,226,232,146) |

Skin rows are 220 x 38 with 44-pixel steps. Only complete rows are drawn and
interactive; scrolling admits three visible rows. Stable look IDs and save gates
are described in [navigation](navigation.md).

Session QR storage supports version 10: at most 57 x 57 modules, 407 bytes.
Use integer modules and four white quiet modules. Payload/modules belong to the
current attempt; stale data cannot authorize presentation.
The current 05/06/07 QR journey uses compact footer actions and suppresses lateral
chevrons; 06 retains previous-route swipe. Its previous-route Back behavior still
needs reconciliation with the repository's lateral-Back contract in [TODO.md](../../TODO.md).

## Companion panels, status and Debug

Home uses four lower panels: Tokens, Model & Context, Device, and Chat & Activity.
Their typed identities are Home (0), ModelContext (2), SessionLink (3), and
ChatActivity (4); the retired Activity identity (1) remains unavailable.
Panels rotate after at least eight visible seconds. Device and Chat extend their
dwell when needed to show complete metadata or the chat title, including readable
start/end holds. A short released tap advances
immediately and restarts that interval; a deliberate canonical-IMU shake returns
to the default token panel. The character remains at (233,122), scale 73, with
the state title at y=190 and the model row in (73,236), 320 x 28. Only the lower
panel changes; there is no full-screen Activity page, page counter or Back control.
Setup and questions retain their existing routes and action controls. Hidden
views, standby and current compaction pause automatic panel rotation.

The selected [context-arc proposal](ui-proposals/20261014/02-context-arc.png)
is implemented with native text and antialiased strokes. The dashboard damage
envelope is (40,268)-(426,393); actual ink follows the local circular chords,
not the entire enclosing rectangle. Tokens show a large Free value, secondary
Last and Window values; timer and IMU do not appear on this panel. The arc length
represents consumed context, growing from an empty track at zero usage to a full
arc at 100%; the Free value still reports remaining tokens. Both colors reflect actual
occupancy: white below 25%, cyan from 25% to below 65%, orange from 65% to below
85%, and red from 85% onward. Full context shows a complete red arc and a red zero
Free value. Active compaction retains its orange indeterminate
accent regardless of occupancy. Missing, stale, invalid and over-limit data
retain explicit states and never produce an invented percentage.

The Device panel's IMU label prefers `Still` only when the acquisition owner's independent
stillness result is valid, successful and no older than 200 ms at its original
measurement timestamp. It remains available when the separate HAR classifier
rejects a cadence gap or reports a model error; those errors remain in the
canonical snapshot. Stale, future, invalid or moving observations remove `Still`
and retain the explicit HAR result or failure. `Still` means measured absence of
motion; it does not identify Sitting, Standing or another posture.

Model & Context contains the qualitative Standard catalog price capsule and
thinking effort; the model name appears once in the shared header. Device contains
the PC connection state at y=268, the device's own canonical Wi-Fi IP at y=288,
and device ID with IMU status at y=308. Compilation date/time occupies y=335;
the compiling computer's actual timezone and hostname scroll in separate
320 x 18 viewports at (73,355) and (73,375). These values come from immutable
captured build metadata, with explicit unavailable, unsupported or error states.
They are not the device clock, the connected PC's live settings, or invented UTC.
Long ID/status combinations use a clipped 320 x 25 row; ordinary states fit
without movement. No firmware version or PC IP is displayed.

Chat & Activity contains the actual title without a Chat prefix or GUID in
(73,276), 320 x 28, followed by the reported current-turn elapsed time at y=316.
The genuine current activity description scrolls in (73,354), 320 x 28; absent
descriptions use the real phase or an explicit unavailable state. No command or
chat age is inferred. Repeated tool descriptions may restart their own text
window, but their reading deadline remains relative to panel entry, so working
activity cannot indefinitely prevent rotation. Unchanged source refreshes do
not restart title or immutable metadata scrolling.

Independent source validity remains visible. Model prices are green Cheap,
orange Reasonable price and red Expensive; they are not monetary chat spending.
The first complete presentation of an Expensive price after a model change
requests one five-plink notice. Rotation and repeated taps never replay it.

Home uses the `TT` unit and decimal whole thousands with `K`, truncating the last
three digits: `Free 319K TT`. Periods group every three displayed digits; counts
below 1000 remain exact, including zero. Canonical and protocol counters retain
their full precision. Excessively wide secondary captions alternate within their
bounded row; the source values and timestamps do not change.

The footer says `Use @CCP in Codex and ask for the available commands` in one
clipped right-to-left marquee at (113,408), 240 x 25, without a heading. It begins
on the first healthy connected, visible Home presentation and makes exactly three
complete passes, each entering from the right and leaving fully on the left.
The existing active scene clock moves it two pixels every 50 ms. It then stays
hidden for the remainder of the display-owner boot/session; no personalization
hint replaces it. Chat changes, panel rotation, tool updates and reconnection do
not restart the message. Standby, source gaps, setup, questions, attention and
compaction pause the retained position and pass count. Final removal damages the
entire footer viewport so old glyphs are erased. Current compaction turns the
title orange and the token arc
into an indeterminate accent, preserving the character cue and sound admission.
The independent fonts derive from the vendored OFL Telex face; other scenes keep
their existing typography. No bitmap/video playback or polling task is added.

The standalone status layer shows date at (122,32), local HH:MM at (122,47),
and battery/Wi-Fi/Bluetooth at y=43-64. Seconds are omitted. Battery is gray
when absent, red when low, orange midrange, green healthy, and cyan/white charging.
Flash row masks and canonical projections require no LVGL or hardware reads.
Touch reveals the layer; five seconds without activity begins the 330-ms fade.
Home translates its personality symbols twelve pixels downward from the standard
anchor, retaining their scale and the complete status exclusion. A change between
standard and Home layouts re-primes position even while animation is paused;
it does not restart the cue's fade, hold or sound. Wi-Fi and compaction symbols
therefore use the same reserved geometry in firmware and the native Studio bridge.

Debug Pause/Play at (158,47), Skeleton at (308,47), Screenshot and debug-view Back
retain authored IDs but are hidden and noninteractive by default. Raster and hit
testing share visibility. Setup Back/Cancel is a separate control. Release keeps
the informational layer while omitting Debug controls.

Debug diagnostic rows occupy y=390-445: measured FPS/CPU, IPv4, free internal RAM/
PSRAM, SD presence/access and measured free space. Invalid samples show their
actual unavailable state; without card detect, SD probe failure is UNCONFIRMED.

## Voice recording

The dedicated recording route begins with `Preparing audio` and `Please wait`.
Only its first completely retired frame admits the owner's three-second start
countdown; a failed or partially transferred frame cannot start capture. The
title then reads `Start listening in`, followed by the canonical `3 seconds`,
`2 seconds` and `1 second`. The display does not reconstruct time or invent
missed seconds. PCM capture begins only when the owner enters Listening.

Five seconds of acquired silence, including silence before the first speech,
or an admitted manual finish enters the three-second send countdown. Its title
is `Sending the audio in`; transport does not send END until that countdown
finishes. Preparing and both countdowns show `Tap to cancel` and `Or shake to
discard`. One acquired tap cancels immediately anywhere on the visible route.
Listening retains single-tap send with a 350-ms double-tap arbitration window,
double-tap discard and shake discard. Every phase change retires pending contacts
and sends. Actual Sending shows `Finishing delivery`: cancellation remains
subject to the transport owner's pre-END admission, without promising that an
already delivered recording can be recalled.

The title remains at y=92, genuine identity metadata at y=134, and the purple
procedural ribbon at (73,179), 320 x 58. Countdown text occupies y=260 and its
entire [260,288) damage band is erased when the number changes, including the
plural suffix. Cancellation hints sit at y=334 and y=362. Listening elapsed time
uses the canonical capture-start timestamp, excluding preparation and countdown;
an invalid timestamp shows `Timing unavailable`. The existing animation cadence,
row buffers and canonical microphone projection are reused. Before Listening,
the ribbon is ambient artwork and does not claim captured PCM. Native preview
fixtures label synthetic input explicitly; they do not validate physical audio.

## Boot and capture

Cold-boot title Cody AI Companion is at y=60 and subtitle A distraction-free
sidekick at y=110, centered using their actual fonts. The complete floppy artwork
is (137,164), 192 x 219, with the original 42-frame/3500-ms cycle. Firmware and
Studio share decoder, serialized source-row ownership and text composition.

The Debug PWR capture countdown occupies [153,313) x [174,292) with no touch
target. After an overlay-free frame retires, capture composes temporary 466 x 466
RGB565 PSRAM, then transfers ownership to storage for atomic top-down 24-bit BMP
publication under `LUNASHELL/screenshots/`. SAVING/SAVED/FAILED follows the real
result. PWR timing and power transitions belong to the shared power owner.

## Native control rasterization

Current rounded radii use immutable 8 x 8 coverage masks; 24 x 24 semantic vector
icons use 4 x 4 coverage sampling. Whole cells bypass sampling. RGB565 composition
respects borrowed stride, byte order and background. Borders have a two-pixel
inset; gradients use eight-bit row weights. Masks are flash data, with no per-frame
supersampling/cache. Other supported primitive radii compute the same sampling
rule when requested. Hit testing preserves pixel-center geometry.

Committed fonts remain 2bpp. The updated generator supports 4bpp with 4x
supersampling, subject to explicit licensed-source regeneration and unchanged
metrics. Source preparation does not establish physical quality. Rendering and
retired-frame damage include old/new content and action bounds.
