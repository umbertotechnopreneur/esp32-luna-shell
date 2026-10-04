# Cody onboarding and connectivity

The navigation atlas page 02 and addendum A01/page 13 define the network journey;
addendum A02/page 14 defines reusable screen 11. The printed IDs remain unchanged:
Hello is 04, and screen 10 preferences remain unimplemented. Startup/text/standby
contracts are in [runtime](runtime.md).

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
| Failed / TimedOut / presentation error | 08 / actual error and gated Retry/Cancel |
| 08, admitted clean Retry | Immediately 07 for the new attempt |
| Online + healthy current Wi-Fi association/IP | 09 / You're online. |
| 09, Continue | 11 / Cody's look when jelly is compiled |
| 11, Back or Done | Supplied destinations, currently both 09; disabled while saving |

05 starts at Hello's `(233,225,130)`, moves in 1,000 ms to `(233,105,65)`, waits
300 ms, then types title, three captions and Show QR at 45 ms/character. Its live
blink, breath, IMU and elastic touch response continue. No content action is
admitted during movement/wait.

06 is white, hides its heading, places Cody at `(233,94,58)` and shows an
installation QR of 240×240 at `(113,140)`. Scale 58 is smaller than 05's final 65.
05/06 use Show QR / Pair Cody blue footers at `(113,390,240,40)` and neither draws
nor hit-tests lateral controls. The inherited installation payload is
`https://play.google.com/store/apps/details?id=com.espressif.provble`; it is not a
credential and does not start BLE.

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

## Backend admission and QR ownership

The standalone connectivity layer compiles the HTTP server without GUI dependencies.
Boot initializes saved-profile Wi-Fi, retains the controller DMA reservation and
initializes the async pairing owner; it does not advertise BLE or open a session.
Consumers use `LUNASHELL_platform.h` and the canonical snapshot, either
`DeviceSnapshot.radios.pairing` or `readConnectivityObservation`.

`LUNASHELL_pairing_request(action, observation.radios.pairing.attempt_id)` submits
typed START/RETRY/CANCEL. `ESP_OK` means admitted, never Online or Cancelled. Keep
the attempt ID in route context to reject delayed events; display code does not
initialize radios, query SDK Wi-Fi or perform synchronous manager cleanup.
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

The backend has a five-minute deadline. Retry requires successful cleanup;
`cleanup_error` is separate from the attempt failure. If closure failed, show it
and allow explicit Cancel to retry cleanup; a poisoned manager stays blocked.
Cancel wipes the displayed QR immediately but navigation waits for Cancelled.
Stored SSID, BLE connection, credentials, DHCP initiation or action admission
never establish Online. See [connectivity/pairing](../connectivity-pairing.md) for
the acquisition, worker and cleanup implementation.

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
selection and type Error: 0x... / Tap a look to retry. Optional preference failure
does not hide a separate fatal identity, snapshot-publication or renderer error.
Retiring a view does not cancel a running copied request: its result is published
and applied between frames without reopening that view.

`LookNavigationContext` supplies caller/Done destinations; there is no inferred
navigation stack, setup-complete action or implemented screen 10/36 flow. Back/Done
are graphite chevrons in 60×60 circles at `(10,203)` / `(396,203)`. The cloud remains
front-facing and animated; skins never become QR decoration. Typed row/status
prefixes and persistent text generations obey [runtime](runtime.md), including
offscreen clipping and silent scroll baselines.

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
read-only endpoints use Bearer authentication and exclude QR secrets. The transport
is local HTTP, not HTTPS or an implemented PC/MCP companion session. Endpoint and
authentication detail belongs in [HTTP API](../http-device-api.md).

Actual Android scanning, wrong credentials/missing AP, DHCP, BLE handoff, repeated
retry/cancel, wake reconciliation, LAN access and asynchronous NVS/rotation/route
retirement races still need approved end-to-end device observation. No host fixture
or successful flash proves those properties.
