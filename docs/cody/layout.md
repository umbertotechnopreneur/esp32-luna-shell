# Cody layout on the Waveshare round display

Native `CODY2D` layout consumes the board's
[geometry](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md).
The repository contract is in [AGENTS.md](../../AGENTS.md). These coordinates
describe the current 466 x 466 adapter; they are not requirements of every future board.

## Content, actions and Back

Software standby blanks the display while the device model retains the current
route. Logical placement, clipping and wake use the selected orientation.

Use the central [73,393) square as a baseline. Expand into the circle only with
local-chord admission of artwork, controls and complete hit bounds. Keep QR
quiet zones intact. Scene content and lower actions have separate rectangles.

Back occupies (10,203), 60 x 60, with one centered left chevron and no caption.
Show it with a valid previous route. Retain the complete circular touch target
and keep it separate from central content and primary/footer actions.
The native style is subdued graphite with a light chevron.

Actions use semantic colors and restrained gradients. Standard buttons center a
text-only caption in the complete control. Device-first pairing adds outline
glyphs to identify the Wi-Fi, QR and Bluetooth actions. On the Setup menu, Connect Codex and
Set up Wi-Fi appear as two vertical rows with a link and Wi-Fi glyph on the left,
respectively. The menu omits its heading and keeps Options below the two rows.
Back retains its separate chevron. Complete controls appear after any title and
main body line, with touch admitted at the same time. Captions have no typewriter
timing or character audio.

| Control | Logical rectangle |
| --- | --- |
| Two cards | (73,260,154,104) and (239,260,154,104), 28-pixel corners |
| Third action for PC management | (153,376,160,52) |
| Setup menu rows | (93,230,280,56) and (93,298,280,56), 16-pixel corners |
| Setup menu Options | (153,382,160,52) |
| Options and category rows | Up to three at x=93, width 280, height 72, y=130/210/290 |
| Options footer | (123,382,220,52), with an outline home glyph next to Go home |
| Factory reset confirmation | Cancel at (93,270,280,64), Reset device at (123,382,220,52) |
| Visibility toggle | (113,226,240,52) |
| Ordinary single action | (113,352,240,72) |
| Compact QR/skin footer | (123,390,220,44) |
| Numbered reply / pending-question rows | (73,256,320,60) and (73,328,320,60) |
| Question-list hint | (99,394,268,25), separate from the wide content slot |

The outer corners of each control fit within the circular chord at its row.
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
Firmware and Studio use the same native color policy. Simulator and physical
visual confirmation of this indicator remain pending.

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

Device-first pairing uses the same title and body fonts, left chevron, and
capsule geometry as the dashboard and Options. The dark canvas, amber offline
glyph and restrained Options-style button gradients carry across the Wi-Fi
setup journey.
Both QR screens keep their white area within the square code tile.

| Screen | Artwork/content |
| --- | --- |
| 04 | Amber offline glyph above Not connected at y=180, Wi-Fi caption, and Tap to pair at (113,352,240,72) |
| 05 | Phone download glyph, Wi-Fi setup title, and two instructions with App download QR at (93,274,280,62) and Show pairing QR at (93,348,280,64) |
| 06 | Phone download glyph, app name at y=139, installation QR 192 x 192 at (137,165), App Store caption at y=360, and Show pairing QR footer |
| 07 | Bluetooth glyph, title at y=115, and session QR 204 x 204 at (131,182) while ReadyToScan |
| 10 / Options | Title at y=82, up to three enlarged rows, lateral paging arrows, and the Go home footer at y=382 |
| 11 / Skin | Preview (233,172,44), title y=78, viewport (117,226,232,126), Done (123,386,220,48) |
| 42 / Wi-Fi offline | Amber offline glyph, title at y=180, Set up Wi-Fi at (93,272,280,58), and Later at (153,350,160,50) |

Skin rows are 220 x 38 with 44-pixel steps. Only complete rows are drawn and
interactive; scrolling admits three visible rows. Stable look IDs and save gates
are described in [navigation](navigation.md).

Session QR storage supports version 10: at most 57 x 57 modules, 407 bytes.
Use integer modules and four white quiet modules. Payload/modules belong to the
current attempt; stale data cannot authorize presentation.
The QR journey uses captioned forward actions. Screens 05 and 06 expose lateral
Back. Screen 06 also accepts a previous-route swipe.

## Companion panels, status and Debug

When Codex is the current source, Home uses four lower panels: Tokens,
Model & Context, Device, and Chat & Activity.
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

Source selection requires fresh authenticated Codex MCP presence and a current
READY connection, even without an active chat. Otherwise Cody presents the local
companion. Account usage, LAN connectivity and app presence are independent
observations. The local MCP stops when its PC stops.

Local Home says `Here with you`. It keeps one calm panel
instead of rotating empty Codex measurements. A tap toggles that panel and Device;
a deliberate shake returns Home. Genuine source gaps remain explicit once, and
Device retains connection diagnostics. Local mode never replays the finite
Codex command footer. Returning devices resume Home during optional connection
failure, and an explicit Later choice completes first setup through the settings
owner. Setup and pending remote questions retain their routes.

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
Unavailable context uses one descriptive row and no empty decorative arc.
A connected Codex without a selected chat shows `No active chat` instead of
repeating missing title, elapsed-time and activity fields.

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

## Reset countdown

The reset screen opens first from Options > Codex stats. Four large split
tiles show days, hours, minutes and seconds. A tile that changes
folds its old upper half toward the hinge for 100 ms, then unfolds its new
lower half for 100 ms, matching the timing of the Windows widget. The reset
date in monospace alternates between UTC and the configured local offset.
The remaining percentage sits below it. Awake informational and action pages
share the five-pixel gradient perimeter when the weekly sample is valid. The
optional globe screensaver temporarily hides this chrome. A full allowance
fills the circumference.

The reset instant and percentage come from the Codex App Server through the
paired companion link. The countdown uses valid clock observations from
Cody. Clock corrections and skipped seconds update the tiles directly. An
expired window or stale source clears the quota arc and its numeric value.

## Codex statistics

The two graphite lateral chevrons reuse the Settings targets at (10,203) and
(396,203). They move among enabled dashboard pages and fade after ten seconds
without touch. Touch or a shake restores them. About traverses System, Memory
and Firmware, then the project QR. Back from System returns to the preceding
widget; Back from the terminal QR returns to Firmware. The QR has no Next
control and uses integer modules with a four-module quiet zone. A shake returns
to the first enabled statistics screen. Rasterization and touch share the
current logical rotation.

Token History uses an ECG-style grid and connects only adjacent measured
15-minute intervals. Its central control cycles through 1, 5 and 24 hours.
It starts at 5 hours. Missing intervals are gaps and measured zero is a real
point at the baseline. The labels use the configured local zone when both
endpoints can be projected. ACCOUNT identifies changes in the service-reported
account counter, preferring lifetime tokens to a daily bucket. Provider lag
can shift when these changes are observed. The grid appears only when the
selected range contains a real point; otherwise a collection, stale or missing
state replaces it. Context measurements are separate from the account series.

## System information

The three Info pages use a small outline icon and the same title baseline,
with a `1/3`, `2/3` or `3/3` indicator at the right of the title and a divider
at y=125. Their subpage identity belongs to the device model.

System labels CPU 1 and CPU 2 at y=137/199, with independent 0-100 percent
values and 280 x 10 bars at (93,176)/(93,238). These correspond to processor
cores 0 and 1. Current uptime uses label/value rows at y=263/288, with previous
uptime at y=328/353. The previous value is the last retained observation, not
an inferred complete session duration.

Memory separates INTERNAL RAM and PSRAM. Each block shows used/total in KiB,
then a bar segmented into occupied, largest free block and remaining free
space. Free and largest-free captions retain exact measured heap values.
The totals describe allocatable heaps, not physical RAM or attribution to
Wi-Fi, graphics or audio. Flash is not included in the RAM total. Unavailable
or stale sections omit their numerical bars.

Firmware uses four label/value pairs: version at y=135/160, compilation
timestamp at y=197/222, timezone at y=259/284 and build host at y=321/346.
All CPU and memory fields come from the existing canonical diagnostics
observation. Presentation does not sample hardware or create another collector.

## Optional screensaver

Options > Behavior > More behavior exposes Screensaver, initially Off. After
fifteen idle seconds on an unobstructed dashboard, a softly colored globe
fills the otherwise black display. There is no text, quota perimeter or
navigation chrome while it is active. The animation runs at eight frames per
second and does not renew the power owner's inactivity time.

The first touch is consumed to restore the retained dashboard. Shake returns
to the first enabled statistics page. Pairing, the project QR, options, voice,
Codex questions and battery notices preempt the screensaver. Dimming and standby
remain owned by the existing power policy.

## Voice recording

The native recording route uses centered captions and explicit Send and Cancel
controls. Only a completely retired Preparing frame admits the owner's start
countdown. The display uses the recorded phase and countdown supplied by the
audio owner. Cancel is available during preparation and both countdowns. Send
and Cancel are available while listening. The transport determines whether the
recipient has confirmed the recording.

## Boot

Cold-boot title Cody AI Companion is at y=60 and subtitle A distraction-free
sidekick at y=110, centered using their actual fonts. The complete floppy artwork
is (137,164), 192 x 219, with the original 42-frame/3500-ms cycle. Firmware and
Studio share decoder, serialized source-row ownership and text composition.

## Native control rasterization

Current rounded radii use immutable 8 x 8 coverage masks; 24 x 24 semantic vector
icons use 4 x 4 coverage sampling. Whole cells bypass sampling. RGB565 composition
respects borrowed stride, byte order and background. Borders have a two-pixel
inset; gradients use eight-bit row weights. Masks are flash data, with no per-frame
supersampling/cache. Other supported primitive radii compute the same sampling
rule when requested. Hit testing preserves pixel-center geometry.

Home's committed Telex coverage uses 4bpp. Its small and heading faces now derive
immutable per-glyph advances from the existing coverage; numbers retain tabular
spacing. Measurement, clipping, reveal and marquees share these metrics. Existing
scene fonts keep their fixed advances. No bitmap regeneration or new font import
is required for this spacing change. Source preparation does not establish
physical quality. Rendering and retired-frame damage include old/new content
and action bounds.
