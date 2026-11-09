# Shared Wi-Fi / BLE pairing lifecycle

Luna and Cody use the same radio and session owners. Pairing is an optional
service, independent of the minimal display/touch baseline and UI selection.

The Wi-Fi acquisition owner adopts the manager's confirmed credential event
before its delayed connect runs. It accepts the first real IP event while
leaving credential persistence and provisioning retries to the stock manager.
It suppresses its own reconnect timer during that ownership interval. Direct
Wi-Fi connect, scan and stop calls are rejected while provisioning owns them.
Provisioning rejects a handoff while a local scan is active. Each owner reads
only its own completed scan because reading the AP list frees the driver buffer.

`LUNASHELL_radios_start_pairing()` stops telemetry before starting provisioning.
`LUNASHELL_radios_end_pairing(restore_telemetry)` stops and deinitializes the
manager before another host may use BLE. Cleanup errors block the handoff.
Telemetry restoration requires a confirmed Wi-Fi lease. These calls are
synchronous service operations and must not run in the display renderer.
Closing without telemetry restoration does not allocate the contiguous DMA
reservation for a future attempt. It leaves any existing reservation owned;
the next explicit Start acquires a missing reservation before Wi-Fi and
provisioning initialization. A failed future allocation remains a Start failure,
separate from successful teardown of the previous attempt.

`LUNASHELL_PROVISIONING_START_ON_BOOT` keeps the existing automatic behavior
for connected Luna profiles and defaults off for Cody. Cody retains the
controller's contiguous DMA reservation while it awaits an explicit pairing
action. Saved Wi-Fi profiles still use the normal station connection path.

Each provisioning attempt rotates a random 64-bit proof of possession for
the existing Security 1 transport. The device name uses the full station MAC.
`DeviceSnapshot.radios.provisioning` publishes typed lifecycle state, observed
errors, cleanup errors, timestamps and a monotonically increasing session ID.
Secrets are excluded. `LUNASHELL_provisioning_copy_qr` requires that exact active
session ID and returns a caller-owned copy for the pairing display only.
Legacy Luna getters remain for its existing UI; new consumers use the copy API.
Security 2 and smartphone interoperability still require separate review and
explicit validation; no production security certification is claimed.

HTTP initializes a logged, persisted `http_enabled=false` only when that key
is absent. A missing API key leaves protected routes unconfigured; corrupt,
wrong-type and oversized saved values still return their real errors. No
default Bearer credential is created. Diagnostics include provisioning state,
never QR contents, Wi-Fi passwords or proof of possession.

The event ordering is grounded in the pinned Espressif
[network_provisioning 1.2.4 manager source](https://github.com/espressif/idf-extra-components/blob/2de4980640bbe3d2d69473d7251640039e185b92/network_provisioning/src/manager.c).
The SET event precedes the config write; CRED_RECV follows the successful write
and the accepted one-second connect timer. No second SDK connect is issued by
the Wi-Fi owner.

## Asynchronous controller

`LUNASHELL_pairing_service_init()` creates one 4 KiB internal-stack worker and
publishes Idle. It does not start BLE. `LUNASHELL_pairing_request(action,
expected_attempt_id)` admits Start, Retry or Cancel against the exact observed
attempt. Its success acknowledges admission; the snapshot reports completion.
The controller rejects duplicates, stale identities and retry after failed
cleanup. Cancel may queue while Start is still running. The worker parks when
idle and observes an active attempt every 250 ms through `PairingObservation`.
This projection reads provisioning state, power state and Wi-Fi lease validity
under one snapshot lock. Its 96-byte limit leaves stack space for BLE startup.
The worker logs its minimum free stack after radio startup and cleanup.

The attempt expires after five minutes. Credential failure, timeout and cancel
close provisioning before publishing the terminal state. Retry creates a new
manager session, QR proof and attempt ID. After the station gets an IP, the
Espressif manager keeps BLE available for the phone to read the final Wi-Fi
status. The manager then stops provisioning. Its auto-stop timer also closes
the session if the phone leaves without reading the result. The controller
waits for the manager's END event to publish the Wi-Fi handoff result and the
stopped BLE transport. A successful result and a valid, healthy Wi-Fi lease
allow the controller to deinitialize the manager, restore telemetry and publish
Online. A cleanup failure remains visible separately from the original failure
and blocks reuse. A power transition out of Active closes the attempt. Wake
requires another explicit pairing request.
Explicit Cancel recovery retains the failed attempt's original error through
Cancelling and confirmed Cancelled. A valid Cancelled snapshot may therefore
remain degraded while `cleanup_error=ESP_OK` confirms closure. Consumers gate
return navigation on the confirmed state and cleanup result; admission of the
next Start or Retry clears the prior attempt error. Genuine teardown failure
continues to publish Failed with its separate cleanup error and blocks reuse.

New UI consumers read `DeviceSnapshot.radios.pairing` or the same canonical
connectivity projection. No callback into the renderer is necessary. The
pairing snapshot contains no QR or authentication secret.

## Cody wake reconnect

Paired Cody normally retains its Wi-Fi/CCP owners in software standby. A wake
therefore does not require a new DHCP lease when the actual association survives.
Retained traffic is admitted from the valid canonical Wi-Fi state even during
EnteringStandby/Waking; display animation is not a network-readiness condition.

Cody retries a dropped usable link after about one second, then waits one
minute after each failed attempt. It reuses the station's configured profile.
If a station association goes a minute without an IPv4 lease, the owner checks
the interface and rejoins the same Wi-Fi when the lease is still missing. A
valid IP event cancels the pending retry.

After a completed wake, the Wi-Fi owner may shorten an already-pending
association retry to one second when the station is disconnected. Provisioning
and explicit stop keep their existing ownership. An earlier retry deadline
takes precedence over delayed failure events. Errors remain in the owner snapshot.

The same wake and a successful acquired-IP event prompt the existing CCP worker
to send its next FIND without the ordinary two-second wait. Prompts coalesce and
remain bound to the current attempt; explicit Pair/Cancel/Forget commands win.
Only a fresh authenticated READY proves PC presence. Nonce/endpoint checks,
COMMITTED admission and the eight-second presence expiry remain unchanged.
The PC responder already replies to FIND immediately; normal Cody standby does
not restart the PC broker or wait for its ten-second lease renewal.

These scheduling changes do not establish an end-to-end latency guarantee. A
real association/DHCP delay, lost datagram or failed PC broker can still delay
recovery. Correlate completed-wake, Wi-Fi Online and the new authenticated-peer
connection log before attributing a particular observed delay. Visual wake and
battery presentations have their own duration and do not block the network owner.

## Physical acceptance

Exercise first provisioning through the phone's final status, wrong password,
missing AP, cancellation, retry, timeout, telemetry handoff, standby/wake and
HTTP access over the LAN. The [active work](../../../TODO.md) tracks outstanding
device observations.
