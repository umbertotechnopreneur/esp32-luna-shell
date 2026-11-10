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
glyphs to identify the Wi-Fi, QR and Bluetooth actions. The Connections page
uses the Options heading position, a short instruction, and two standalone
glyphs side by side. Codex has a violet link and Wi-Fi has a mint signal icon.
The labels and their PC link and Join network hints share the glyphs' touch
areas. The lateral chevron handles Back. Complete controls appear after the
title and main body lines, with touch admitted at the same time. Captions have
no typewriter timing or character audio.

| Control | Logical rectangle |
| --- | --- |
| Two cards | (73,260,154,104) and (239,260,154,104), 28-pixel corners |
| Saved PC management | Three standalone 36-pixel icons, with 90 x 90 touch areas at x=83/188/293, y=302 |
| Connections actions | 54-pixel glyphs at (148,219) and (318,219), with 140 × 168 touch areas |
| Check relay, when configured | Text-only footer at (123,366,220,52). The latest check replaces the Connections hint at y=136. |
| Options and category rows | Up to three rows at x=93, width 280, height 72, y=130/210/290. Destination cards use 18-pixel corners and icon badges; settings use flat rows. |
| Visible screens | Three Options-sized rows, name on the left and visibility on the right, with page and save status below |
| Options footer | (123,382,220,52), with a centered Home caption |
| Restart / factory reset confirmation | Cancel at (93,270,280,64), confirmation at (123,382,220,52) |
| Visibility toggle | (113,226,240,52) |
| Ordinary single action | (113,352,240,72) |
| Compact QR/skin footer | (123,390,220,44) |
| Numbered reply / pending-question rows | (73,256,320,60) and (73,328,320,60) |
| Question-list hint | (99,394,268,25), separate from the wide content slot |

The outer corners of each control fit within the circular chord at its row.
Coverage is clipped to the rounded control; complete hit bounds remain admitted.

Device-first Options uses named Tools, Preferences and Device pages with three
page marks above the footer. Destination cards use a quiet graphite gradient,
a colored semantic icon, a title and a smaller description. Codex stats uses a
bar-chart icon. Behavior and Display retain flat rows with the canonical setting
value on the second line. Home and More options use the graphite footer palette.

Saved PC uses a link glyph above the title, an explicit PC-name-unavailable
caption, and the canonical configured Wi-Fi name. The current link projection has no
human-readable PC name. Printable SSIDs occupy up to two complete rows; stale,
missing or unsupported names are explicit. Keep, Re-pair and Forget use three
standalone icons with captions below, and Forget retains its separate confirmation.
The network label does not infer persistent storage from configured credentials.
These layouts have been prepared in source; visual and physical acceptance remain pending.

An amber heading marks a question that needs attention. The notice names the
question and offers Open question, or Open questions when more than one is
pending. The choice view keeps the question and its source context above the
rows. Reply rows contain one numbered line, with at most two visible rows. A vertical
swipe opens the page containing the third reply. Long row captions show an
explicit ellipsis; the tap opens their complete canonical label and description,
without sending anything. Review uses a rounded, read-only panel at
(73,191,320,134) with five 26-pixel-spaced lines at y=196..300.
Swipe moves backward/forward; a tap in the panel advances a page and
returns to the first page after the last. The separate `Send reply` capsule at
(113,352,240,72) is the only initial reply submission action. Scrolling never
replays text audio. Orientation, source changes and hidden controls cancel
uncompleted gestures.

The pending-question view shows the owner's pending count and up to two
canonical question titles. Selecting
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
Firmware and Studio use the same native color policy. Device-first presents a
complete frame; the legacy Studio inspector retains its animated reveal.

A typed `ReplyReady` notification uses the compact character at (233,92), scale
42, and one mint `Chat waiting` headline at y=150. The originating chat's complete
bounded title occupies up to three body rows at y=214/242/270. A red dot sits
beside its first line and appears with that line. When its owner has
no admitted title, the body says `Open the other chat in Codex`; it never repeats
the headline or borrows the currently followed chat's identity. Project context
is subordinate at y=312, with explicit unavailable/offline/error text when needed.
The `OK` capsule shows a check mark, retains (113,352,240,72) and
acknowledges the current LAN notice through its authenticated dismissal.
Controls appear only after the full narrative;
reply readiness implies neither task success nor passed checks. Generic notices
also omit body text when it is identical to their title. Terminal request outcomes
retain their own expired, cancelled or rejected captions.

## Events and Current turn

Events is the connected dashboard's starting page. Its authenticated queue
shows three rows, with open questions first and ready replies after them. A
vertical swipe moves one row. The lead question preview scrolls horizontally
inside its row; its event label and chat caption stay fixed. Tapping an exact
row opens the existing choice flow or the `Chat waiting` detail. Its `OK`
capsule shows a check mark and returns to Events after the broker confirms
read dismissal. A question caption comes from
its declared source context when one is supplied.

Swipe left on Events for Current turn, and right for statistics. Swipe right
on a statistics page to return to Events. Current turn uses the lateral Back
control. It shows the currently followed chat, an observed Shell, Web search
or Editing identity when available, and a separate compaction icon and measured
elapsed time. A hosted web search without a native hook remains unavailable.

## Setup, QR and Skin

The Home-launched Wi-Fi journey keeps its status chrome above the heading, a
subtitle below it, and the filled Home icon at (211,390,44,44). Choices occupy
(93,210,280,56) and (93,280,280,56). The platform selector uses two 145 x 42
controls at (83,154) and (238,154), followed by one 168 x 168 QR at (149,205).
The session QR occupies (137,164,192,192). Each QR retains four quiet modules
and integer module scaling. Back/Next remain in their separate lateral targets.
The Home button returns only after an owned pairing session has been cancelled.
The compact status uses actual clock, Wi-Fi RSSI, Bluetooth and battery observations;
its numeric battery percentage can be suppressed by the renderer, with a settings
control deferred. The Home notice occupies y=70..123 and uses the reported BLE
name, not an independently inferred identity.

The existing recovery entry retains the prior device-first pairing screens below.
They use the same title and body fonts, left chevron, and capsule geometry as the
dashboard and Options. The dark canvas, amber offline
glyph and restrained Options-style button gradients carry across the Wi-Fi
setup journey.
Both QR screens keep their white area within the square code tile.

| Screen | Artwork/content |
| --- | --- |
| 04 | Amber offline glyph above Not connected at y=180, Previous Wi-Fi and up to two rows for the saved name, and Tap to pair at (113,352,240,72) |
| 05 | Phone download glyph, Wi-Fi setup title, and two instructions with App download QR at (93,274,280,62) and Show pairing QR at (93,348,280,64) |
| 06 | Phone download glyph, app name at y=139, installation QR 192 x 192 at (137,165), App Store caption at y=360, and Show pairing QR footer |
| 07 | Bluetooth glyph, title at y=115, and session QR 204 x 204 at (131,182) while ReadyToScan |
| 09 / Wi-Fi result | Mint Wi-Fi glyph for a usable connection, amber offline glyph otherwise. Title at y=200, status detail at y=248, and up to two SSID rows from y=280. Continue at (113,352,240,72). |
| 10 / Options | Title at y=82, up to three enlarged rows, lateral paging arrows, and the Home footer at y=382 |
| 12 / Pair Codex | Title at y=88, link glyph at (205,148,56,56), and the six-character code at y=236 when ready |
| 14 / Waiting for PC | Same heading and glyph slot as Pair Codex, with two status lines and Later |
| 11 / Skin | Preview (233,172,44), title y=78, viewport (117,226,232,126), OK (123,386,220,48) |
| 42 / Wi-Fi offline | Amber offline glyph, title at y=180, Set up Wi-Fi at (93,272,280,58), and Later at (153,350,160,50) |

During passive reconnect, the 72-pixel signal glyph animates above Wi-Fi
connecting. Its lower rows light up in mint while the device waits for
association and an IP. Each station attempt can reopen the same animation.
Between attempts, the offline page shows the configured name even if the last
association failed. The waiting page has no touch action. A usable Wi-Fi
connection describes local network access. Codex link and relay availability
are independent.

Skin rows are 220 x 38 with 44-pixel steps. Only complete rows are drawn and
interactive; scrolling admits three visible rows. Stable look IDs and save gates
are described in [navigation](navigation.md).

Session QR storage supports version 10: at most 57 x 57 modules, 407 bytes.
Use integer modules and four white quiet modules. Payload/modules belong to the
current attempt; stale data cannot authorize presentation.
The QR journey uses captioned forward actions. Screens 05 and 06 expose lateral
Back. Screen 06 also accepts a previous-route swipe.
The Codex link glyph pulses during code preparation and while searching on a
usable LAN. The pulse repaints only its small display region. A ready code
stays visible above its instructions. A pairing error replaces the link with
an amber retry glyph and preserves the reported error.

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

The Cody Device diagnostics card displays measured CPU utilization and free
heap. The character HUD uses the lower arc for FPS/CPU, IPv4 and free internal
RAM/PSRAM. For the GPS prototype, the character HUD also has card access and
free-space rows. Its SD probe failure is UNCONFIRMED without a card-detect pin.

## Reset countdown

The reset screen opens first from Options > Codex stats. Four large split
tiles show days, hours, minutes and seconds. A tile that changes
folds its old upper half toward the hinge for 100 ms, then unfolds its new
lower half for 100 ms, matching the timing of the Windows widget. The reset
date in monospace alternates between UTC and the configured local offset.
The remaining percentage sits below it. Awake informational and action pages
share the outer gradient perimeter when the weekly sample is valid. An inner
battery ring uses a green-to-yellow fill over a graphite track while a valid
charge reading is available. A one-pixel radial gap separates the rings, and
navigation draws above them. The optional globe screensaver hides both rings.
A full allowance or charge fills its circumference. Both rings start at twelve
o'clock and retain their filled portion counterclockwise. The empty portion
grows clockwise from twelve o'clock.

The reset instant and percentage come from the Codex App Server through the
paired companion link. The countdown uses valid clock observations from
Cody. Clock corrections and skipped seconds update the tiles directly. An
admitted saved observation keeps the countdown available while the PC is off.
The amber `Saved` timestamp uses UTC. The saved percentage remains a historical
value, and the live perimeter requires a current weekly observation. An expired
window requires a fresh reset observation before showing a new countdown.

## Local timer

Options opens Timer with a large minutes and seconds readout. The duration
controls occupy a pair of 154 x 60 cards at y=247. Sound and Start use the same
pair at y=321. Running and finished timers use a centered 240 x 60 capsule for
Cancel or OK. The lateral Back target returns to the previous view. The
finished page distinguishes a pending, muted or unavailable alarm.

## Codex statistics

The two graphite lateral chevrons reuse the Settings targets at (10,203) and
(396,203). They move among enabled dashboard pages and fade after ten seconds
without touch. Touch or a shake restores them. About traverses System, Memory
and Firmware, then the project QR. Back from System returns to the preceding
widget; Back from the terminal QR returns to Firmware. The QR has no Next
control and uses integer modules with a four-module quiet zone. A shake returns
to the first enabled statistics screen. Rasterization and touch share the
current logical rotation.

Device groups the observed Codex, Wi-Fi and battery states into three flat
icon rows. The followed chat and Record voice appear when their observations
and controls are current.

Account and Token activity empty states use a colored icon, a mixed-case status
and short guidance. Stale, saved, unavailable and collecting observations remain
distinct. The history range control says Show 1 hour, Show 5 hours or Show 24 hours
and retains the same cycling action. Missing intervals remain gaps.

Reset uses four flip tiles while it has a valid duration. A clock glyph and a
single status occupy that area when it has no duration. Limits centers a single
reported window, and Credits centers the balance if voucher data is unavailable. Last 7 Days labels
partial coverage and distinguishes a measured zero from a missing day.

Credits places **Reset Vouchers** below the balance, with a 38 x 22 gold ticket
at the right. The viewport holds three complete numbered rows and moves one row
per vertical swipe when there are multiple vouchers. A slim position mark stays
within that viewport. Lateral arrows continue to change statistics pages. Each
row uses the configured timezone at the actual expiry instant, with a caption
such as `11 March 2027`. Equal dates retain separate rows. Unavailable local
dates, missing expiry details and explicit non-expiring vouchers are distinct.

Token History uses a linear scale with labels at zero, the midpoint and the
rounded upper bound. The trace connects only adjacent measured 15-minute
intervals. Its central control cycles through 1, 5 and 24 hours and starts at
5 hours. Missing intervals are gaps and measured zero is a point at the baseline. The labels use the configured local zone when both
endpoints can be projected. ACCOUNT identifies changes in the service-reported
account counter, preferring lifetime tokens to a daily bucket. Provider lag
can shift when these changes are observed. The grid appears only when the
selected range contains a real point. In other cases a clock glyph and the
collection, stale or missing state occupy the plot area. Context measurements
are separate from the account series.

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

Options > Behavior > More behavior contains the Screensaver setting, initially
Off, and a footer opening Synth Voice. Its three unavailable rows use the same
Options geometry until a speech engine is integrated. After
fifteen idle seconds on an unobstructed dashboard, a softly colored globe
fills the otherwise black display. There is no text, quota perimeter or
navigation chrome while it is active. The globe pulses from 50% to 100% of
its full color intensity over eight seconds, with two seconds at the minimum.
The animation targets 25 frames per second and does not renew the power owner's
inactivity time.

The first touch is consumed to restore the retained dashboard. Shake returns
to the first enabled statistics page. Pairing, the project QR, options, voice,
Codex questions and battery notices preempt the screensaver. Dimming and standby
remain owned by the existing power policy.

## Voice recording

The native recording route uses centered captions and explicit Send and Cancel
controls. The display shows the confirmed destination title above the animation
band during preparation, listening and transfer. Long titles keep both ends.
Only a completely retired Preparing frame with the destination admits the owner's
start countdown. Both countdowns show a centered 3, 2 or 1 below the confirmed
destination. The audio owner supplies the seconds and determines when to advance.
The red Cancel button stays below the number. The ribbon animates during
preparation, listening and sending; while listening, it responds to current
microphone levels. When START is still pending after the countdown, Waiting for PC
keeps the destination and Cancel visible without opening the microphone. Cancel
is also available during preparation and both countdowns. Send and Cancel are
available while listening.

Failure distinguishes a microphone failure, an audio link failure and an
authenticated PC rejection. The screen keeps the hexadecimal error and returns
automatically after three seconds. A PC rejection points to the PC audio setup,
where the native readiness failure is recorded. The transport determines whether
the recipient confirmed the recording.

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
