# CCP - Cody's Codex Plugin

CCP is a local Codex plugin for discovering and pairing a physical Cody device
on the same LAN. Cody shows a six-character code; the user supplies that code
to Codex, and the plugin matches the correct device even when many devices or
PCs share the network.

The implementation follows the repository's
[CCP Service Atlas](../../docs/cody/cody-ccp-service-atlas-v1.pdf): Codex calls
local MCP tools over STDIO; the Python service talks to Cody using CCP1 on the
LAN. CCP is the product name. The existing `cody-codex-plugin` package identity,
`ccp_*` tools and protected-state path remain stable.

The plugin is self-contained and uses Python's standard library on Windows.
It does not connect the device to the raw Codex App Server and does not expose
Codex credentials on the LAN.

## Local Codex installation

The repository now supplies `.agents/plugins/marketplace.json` with an available
entry for this folder. The folder contains both the portable `plugin.json` /
`mcp.json` layout and the supported `.codex-plugin/plugin.json` layout. Both MCP
definitions resolve the installed plugin root, rather than the current project
directory, and launch Python with `-B` to avoid runtime cache writes in the source
or installed package.

To install on the selected Windows PC:

1. Make Python available as `python` to the desktop app's environment.
2. Restart the desktop app, open the Plugins Directory and select the
   **Cody local plugins** source. Install **CCP - Cody's Codex Plugin**.
3. Open a new chat in Codex with CCP enabled. Ask it to discover Cody, then
   supply the code currently shown on physical screen 12 to pair the device.
4. Use the connection-status tool for authenticated presence after pairing.

Cody now offers **Connect Codex?** on startup when local Wi-Fi/IP is ready but
the device has no retained pairing. Choose **Yes** to display screen 12's code;
**No** returns to Idle without saving a refusal or starting CCP. To reopen setup,
hold the visible character still for 1.5 seconds and choose **Connect Codex** or
**Network setup**. PWR/BOOT restart gestures preserve the saved pairing; they do
not erase or reset it. An already paired Cody searches for its saved PC at boot
and shows screen 15 only after an authenticated READY response.

This is the repository-local marketplace workflow described in
[official plugin packaging documentation](https://developers.openai.com/plugins/build/plugins).
If the source needs explicit registration, `codex plugin marketplace add .`
from the repository root registers that local catalog. Registration and
installation are user operations; preparing this catalog does not perform them.

Codex launches the MCP entrypoint from the installed plugin root:

```text
python -B <installed-plugin-root>/mcp/ccp_server.py --stdio
```

Codex must run the plugin on the PC whose identity Cody should retain. Windows
protects stored link keys with the current user's DPAPI account boundary. The
runtime file is kept outside the repository under
`%LOCALAPPDATA%\CodyCodexPlugin\state.json`.

One CCP process owns the fixed listener on a PC. It acquires that listener
before loading or creating the protected state. A second local process fails
visibly before touching that identity. On a LAN with many Codex PCs, every
PC keeps a different random ID; Cody's retained PC ID and link key ensure that
only the paired instance can answer its authenticated liveness request.

Windows Firewall must allow private-network UDP receive/send for Python on
ports 42117 and 42118. Do not enable public-network access.

## MCP tools

- `ccp_discover_devices`: broadcasts one bounded discovery request and returns
  current pairing offers.
- `ccp_pair_device`: matches the code shown on Cody and completes the two-sided
  persisted-link confirmation before reporting success.
- `ccp_connection_status`: reports stored identities and authenticated liveness
  observed by the running plugin. A saved pairing starts disconnected until a
  verified FIND/READY exchange; presence expires after ten monotonic seconds.
  System-clock corrections cannot keep a dead connection alive. A stopped
  listener produces a structured tool error instead of a successful status.

The firmware/session protocol is documented in `protocol/CCP1.md`.

CCP1 commissioning and liveness do not select a Codex chat or transport project,
task or user messages. Screen 15 means that the paired CCP instance replied;
those additional capabilities remain separate work.

## Verification boundary

The prepared host regression suite is invoked only through:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
```

It covers authenticated presence and the required two-sided pairing completion
using synthetic clocks/keys, temporary files below `artifacts/` and mocked
listeners. It does not open LAN sockets, call Codex, access the real protected
state or contact a device. It has not been executed for this revision.
Real plugin installation, lifetime across chats, private firewall behavior,
multi-adapter broadcast, display-code pairing and reconnection still require
explicitly authorized end-to-end observation.
# CCP 0.3 chat activity milestone

The MCP entrypoint now attaches to a single hidden, on-demand Windows broker.
The broker owns UDP 42118; MCP transports renew independent leases on the
DPAPI-authenticated loopback control port 42119. Closing one chat does not stop
the others. The broker exits after 60 seconds without a live transport or hook.
The existing protected pairing file and PC identity are preserved.

Review and trust the bundled `hooks/hooks.json` in Codex before using activity.
Install/enable alone does not trust hooks. A new prompt or session start registers
the chat; `ccp_list_sessions` lists observed identities, and
`ccp_select_session(device_id, session_id, project)` selects the user's exact chat.
`project` is an explicit 1-24 character ASCII display label. No automatic first-chat
selection, transcript reading, prompt storage or model-authored activity is used.
Selection is volatile and must be repeated after a broker restart.

Hooks send lifecycle metadata only. Working/editing/running observations expire
after 120 seconds without a new event, becoming unavailable rather than remaining
apparently current. A Stop event means response ready, never task success.
`ccp_clear_session` removes the selection while retaining pairing.

New firmware requests the authenticated activity capability in FIND; the broker
returns nonce-bound ACTIVITY snapshots alongside READY. Selection tool success
means accepted locally, not proof that the physical display has updated.
Native presentation and physical verification are tracked in the root TODO.
