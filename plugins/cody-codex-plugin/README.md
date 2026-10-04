# Cody's Codex Plugin (CCP)

CCP is a local Codex plugin for discovering and pairing a physical Cody device
on the same LAN. Cody shows a six-character code; the user supplies that code
to Codex, and the plugin matches the correct device even when many devices or
PCs share the network.

The plugin is intentionally self-contained and uses Python's standard library.
It does not connect the device to the raw Codex App Server and does not expose
Codex credentials on the LAN.

## Local Codex installation

The folder is a complete local plugin package with both the portable
`plugin.json`/`mcp.json` layout and the supported Codex compatibility layout.
Add it to a local marketplace; the bundled STDIO executable command is:

```text
python ./mcp/ccp_server.py --stdio
```

Codex must run the plugin on the PC whose identity Cody should retain. Windows
protects stored link keys with the current user's DPAPI account boundary. The
runtime file is kept outside the repository under
`%LOCALAPPDATA%\CodyCodexPlugin\state.json`.

Windows Firewall must allow private-network UDP receive/send for Python on
ports 42117 and 42118. Do not enable public-network access.

## MCP tools

- `ccp_discover_devices`: broadcasts one bounded discovery request and returns
  current pairing offers.
- `ccp_pair_device`: matches the code shown on Cody, confirms the device proof,
  and stores the resulting link identity.
- `ccp_connection_status`: reports stored identities and authenticated liveness
  observed by the running plugin.

The firmware/session protocol is documented in `protocol/CCP1.md`.
