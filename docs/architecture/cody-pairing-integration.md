# Cody HTTP / Wi-Fi and pairing integration

Implementation started at Umberto Giacobbi's request on 3 October 2026;
checkpointed on 4 October 2026 in `feat/http-and-wifi`. The shared stack was
implemented in the Luna worktree first and carried here through focused commits.
The original backend increment did not change scenes or welcome assets. The
subsequent [standalone OOBE presentation](cody-onboarding.md) now consumes this
boundary and implements the network routes through 09, following addendum A01.

## Boot and public boundary

Cody's catalog layers `features/cody-connectivity.defaults` after the original
rig profile. It admits the SDK `esp_http_server` component and compiles the
existing read-only HTTP API. Its persisted default remains disabled. No LVGL,
Brookesia GUI or legacy Phone dependency is added to the standalone Cody graph.
The existing managed-component lock is retained: no managed version changed.
Dependency resolution and lock verification have not run.

Boot still initializes Wi-Fi for a saved profile. Without a profile, BLE awaits
Pair Cody rather than advertising an unsolicited provisioning session. A
controller DMA reservation remains held while waiting. The async pairing worker
is initialized after radio startup and never opens a session during init.

Use only `LUNASHELL_platform.h` and `LUNASHELL_device_snapshot.hpp` for OOBE.
Read `DeviceSnapshot.radios.pairing`, or call `readConnectivityObservation` for
the same smaller canonical projection. The request is:

```cpp
LUNASHELL_pairing_request(
    LUNASHELL_PAIRING_ACTION_START,
    observation.radios.pairing.attempt_id
);
```

An `ESP_OK` return means the action was admitted. Rendering waits for the
published state and must not call radio initialization, provisioning manager
APIs, SDK Wi-Fi queries or synchronous cleanup itself. Keep the attempt ID in
the action's route context so a delayed event from an older screen is rejected.

## Existing atlas routes

Original printed atlas pages and IDs remain valid together with addendum A01.

| Route/action | Backend contract | Navigation result |
| --- | --- | --- |
| 05, Show QR | No pairing action | Open 06, installation QR |
| 06, Pair Cody | Submit START using current attempt ID | Await ReadyToScan, then open 06-bis |
| 06, Back | No active session | Return to 05 |
| 06-bis, Back | Submit CANCEL | Return to 06 only after Cancelled |
| 07, Cancel | Submit CANCEL | Return to 06 only after Cancelled |
| 08, Retry | Submit RETRY using failed attempt ID | Await new ReadyToScan, then open 06-bis |

06 shows the app installation QR and the Pair Cody action. Remove the visible
"Scan to install" text from that screen, as requested. 06-bis shows the title
"Scan to pair" and a real device/session QR scanned inside the provisioning app.
The inherited Android app download URL is
`https://play.google.com/store/apps/details?id=com.espressif.provble`;
it is installation content, never a device/session credential.

The blue action capsule and semantic color variants belong to the OOBE agent's
reusable CODY2D component. Preserve the approved palette and circular navigation
rules. The backend action identity is independent of button color or position.

## States and QR ownership

| Pairing state | Presentation / action availability |
| --- | --- |
| Idle | Installation/invitation flow; Start is available |
| Starting | Pending BLE startup; Cancel may queue during startup |
| ReadyToScan | 06-bis; display the exact current session QR |
| Connecting | 07; credentials were received by the manager |
| Completing | 07; releasing provisioning and starting telemetry |
| Online | 09; manager succeeded and Wi-Fi has a healthy acquired IP |
| Cancelling | Keep the current route pending until cleanup is confirmed |
| Cancelled | Session closed; the UI may return to 06 |
| Failed | 08; show the real error and credential failure reason |
| TimedOut | 08; five-minute deadline expired and cleanup completed |

In ReadyToScan, call `LUNASHELL_provisioning_copy_qr(buffer, capacity,
pairing.provisioning_session_id)` with a caller-owned buffer of at least 192
bytes. Generate a standalone QR from that copy. An expired/racing state returns
`ESP_ERR_INVALID_STATE`; reread the snapshot and follow its current state.
Do not reuse an older QR, call the LVGL `PairingQrAsset`, or write QR contents
to logs, snapshots, the HTTP API or files. Release/wipe the display's secret
buffer when it leaves the pairing route. The backend rotates the proof of
possession on each new attempt.

Always check `data_valid` before interpreting a lifecycle snapshot. A failed
lifecycle remains a valid observed state with Degraded health and a real
`last_error`; it is usable for displaying the failure. `cleanup_error` is
separate. Retry is available only when cleanup succeeded. Otherwise keep the
failure visible; an explicit Cancel can retry closure, and a poisoned manager
remains blocked rather than silently repaired. Do not infer Online from a
stored SSID, BLE connection, credential receipt or Start admission. Saved-profile
boot also needs actual valid Wi-Fi association and IP before an online route.
Online is the completed attempt's observed result. Continue checking the live
Wi-Fi snapshot on screen 09; a later disconnect must not leave an online label
based only on that earlier terminal pairing result.

## HTTP configuration

The API listens on the actual station IP at port 8080 when explicitly enabled.
It follows IP acquisition/loss. Pairing does not enable it implicitly. The
existing `LUNASHELL::httpServerSetApiKey` and `httpServerSetEnabled` operations
are the configuration boundary for a future local Settings flow. Call them
outside the renderer; persist a user-supplied key before enabling access.
Initialization persists `http_enabled=false` only for a genuinely absent key.
A missing ApiKey is unconfigured; an empty, corrupt or oversized saved key fails.

`GET /api/v1/health` is public; `/api/v1/capabilities`, `/api/v1/info`,
`/api/v1/diagnostics`, `/api/v1/rtc`, `/api/v1/gps` and `/api/v1/status` use Bearer auth.
Diagnostics expose provisioning and pairing states without QR secrets. The
transport is HTTP on the local network. This is the inherited diagnostics API;
PC companion sessions/MCP transport remain deferred work.

## Review and deferred evidence

Source review covered startup admission, command identities, event ordering,
single connection/retry ownership, cancellation, timeout, QR copying, snapshot
publication, profile closure and HTTP configuration. No configure, build, size,
tests, simulator checks, validation scans, flash or hardware access was run.
The integrated UI, Android app scan, first credentials, wrong password, missing
AP, repeated Retry, Back/Cancel during startup/connect, DHCP, BLE handoff,
standby/wake, reboot persistence and actual HTTP LAN behavior remain unverified.
