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
| 06, Pair Cody | Submit START or a clean terminal-attempt RETRY using the current attempt ID | Immediately enter 07; await the real lifecycle and QR there |
| 06, Previous swipe | No active session | Return to 05; no lateral Back target |
| 07, Cancel | Submit CANCEL | Return to 06 only after Cancelled |
| 08, Retry | Submit RETRY using failed attempt ID after successful cleanup | Immediately enter 07; await its new ReadyToScan and session QR |

05 uses a Show QR footer; 06 shows the app installation QR and a Pair Cody
footer, with its heading hidden on the white page. Neither draws or hit-tests
lateral arrows. The live 06 cloud uses scale 58 versus 05's final scale 65.
07 owns the entire starting/scan/connect flow: it is entered immediately on
Pair Cody, then types "Scan to pair" and displays a real device/session QR only
after that current attempt reaches ReadyToScan and encoding succeeds. There is
no 06-bis route. The installation viewport is 240x240 at (113,140); the session
viewport inside 07 is 204x204 at (131,182), both with integer modules and the
four-module quiet zone.
The inherited Android app download URL is
`https://play.google.com/store/apps/details?id=com.espressif.provble`;
it is installation content, never a device/session credential.

The captioned footer is a reusable CODY2D capsule with shared draw/touch bounds.
The backend action identity is independent of button color or position. Admission
can replace the presentation immediately with a pending state; it never invents
a session QR, failure or successful connection.

## States and QR ownership

| Pairing state | Presentation / action availability |
| --- | --- |
| Idle | Installation/invitation flow; Start is available |
| Starting | 07 pending BLE startup; Cancel may queue during startup |
| ReadyToScan | Remain in 07; display the exact current session QR |
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

## Presentation clocks and standby

The invitation/installation/pairing scenes author a runtime
`standby_override_seconds=60`; zero on other scenes inherits the configured
inactivity timeout. This is a power-owner scene policy, not an NVS preference
write, pairing keepalive or extension of the backend's five-minute deadline.
Leaving/parking the scene clears the override and restores the saved policy.

Software standby retains the route, pending operation and active text/entrance
clocks in RAM. Wake completes the normal visual wake phases and reads the
canonical pairing/Wi-Fi observation before exposing the retained scene. If the
power owner closed that provisioning session, reconcile its current Idle,
Cancelled or terminal result rather than redisplaying the previous QR or
restarting first setup. Cold boot/reset keeps the normal startup policy; deep
sleep resets RAM and has no persisted route-resume contract.

All scene title/body/footer and look-selector text uses captured typed prefixes
from a separate active text clock. An optional original 20-ms keyboard-like tap
follows newly displayed characters after frame retirement. The audio owner
suppresses it for quiet hours, zero volume or standby. Unavailable trusted time
suppresses it only when both routines and quiet-hours policy are enabled;
disabling either permits it without that clock. Profiles excluding audio
or the speaker skip cue preparation and retain visual startup. The owner
preserves codec volume/shared clock and drops late or busy taps. The
renderer performs no codec I/O. The ten archive SFX previews stay private in
`artifacts/audio/cody-gentle-sfx/`; they are not integrated into firmware and
their source redistribution rights remain unconfirmed.

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

The current footer/07 flow, standby retention/override and teletype microclick
increment has source review and asset preparation only. Earlier validation
records do not establish this revision's behavior; no new test, build, flash,
hardware access or perceptual audio review was run for the increment.

Source review covered startup admission, command identities, event ordering,
single connection/retry ownership, cancellation, timeout, QR copying, snapshot
publication, profile closure and HTTP configuration. No configure, build, size,
tests, simulator checks, validation scans, flash or hardware access was run.
The integrated UI, Android app scan, first credentials, wrong password, missing
AP, repeated Retry, Back/Cancel during startup/connect, DHCP, BLE handoff,
standby/wake, reboot persistence and actual HTTP LAN behavior remain unverified.
