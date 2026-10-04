# Shared Wi-Fi / BLE pairing lifecycle

Requested by Umberto Giacobbi, 3 October 2026. Implement shared stack changes
in the Luna worktree first, then carry the commits to Cody's HTTP/Wi-Fi branch.

The Wi-Fi acquisition owner adopts the manager's confirmed credential event
before its delayed connect runs. It accepts the first real IP event while
leaving credential persistence and provisioning retries to the stock manager.
It suppresses its own reconnect timer during that ownership interval. Direct
Wi-Fi connect, scan and stop calls are rejected while provisioning owns them.

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
idle and observes an active attempt every 250 ms through the canonical small
`ConnectivityObservation`, without a second sensor/network read path.

The attempt expires after five minutes. Credential failure, timeout and cancel
close provisioning before publishing the terminal state. Retry creates a new
manager session, QR proof and attempt ID. Online requires manager success plus
a valid, healthy Wi-Fi snapshot with association and a real nonzero IP; the
provisioning manager is released before telemetry starts. A cleanup failure
remains visible separately from the original failure and blocks reuse. A power
transition out of Active closes the attempt; wake never silently reopens it.
Explicit Cancel recovery retains the failed attempt's original error through
Cancelling and confirmed Cancelled. A valid Cancelled snapshot may therefore
remain degraded while `cleanup_error=ESP_OK` confirms closure. Consumers gate
return navigation on the confirmed state and cleanup result; admission of the
next Start or Retry clears the prior attempt error. Genuine teardown failure
continues to publish Failed with its separate cleanup error and blocks reuse.

New UI consumers read `DeviceSnapshot.radios.pairing` or the same canonical
connectivity projection. No callback into the renderer is necessary. The
pairing snapshot contains no QR or authentication secret.

## Delivery boundary

The cleanup/admission separation and retained-error cancellation behavior have
received static source review only. These edits have not received configure,
build, size, automated-test, simulator or physical-device validation. Existing
observations of the previous firmware do not verify the new behavior. First
provisioning, wrong password, missing AP, cancellation, retry, timeout, telemetry
handoff, standby/wake and HTTP LAN behavior remain end-to-end validation gates.
