# Cody onboarding and connectivity

The navigation atlas page 02 and addendum A01/page 13 define the network journey;
addendum A02/page 14 defines reusable screen 11, and addendum A04/page 16 records
the pairing handoff and continued first-run route. Existing IDs remain unchanged;
only the Debug-only pairing screen adds ID 39. Startup/text/standby contracts are
in [runtime](runtime.md).

## Current route sequence

| Route/action | Current result |
| --- | --- |
| 04, complete Hello text retired + 1 active second | 05 / Let's pair |
| 05, Show QR | 06 / app installation QR; no radio action |
| 06, Previous swipe | 05; no invisible lateral Back target |
| 06, Pair Cody admitted | Immediately 07 / Starting Bluetooth |
| 07, current attempt ReadyToScan + QR encoded | Remain 07 / Scan to pair + session QR |
| Credentials / Connecting / Completing | Retire QR; type actual progress in 07 |
| 07, Cancel | Wipe QR and await confirmed closure, then 06 |
| 05 / 06 / 07, 30 active seconds without touch or genuine pairing progress | Return immediately to 04 / Welcome; retire old presentation work and close the owned attempt asynchronously |
| Timeout-returned 04, fresh tap after confirmed clean closure | Resume at 05; no boot sequence is replayed |
| Failed / TimedOut / presentation error | 08 / actual error and gated Retry |
| 08, Retry after clean terminal failure | Return to 05; Show QR and Pair Cody admit the next attempt |
| 08, Retry with an active session or failed cleanup | Request typed Cancel, keep 08 visible, then return to 05 only after clean Cancelled |
| Online + healthy current Wi-Fi association/IP | Persist and read back `cody_firstboot=false` |
| Persisted handoff + BLE advertising restored, Debug | 39 / Pairing debug; any awake tap dismisses |
| Persisted handoff + BLE advertising restored, Release | Skip 39 and enter 09 directly |
| 09, Continue | End pairing-route ownership and enter 10 / Make it yours |
| 10, Next | 36 / Settings |
| 36, Continue | 11 / Cody's look |
| 11, explicit row choice + Done | 12 when `CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=y`; otherwise 13 |
| 12, CCP1 pairing starts | Show the six-character display-only code; no manual success action |
| Matching CCP proof + two-sided persisted-link confirmation | 13 / You're all set. |
| 13, Let's go | 16 / native Idle animation for 10 active seconds |
| 16, ten active seconds elapsed | 14 / authenticated search for the retained PC identity |
| Matching authenticated READY | 15 / CCP is ready; Wi-Fi presence alone never promotes it |

05 starts at Hello's `(233,225,130)`, moves in 1,000 ms to `(233,105,65)`, waits
300 ms, then types title, three captions and Show QR at 45 ms/character. Its live
blink, breath, IMU and elastic touch response continue. No content action is
admitted during movement/wait.

06 is white, hides its heading, places Cody's worried face at `(233,94,58)` and shows an
installation QR of 240×240 at `(113,140)`. Scale 58 is smaller than 05's final 65.
05/06 use Show QR / Pair Cody blue footers at `(113,390,240,40)` and neither draws
nor hit-tests lateral controls. The inherited installation payload is
`https://play.google.com/store/apps/details?id=com.espressif.provble`; it is not a
credential and does not start BLE.

08 uses the separate afraid expression: open eyes, inward-raised worried brows and
a narrow open mouth. Its only footer label is Retry. A failed installation QR
returns to 05 without radio cleanup because that work starts no session. A clean
terminal pairing attempt also returns to 05 without starting another attempt.
The new attempt begins only when Pair Cody is pressed again on 06.
If pairing is already Online and only the local first-boot write failed, Retry
retries that write on 07 instead of starting another BLE pairing attempt.

07 enters on action admission, not radio success. Its white scene places Cody at
`(233,80,46)` above title y=131. ReadyToScan types Scan to pair and displays the
current session QR, 204×204 at `(131,182)`, inside the same 07. There is no active
06-bis transition; reusable `scan-to-pair` remains authoring data only. Progress,
cancellation and cleanup replace the QR rather than showing stale session data.
05–09 use state-admitted captioned footer actions and hide lateral arrows/targets.

09 continuously checks live Wi-Fi, not just the completed pairing result. Lost
association/IP replaces the online claim with No connection and Wi-Fi disconnected
or UNAVAILABLE. Software wake reconciles these same observations before exposing
a retained route; closed sessions return to 06 or their actual terminal state.
Its scene override requests standby after 60 seconds. Wake restores the retained
09 route and does not activate Continue; only a fresh user press advances to 10.

After the same current attempt publishes Online with a healthy association and
nonempty IP, the action worker writes `cody_firstboot=false`, reads it back and
rejects any mismatch before success UI. Debug builds then show reusable text route
39 with a micro Cody cloud and the actual copied SSID, IP and BLE name plus the
handoff milestones. It has no button or invisible control; any awake tap dismisses
it. Release compiles out its scene definition, text and atlas mapping and enters
09 directly. The current BLE name is `CODY-` plus the final two MAC bytes.

## Backend admission and QR ownership

The standalone connectivity layer compiles the HTTP server without GUI dependencies.
Boot initializes saved-profile Wi-Fi and the async pairing owner. Without saved
credentials it retains the controller DMA reservation until explicit Pair Cody;
with credentials it transfers that lease to BLE telemetry. Boot does not open a
provisioning session in the Cody profile.
Consumers use `LUNASHELL_platform.h` and the canonical snapshot, either
`DeviceSnapshot.radios.pairing` or `readConnectivityObservation`.

`LUNASHELL_pairing_request(action, observation.radios.pairing.attempt_id)` submits
typed START/RETRY/CANCEL. `ESP_OK` means admitted, never Online or Cancelled. Keep
the attempt ID in route context to reject delayed events; display code does not
initialize radios, query SDK Wi-Fi or perform synchronous manager cleanup.

Explicit Pair drains and temporarily parks native display DMA before stopping
telemetry or acquiring the controller lease. The gate stays closed through
Wi-Fi/manager initialization and controller startup, then reopens on success or
failure without changing the separate standby gate. Admission still requires a
34,816-byte contiguous hold, a 30,720-byte controller block and 4,096-byte free
headroom. Compact `Pairing DMA` records show actual free/largest blocks at each
boundary; insufficient memory remains a real error, not successful pairing.
One static owner retains the 34,816-byte lease across standby and every failure
before controller handoff. It is released only immediately before controller
initialization. Cody parks display DMA during radio/session closure, then reclaims
the lease after the SDK confirms an idle controller. Repeated reserve calls reuse
the held block. This is a lifetime budget, not a second allocation while the
controller is already using its exchange memory.

Radio closure returns `cleanup_error` and `dma_reservation_error` separately.
An allocation failure remains published in BLE and pairing snapshots, but does
not misreport a closed manager as still open or prevent clean Retry navigation.
Known-off radios may enter standby with that explicit budget error; the next
Start must pass strict admission again. Failed or uncertain hardware cleanup
never attempts reallocation. NimBLE host allocations use PSRAM; the controller
DMA block remains internal. Frame admission captures deliberate parking under
the display lock so a fast gate reopening cannot terminate Cody.

Published states are Idle, Starting, ReadyToScan, Connecting, Completing, Online,
Cancelling, Cancelled, Failed and TimedOut. Check validity/health/staleness before
interpretation. A valid degraded failure remains displayable with its real error.

Only ReadyToScan may copy `LUNASHELL_provisioning_copy_qr(buffer, capacity,
provisioning_session_id)` into a caller-owned buffer of at least 192 bytes. A racing
or expired session returns `ESP_ERR_INVALID_STATE`: reread and follow current
state, never reuse old QR data. Proof of possession rotates each attempt. Retire
and wipe the secret payload/modules; never put them in logs, snapshots, HTTP or
files, and do not call the GUI QR asset owner.

Both QR viewports use integer modules, a four-module white quiet zone and bounded
version 10 storage: at most 57×57 modules / 407 packed bytes, no QR framebuffer or
resampled bitmap. QR encoding errors remain visible and require explicit retry.
The pinned QR dependency is `espressif/qrcode` `0.1.0~2`; dependency-lock updates
remain explicit reviewed platform workflows, not silent resolver changes.

The backend has a five-minute deadline. A new pairing attempt requires successful
cleanup; `cleanup_error` is separate from the attempt failure. If 08 still owns an
active session or cleanup failed, Retry requests the typed Cancel operation and
stays disabled on 08 until the canonical state confirms clean Cancelled. A failed
closure remains visible with its real error and can be retried explicitly; an
uncancellable state remains blocked rather than presenting a false return to 05.
Cancel on 07 wipes the displayed QR immediately but navigation waits for Cancelled
and then returns to 06.
Stored SSID, BLE connection, credentials, DHCP initiation or action admission
never establish Online. See [connectivity/pairing](../connectivity-pairing.md) for
the acquisition, worker and cleanup implementation.

A fixed-capacity screen policy gives 05, 06 and 07 their separate 30-second
inactivity return. It uses the awake route clock: pausing animation does not pause
inactivity, while standby and the lifecycle wake sequence consume no timeout time.
Fresh presses, actual contact
movement/releases and canonical progress in the current attempt refresh it;
animation, typewriter prefixes, IMU movement and repeated unchanged snapshots
do not. This policy does not write settings or change the five-minute backend
deadline or the scenes' 60-second standby overrides.

The timeout retires the route generation and wipes the displayed QR before showing
the existing `Hi, I'm Cody!` Greeting scene. Its normal teletype remains paused when
Debug stops animation; its route inactivity clock is separate. This returned Welcome
stays visible until a fresh tap. A detached cleanup context retains only the old attempt identity:
an outstanding admission result can identify the attempt to cancel, but late QR,
progress and first-boot results cannot restore their old route or start a new
first-boot completion write. Running worker operations retain ownership until they
actually exit. New navigation remains blocked until canonical clean closure and
the bounded action mailbox has drained. Cancellation admission is not closure;
an actual cancellation or cleanup failure remains visible on 08 with its real
error and explicit Retry.

## Reusable look selector 11

The live preview uses `(233,172,44)`, title y=78 and viewport `(117,226,232,146)`.
Rows are 220×38 with 44-pixel spacing, fixed capacity 8 and a visible scroll thumb;
vertical drag has no momentum. The five flash-resident looks are Pearl Mist,
Aurora Veil, Cosmic Stardust, Enchanted Mosaic and Dreamwave. Their stable catalog
IDs, not names/list order, are persistence identities. `cody_skin` persists Pearl
Mist only when missing; unknown ID, invalid type or failed read is an error,
never repair.

One copied stable-ID request enters the static 4-KiB action worker's single-slot
mailbox. It performs admission, QR work and NVS writes outside the display worker;
radio lifecycle remains with its owner. During Saving... the old preview stays
active, navigation/second save are disabled and controls dim. The worker persists
only. On success the display owner selects the skin between retired frames.

`DeviceSnapshot.character_look_save` publishes stable ID, monotonic request ID,
Saving/Saved/Failed, original error and actual start/completion timestamps. It is
an operation result, not a sensor measurement. Failed writes retain the previous
selection; the disabled footer shows the real `Error: 0x...`, and tapping a look
starts an explicit retry. Optional preference failure
does not hide a separate fatal identity, snapshot-publication or renderer error.
Retiring a view does not cancel a running copied request: its result is published
and applied between frames without reopening that view.

`LookNavigationContext` supplies caller/Done destinations; there is no inferred
numeric navigation stack. During first setup screen 11 exposes no Back control.
Its 240×40 Done footer is initially disabled even though Pearl Mist is visibly
selected. Tapping any row is mandatory; tapping the already selected row counts as
explicit confirmation, while a different row enables Done only after its save
succeeds. Screens 36 and 11 keep reusable entry points for later personalization.
The cloud remains front-facing and animated; skins never become QR decoration.
Typed row/status prefixes and persistent text generations obey [runtime](runtime.md),
including offscreen clipping and silent scroll baselines.

## Continued setup and Codex gate

Screen 10 presents the compact Make it yours preference categories and advances to
the reusable Settings route 36. Screen 36 currently admits Cody's look and advances
to the shared selector 11. Their identities remain stable so later Settings hosts
can reuse 36 and 11 without duplicating preference or look state.

`CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX` is an explicit boolean. The Cody profile and
Kconfig default currently set it to `y`, so Done from 11 enters screen 12; `n`
skips to 13. With the gate enabled, screen 12 starts a five-minute CCP1 pairing
window and displays six characters from
`ABCDEFGHJKLMNPQRSTUVWXYZ23456789`. The code never enters the canonical snapshot,
HTTP, logs or a datagram. There is no Done button. Cody first persists the link
and authenticates `PAIRED`; CCP then protects its matching PC record and returns
`CONFIRM`. Only the authenticated `ACTIVE` completion can enter 13, so neither
side presents a one-sided retained link as complete.

Let's go from 13 retires the setup scene and leaves the normal native Idle
animation visible for ten seconds of the existing active clock. Standby does not
consume that hold. At expiry Cody enters 14 and broadcasts authenticated FIND
messages for the one retained PC ID. Only a plugin with that PC ID and link key
can return a valid READY and promote screen 15. READY is repeated liveness evidence,
not evidence of a Codex task, project, chat or message transport; loss of liveness
returns the presentation to 14.

The `LUNASHELL_codex_link` owner binds UDP 42117 and publishes only non-secret
`DeviceSnapshot.radios.codex_link` state. It generates the display code, salt and
verifier, validates proofs, persists one versioned CRC-protected `ccp_pairing`
record through the internal-stack settings owner, and wipes attempt secrets at
success, timeout or cancellation. A genuinely absent record is unpaired; wrong
size, version, CRC or contents is a visible failure requiring explicit repair.
The PC plugin listens on UDP 42118 and retains its independently generated PC ID.
The complete field order and proof inputs are versioned in
[`plugins/cody-codex-plugin/protocol/CCP1.md`](../../../plugins/cody-codex-plugin/protocol/CCP1.md).

CCP1 uses PBKDF2-HMAC-SHA256 and HMAC proofs but is a compact commissioning and
liveness protocol, not a formal PAKE, TLS replacement or security certification.
The future bidirectional context/status/message transport remains a separate
product and security boundary.

## Input and public HTTP boundary

Draw/hit-test geometry uses the shared 466×466 circle and 320×320 safe square.
Released horizontal swipes inside the safe square invoke the same typed actions:
48-pixel displacement, horizontal dominance, axis lock after 12 pixels. Vertical
list scrolling and short cloud drags retain ownership. Route/orientation changes
cancel a pending gesture; swipe never bypasses busy or cleanup gates. Debug toolbar
contacts cannot become route gestures. See [circular layout](../circular-display-layout.md).

HTTP listens on the actual station IP at 8080 only when explicitly enabled; pairing
does not enable it. `httpServerSetApiKey` / `httpServerSetEnabled` run outside the
renderer. Only absent `http_enabled` persists default false. Missing key means
unconfigured; empty/corrupt/oversized saved key fails. Health is public; protected
read-only endpoints use Bearer authentication and exclude QR and CCP1 secrets.
`radios.codex_link` exposes state, identities and liveness timestamps only. The
transport is local HTTP, not HTTPS or the future Codex message/session transport. Endpoint and
authentication detail belongs in [HTTP API](../http-device-api.md).

Actual Android scanning, wrong credentials/missing AP, DHCP, BLE handoff, repeated
retry/cancel, wake reconciliation, CCP firewall/broadcast behavior, concurrent
plugin instances, LAN access and asynchronous NVS/rotation/route retirement races
still need approved end-to-end device observation. No host fixture or successful
flash proves those properties.
