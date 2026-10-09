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

One background CCP broker owns the fixed listener on a PC. It acquires that
listener before loading or creating the protected pairing state. Multiple MCP
transports share that broker; a conflicting broker fails before touching the
pairing identity. On a LAN with many Codex PCs, every
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

Screen 15 means the paired CCP instance replied. The activity extension below
automatically follows the chat of the latest user prompt and its real lifecycle status. User messages, replies,
PR details, branch acquisition and transcription remain separate work.

## Verification boundary

The prepared host regression suite is invoked only through:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
```

It covers authenticated presence and the required two-sided pairing completion
using synthetic clocks/keys, temporary files below `artifacts/` and mocked
listeners. It does not open LAN sockets, call Codex, access the real protected
state or contact a device. All 22 earlier cases passed on 10 October 2026 for CCP 0.3.3.
The revised 0.4 suite covers automatic switching, background isolation, reordered
prompt delivery, bounded labels and removal of the manual selection tools;
its new results are tracked in the root done.md/TODO.md.
Local marketplace installation and manual MCP startup of 0.3.3 succeeded; the
existing pairing record was retained. Native plugin autoload, hook trust,
lifetime across chats, private firewall behavior, multi-adapter broadcast and
activity delivery/reconnection still require end-to-end observation. The earlier
0.2 pairing was physically confirmed; that does not establish 0.3 activity.
## CCP 0.4 automatic chat activity

The MCP entrypoint now attaches to a single hidden, on-demand Windows broker.
The broker owns UDP 42118; MCP transports renew independent leases on the
DPAPI-authenticated loopback control port 42119. Closing one chat does not stop
the others. The broker exits after 60 seconds without a live transport or hook.
The existing protected pairing file and PC identity are preserved.
Startup checks port ownership with an exclusive Winsock bind. A connection
timeout to a closed loopback port cannot establish absence; access failures
remain errors. The child re-acquires ownership before accessing protected state.

Review and trust the bundled `hooks/hooks.json` in Codex before using activity.
In a local Codex CLI session, `/hooks` opens that review. Local plugin command
hooks require local orchestration; Work Cloud does not execute them even when
some tools run on the PC. This adapter must receive genuine events before any
chat activity can be shown.
Install/enable alone does not trust hooks. `UserPromptSubmit` automatically makes
that exact chat the followed context for all Cody devices paired to this PC.
The newest prompt-hook emission wins; delayed older hooks cannot reverse it.
SessionStart, tools, completions and interruptions from other chats never switch
the followed context. `ccp_list_sessions` is read-only and exposes `followed`.
The former `ccp_select_session` and `ccp_clear_session` tools have been removed.

There is no chat picker on the device and no manual chat-selection prerequisite
in Codex. Screen 15 says **Connected / Waiting for Codex** until the first genuine
prompt. A broker restart waits for a new prompt automatically. Opening a tab
alone is not a documented hook signal; this feature follows submitted work,
not window focus. It does not read transcripts or retain prompt/response text.

The display uses the working directory's last component as a project label,
projected to printable ASCII (`?` for unsupported characters) and shortened with
`...` to at most 24 characters. A filesystem root is labelled `Codex`. Those
presentation decisions are logged. The full session digest remains the identity;
two chats in the same project remain distinct. Chat titles and branches are not
provided by this adapter.

Hooks send lifecycle metadata only. Working/editing/running observations expire
after 120 seconds without a new event, becoming unavailable rather than remaining
apparently current. A Stop event means response ready, never task success.
Disable the plugin to stop future hooks; this does not erase device pairing.

New firmware requests the authenticated activity capability in FIND; the broker
returns nonce-bound ACTIVITY snapshots alongside READY. Local hook acceptance
does not prove that the physical display has updated.
Native screens 17-20 and 32 consume the canonical activity snapshot. Ending a
session shows 20B for five active seconds and returns to 16. Holding the character
opens Setup; Back restores the saved-link journey and plugin-provided chat context.
Packets expire on Cody after eight seconds; ordinary LAN reconnect replays the
latest snapshot without repeating pairing. UI activity does not override the
user's standby policy. Physical verification is tracked in the root TODO.

The supported hook events and trust boundary are documented in the
[official Codex hooks reference](https://learn.chatgpt.com/docs/hooks).
