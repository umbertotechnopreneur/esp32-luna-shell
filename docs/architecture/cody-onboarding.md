# Cody first-setup network journey

Implementation source checkpoint, 4 October 2026. Build, host checks, simulator
and physical pairing remain unverified for this change.

The source of truth is navigation atlas page 02 for startup, **addendum A01,
page 13**, for QR routes (also planning atlas page 23), and **addendum A02,
page 14**, for screen 11 (also planning atlas page 24). The addenda supplement
the printed pages without renumbering them. Hello is screen **04**, not 06.

## Startup and entry

Cold boot/software reset: Sleeping01 fades the animated pearl character in over
2 seconds, Waking02 interpolates over 4 seconds, and Ready03 holds for 4 seconds.
Actual service checkpoints still gate advancement. First setup enters Hello04
with the Happy face and `Hi, I'm Cody!` monospace typewriter. After the complete
text has actually retired to the panel, it remains readable for one second of
active animation time, then enters 05. Non-first setup instead enters Idle16.

Sleep/wake panel fades use the shared 800-ms default. A software wake exposes an
already-rendered Sleeping01 through that panel fade, without multiplying a second
two-second character fade over it; Waking02 and Ready03 then receive 4 seconds each.
The active route, typed prefixes and entrance/text clocks remain in RAM during
software standby. After Ready, the host reconciles the canonical pairing/Wi-Fi
state before exposing that retained route; a session closed while asleep can
therefore return to installation or show its real terminal result. Standby and
pause do not advance the Hello reading hold or typewriter, and wake does not
restart first setup. Cold boot/software reset still follows the normal startup
and first-boot policy. Deep sleep resets RAM; this retention is not persisted
navigation or a promise to resume across deep sleep.

05 starts with the same large Cody placement as Hello. One reusable scene
entrance moves it upward and halves its scale over 1 second, then waits 300 ms
before revealing the title and captions sequentially at 45 ms per character.
The same snapshot-based motion, blinking, breathing and elastic drag remain active.
Happy now keeps both eyes open between natural blinks; its scene faces and named
catalog preset are changed together, preserving the compile-time identity contract.

## QR and radio routing

| Current route/action | Result |
| --- | --- |
| 04, intro fully displayed | 05 / Let's pair |
| 05, Show QR | 06 / installation QR |
| 06, Previous swipe | 05; no lateral Back control is drawn or hit-tested |
| 06, Pair Cody | Queue explicit Start (or a new Retry of a clean terminal attempt) and immediately enter 07 / Starting Bluetooth |
| ReadyToScan, current attempt/session QR encoded | Remain in 07; type Scan to pair and display that session's QR |
| Credentials received / Connecting / Completing | Remain in 07 and type the real progress message |
| 07, Cancel | Await confirmed cleanup, then 06 |
| Failed / TimedOut | 08 / actual error; Retry only after successful cleanup |
| 08, Retry | Immediately enter 07 for a new admitted attempt; show its QR only after ReadyToScan and successful encoding |
| Online plus currently valid Wi-Fi association and IP | 09 / You're online. |
| 09, Continue | 11 / Cody's look (screen 10 preferences remain separate work) |
| 11, vertical drag | Scroll the fixed-capacity look list only |
| 11, tap a look | Queue one stable-ID write; retain the previous preview until success |
| 11, write fails | Keep the previous selection, display the real error and allow an explicit retry |
| 11, Back or Done | Follow the supplied caller/Done destinations (both currently 09); wait while saving |

05 shows a captioned **Show QR** footer. 06 is a white installation page with
its heading hidden, a live cloud at `(233,94,58)` and a **Pair Cody** footer.
Its scale 58 is slightly smaller than 05's final scale 65. The installation QR
occupies **240x240 at (113,140)**. No lateral arrow or invisible lateral touch
target exists on either 05 or 06. Footer bounds are `(113,390,240,40)` and are
shared by rendering and hit testing.

07 is entered as soon as Pair Cody is queued. Its white scene keeps a live cloud
at `(233,80,46)` above the title at y=131. While starting, it types the actual
progress caption. ReadyToScan replaces that message with **Scan to pair** and
the current device/session QR in **204x204 at (131,182)**, within the same 07
route. Credentials/progress retire that QR and display the real backend state.
The current route has no 06-bis transition. The reusable `scan-to-pair` scene
definition remains authoring data; it is not an active onboarding route.

Both QR viewports retain integer modules through version 10 and a four-module
quiet zone, without a QR framebuffer or resampled bitmap. The full white page
supplies the backdrop. The two payloads remain distinct.

The installation QR encodes the actual inherited
Android provisioning-app installation URL:
`https://play.google.com/store/apps/details?id=com.espressif.provble`.
It does not start BLE. 07 encodes only the current device/session payload
copied through `LUNASHELL_provisioning_copy_qr`, scanned inside that app.
Neither illustrative QR drawings nor a previous attempt's QR enter firmware.

09 continues checking the live Wi-Fi observation. Losing the link replaces its
online claim with `No connection` and `Wi-Fi disconnected` or `UNAVAILABLE`.
The broader preferences route 10 is not implemented by this increment. Screen
09 therefore supplies the current direct Continue entry to the completed screen
11 selector; route 10 can become the caller later without changing screen 11.
The current development force-first-boot setting remains respected.

## Scene inactivity and typed text

Each immutable scene explicitly provides `standby_override_seconds`. Zero
inherits the user's configured screen inactivity timeout; a positive value
replaces it only while that scene is active. The pairing invitation,
installation, reusable scan definition and 07 pairing scene author 60 seconds.
Hello, 08, 09 and 11 inherit the configured policy. The power owner applies and
clears this runtime override without writing NVS or changing the saved timeout.
Scene entry supplies a fresh inactivity baseline; actual touch still refreshes
activity. This screen policy does not extend the backend pairing deadline.

Visible titles, body captions, footer labels and look-selector text use typed
prefixes captured before rendering. A separate 64-bit active text clock survives
long-running scenes and excludes pause/standby time. Replacing a progress message
restarts its text without replaying the cloud entrance. Look rows retain their
stable sequence during scroll/save; new `Saving...` or error/retry lines start
their own bounded sequence at the current text-clock anchor. Offscreen labels
do not contribute to the displayed character count.

After a complete frame retires, newly displayed characters can notify an
optional original 20-ms keyboard-like microclick. Its 640-byte mono PCM16 asset
is synthesized in firmware flash. The renderer only sends bounded notifications;
the audio owner performs codec work, preserves the existing volume and shared
transport clock, and suppresses cues during quiet hours, zero volume or standby.
An unavailable trusted clock suppresses cues only when both routines and
quiet-hours policy are enabled; disabling either permits them without that clock.
Profiles excluding audio or the speaker skip cue preparation and keep visual
startup available. Notifications coalesce at at most 20 taps
per second; expired/busy taps are dropped, not replayed later. Entry, message
replacement, scroll and wake prime a silent character baseline. Runtime playback failures
retain their real reported error without stopping this optional presentation
effect. Audible gentleness and speaker volume still require a listening check.

Ten separate archive-derived gentle SFX candidates were prepared only under
`artifacts/audio/cody-gentle-sfx/`, with an audition and source/trim manifest.
Their selection metadata is in `assets/cody/audio/gentle-sfx/`. These are private
listening artifacts, not firmware assets or automatic runtime cues. Source
redistribution rights and perceptual review remain unconfirmed.

## Screen 11 / Cody's look

Screen 11 uses one live preview above one vertically scrollable list. It does
not expose the internal word `skin`, a color editor, texture mixing or per-look
parameters. The visible names are Pearl Mist, Aurora Veil, Cosmic Stardust,
Enchanted Mosaic and Dreamwave. Pearl Mist remains the first-boot default.

Rows retain the catalog's stable IDs across reordering. A tap writes the exact
ID to the `cody_skin` user setting through the existing action worker before
changing the active texture; a failed write leaves the previous selection and
preview intact. Only a genuinely absent key creates and persists the Pearl Mist
default. Read/type/storage errors and an
unknown saved ID remain startup errors and are never repaired silently.

The list has a bounded fixed capacity, a visible scroll thumb and no momentum.
A drag changes only the list offset; a tap-sized release selects one row. The
same logical geometry is used for drawing and touch after the display owner's
rotation mapping. Back remains the separate left-side chevron. Done returns to
the configured completion destination; Back returns to the caller. Future Settings
can supply the same destination for both, while setup can supply different ones.

### Reusable selector and optional persistence

`CODY_look_screen` owns the bounded eight-row view, clipping, scroll/tap state,
selection, retained status text and the `LookNavigationContext`. Its API prepares
the same generated scene and emits a stable-ID save intent; it has no worker,
NVS call, per-frame allocation or host-specific return route. The onboarding host
supplies both `caller` and `done` and resolves the returned `SceneId` through its
normal route entry. The current 09 -> 11 -> 09 flow is preserved. Screens 10 and
36, setup completion policy and a general navigation stack are not implemented.
Returning to 09 invalidates its presentation cache so even an already-offline
caller is rebuilt from the next canonical connectivity observation.

One copied request occupies the existing action mailbox. `Saving...` and dimmed
lateral controls remain visible while the worker waits for settings. No second
save or navigation is admitted until its result is consumed. The worker persists
only; the display owner calls `select_jelly_skin` after success, between retired
frames, then updates the selector. It never changes skin midway through a frame.

Preference persistence is explicitly optional at the onboarding orchestration
boundary. `persist_jelly_skin` returns the original error. The worker publishes
`DeviceSnapshot.character_look_save` with the stable ID, monotonic request ID,
Saving/Saved/Failed state, original error and real start/completion timestamps.
This is an operation result, not a measured sensor sample. Failed writes show
`Error: 0x...` and `Tap a look to retry` while retaining the previous selection.
The original write failure and any error drawing that result remain separate;
snapshot-publication, identity and renderer failures are still fatal.

View reset retires its generation, not an already-running storage operation.
Requests own their copied IDs, terminal results are still published, and a
successful write is applied between frames without reopening a retired selector.
The no-connection title has static storage; status buffers belong to the active
view, while labels and scene definitions borrow immutable flash data. All view
mutations remain under the existing display ownership between complete frames.

## Ownership, memory and reusable pieces

- `CODY_lifecycle` owns active-time expression sequencing. One lightweight timing
  header shares the panel fade duration with the power owner.
- `CODY_scene` still owns cloud/title/optional content. `title_visible` and the
  explicit authored `background_rgb565` belong to reusable scene definitions,
  not special-case screen rendering or a fallback theme.
- `CODY2D::CapsuleControl` shares full bounds between drawing and hit testing.
  Network routes 05 through 09 use a captioned blue footer at
  `(113,390,240,40)` for Show QR, Pair Cody, Cancel, Retry or Continue, as admitted
  by the current state. Their lateral arrows and hit targets are hidden.
  Screen 11 keeps graphite Back/Done circles at `(10,203,60,60)` and
  `(396,203,60,60)`, outside the safe square. Disabled actions retain a dimmed
  target and cannot invoke a command. Back requires a valid caller destination.
- A released horizontal swipe in the safe square invokes those same typed
  navigation actions: left is Next, right is Previous. It needs 48 pixels of
  displacement and horizontal dominance. Axis ownership locks after 12 pixels;
  vertical look-list scroll and short Cody drags are retained. Route/orientation
  changes cancel a pending gesture. Start/Retry/Cancel still await their real
  backend state and confirmed cleanup; swipe never shortcuts those gates.
- The Debug Screenshot control is independent of the OOBE route's Back/Next
  policy. Its rounded 44-pixel target occupies the upper center, initially hidden
  until touch, with 5-second inactivity and 330-ms fades. A reveal-only touch
  never triggers it, and toolbar contacts cannot become navigation swipes.
  Pause/Skeleton remain hidden and non-interactive. Release excludes Debug
  inspection controls; the shared battery/date/time chrome keeps its own owner.
- Logical drawing and acquired touch share the existing rotation owner. The cloud
  remains animated/reactive at each authored scale; QR modules are never warped.
- One static 4-KiB action-worker stack moves Start/Retry/Cancel admission, QR
  encoding and explicit look writes off the display worker. Radio lifecycle,
  initialization and cleanup stay with the existing pairing owner. A single-slot
  release/acquire mailbox has no per-frame
  allocations or unbounded action queue.
- Version-10 QR modules fit in **407 bytes**, with a four-module white quiet zone
  and integer pixel scaling in the installation/session viewports. No RGB QR framebuffer exists.
  The pinned `espressif/qrcode` dependency is `0.1.0~2`, as in the existing full UI.
- The same licensed JetBrains Mono source supplies an additional 18px printable-
  ASCII caption/action font: **7,125 bytes in internal flash**, no runtime glyph cache.
  The existing title font is unchanged; no localization or SD dependency is added.
- The look catalog and all five packed textures stay in internal flash. The
  selector renders names from the catalog and never treats list position as
  identity.

All measured radio state comes from `DeviceSnapshotService.readConnectivityObservation`.
Validity/staleness and typed attempt/session identities gate every action and QR.
Admission never means Online or Cancelled. An encoding error remains visible and
requires explicit Retry; there is no automatic allocation/retry loop. Payloads and
QR modules are wiped when retired and are never written to logs, HTTP or files.
Snapshot-read/render failures still propagate their original errors.

## Validation status

The latest footer/07 routing, runtime standby override, retained software-wake
state, shared text clocks and original microclick changes have source review
only. The private archive preview generator ran; that asset preparation is not
firmware, simulator, audio-listening or hardware validation. The historical
authorized checks and flash below apply to their stated earlier revision.

The Cody-only QR manifest remains subject to the canonical dependency-lock
comparison. Any required reviewed lock update goes through
`scripts/device.ps1 update-lock -Profile cody-companion -BuildMode Debug
-BuildDir artifacts/build-cody-companion-debug`; never bypass that comparison.
Tests/build/flash require the user's explicit approvals.
The scene asset generator completed for eight scenes. This source preparation
is not firmware, simulator or physical-device validation.
The rounded navigation/swipe/entrance/open-eye increment likewise has no new
build, test, simulator run, flash or device observation.
The subsequent white QR-page increment regenerated definitions only. Its caption
and symbol quiet-zone readability, actual app scans, white-theme dirty redraw and
Debug fade still require authorized simulator/device checks. Pending-command
changes also advance the separate control revision, so dimmed lateral controls
are repainted when admission/QR work completes.

On 2026-10-04, the authorized `snapshot-integrity`, `cody-native`, `cody-runtime`,
`cody-response` and `cody-poc` checks passed. This includes the selector source
ownership guards, all five real look textures, actual C++ persistence with
host-injected read/write failures, missing-only default persistence, retry, and
the requirement that a successful write cannot activate the renderer selection.
The host suites also cover 32 preview contracts, 15 response tests and a
500-tick native/Python response fixture. Their settings fixture has no real NVS
driver and does not establish hardware write timing or asynchronous UI behavior.
The same authorized sequence built firmware revision `0295963d` with ESP-IDF
6.0.2, profile `cody-companion`, Debug and eight jobs. The app image is
3,550,736 bytes, with 76% of the app partition free. The flash on COM10 completed
with verified data hashes and a hard reset. The flash's incremental build also
passed; no monitor, capture, visual inspection or leak-soak check was performed.
Pending runtime checks:
repeated success/failure/retry, an eight-digit error code, busy navigation, route
reset during NVS work, rotation during a gesture, completed-frame ownership,
canonical result ordering, and no RAM growth across repeated entries. None of
these end-to-end properties is claimed as validated by the host checks.
