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
Standby and pause do not advance the Hello reading hold or typewriter.

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
| 06, Back | 05 |
| 06, Pair Cody | Queue explicit Start (or a new Retry of a clean terminal attempt); wait for the real session |
| ReadyToScan, current attempt/session QR encoded | 06-bis / Scan to pair |
| 06-bis, Back | Queue Cancel; wipe the device QR; await Cancelled before returning to 06 |
| Credentials received / Connecting / Completing | 07 / Pairing... |
| 07, Cancel | Await confirmed cleanup, then 06 |
| Failed / TimedOut | 08 / actual error; Retry only after successful cleanup |
| 08, Retry | New attempt and a new device QR; never replay the whole OOBE |
| Online plus currently valid Wi-Fi association and IP | 09 / You're online. |
| 09, Continue | 11 / Cody's look (screen 10 preferences remain separate work) |
| 11, vertical drag | Scroll the fixed-capacity look list only |
| 11, tap a look | Queue one stable-ID write; retain the previous preview until success |
| 11, write fails | Keep the previous selection, display the real error and allow an explicit retry |
| 11, Back or Done | Follow the supplied caller/Done destinations (both currently 09); wait while saving |

06 uses **Get pairing app**; 06-bis uses **Link your phone**. Both have the same
white background, black title at y=74, tiny live Cody at `(233,38,22)`, and a
black 18px step caption at y=117. The QR viewport is **270x270 at (98,147)**,
up from the earlier 210x210. It extends below the safe square but all bounding
corners remain inside the nominal circle, clear of both lateral touch targets.
There is no separate white-card border: the whole page is white. Only the
required four-module quiet zone and centering residual remain around the
integer-scaled symbol, per [DENSO WAVE's QR margin guidance](https://www.qrcode.com/en/howto/code.html).
This larger viewport keeps integer modules through version 10; no framebuffer
or resampled bitmap is introduced, and the two QR payloads remain distinct.

The installation QR encodes the actual inherited
Android provisioning-app installation URL:
`https://play.google.com/store/apps/details?id=com.espressif.provble`.
It does not start BLE. 06-bis encodes only the current device/session payload
copied through `LUNASHELL_provisioning_copy_qr`, scanned inside that app.
Neither illustrative QR drawings nor a previous attempt's QR enter firmware.

09 continues checking the live Wi-Fi observation. Losing the link replaces its
online claim with `No connection` and `Wi-Fi disconnected` or `UNAVAILABLE`.
The broader preferences route 10 is not implemented by this increment. Screen
09 therefore supplies the current direct Continue entry to the completed screen
11 selector; route 10 can become the caller later without changing screen 11.
The current development force-first-boot setting remains respected.

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
  OOBE uses discreet graphite circles with small light chevrons, not blue footer
  buttons. Previous/Cancel occupies `(10,203,60,60)`; Next/Retry/Continue/Done
  occupies `(396,203,60,60)`. Both are outside the safe square. Disabled actions
  retain a dimmed target and cannot invoke a command. Back appears only for a
  valid previous route; there is no synthetic navigation history.
- A released horizontal swipe in the safe square invokes those same typed
  navigation actions: left is Next, right is Previous. It needs 48 pixels of
  displacement and horizontal dominance. Axis ownership locks after 12 pixels;
  vertical look-list scroll and short Cody drags are retained. Route/orientation
  changes cancel a pending gesture. Start/Retry/Cancel still await their real
  backend state and confirmed cleanup; swipe never shortcuts those gates.
- Debug Pause/Skeleton is independent of the OOBE route's Back/Next policy.
  Rounded 44-pixel controls occupy the top rim, initially hidden until touch,
  with 5-second inactivity and 330-ms fades. A reveal-only touch never triggers
  a control, and toolbar contacts cannot become navigation swipes. Release
  excludes these inspection controls. The top area remains reserved for later
  battery/date/time chrome; that status implementation is not included here.
- Logical drawing and acquired touch share the existing rotation owner. The cloud
  remains animated/reactive at each authored scale; QR modules are never warped.
- One static 4-KiB action-worker stack moves Start/Retry/Cancel admission, QR
  encoding and explicit look writes off the display worker. Radio lifecycle,
  initialization and cleanup stay with the existing pairing owner. A single-slot
  release/acquire mailbox has no per-frame
  allocations or unbounded action queue.
- Version-10 QR modules fit in **407 bytes**, with a four-module white quiet zone
  and integer pixel scaling in a 270x270 logical area. No RGB QR framebuffer exists.
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
