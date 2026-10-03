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

`LUNASHELL_PROVISIONING_START_ON_BOOT` keeps the existing automatic behavior
for connected Luna profiles and defaults off for Cody. Cody retains the
controller's contiguous DMA reservation while it awaits an explicit pairing
action. Saved Wi-Fi profiles still use the normal station connection path.

Each provisioning attempt rotates a random 64-bit proof of possession for
the existing Security 1 transport. The device name uses three MAC suffix bytes.
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

## Delivery boundary

Implementation has received source review only. No configure, build, size,
tests, simulator checks or hardware operation was requested or performed.
First provisioning, wrong password, missing AP, cancellation, retry, timeout,
telemetry handoff, standby/wake and HTTP LAN behavior remain unverified.
