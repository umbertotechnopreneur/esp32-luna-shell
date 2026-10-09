---
name: cody-codex
description: Connect a physical Cody device and inspect the chat automatically followed by CCP on the local network. Use for Cody connection, activity or chat-association requests.
---

# Cody Codex connection

Use the CCP tools for the physical device named by the user.

- Call `ccp_discover_devices` before pairing unless the same live MCP process
  already returned a current offer.
- Pair only after the user supplies the six-character code visible on Cody's
  display. Never guess, repeat, retain, or place the code in explanatory prose.
- When more than one device is visible, let `ccp_pair_device` match the code;
  never choose the first network response.
- Treat tool admission as distinct from device confirmation. Report paired or
  connected only when the corresponding tool result says so.
- Use `ccp_connection_status` for status questions. Do not infer physical
  behavior from a stored identity or a previously successful call.
- Do not expose verifier, proof, link-key, salt, or protected local-state data.

For automatic chat activity after pairing:

- Trusted `UserPromptSubmit` hooks automatically associate the latest prompted
  chat with this PC's paired Cody devices. Do not ask the user to select a chat
  on Cody or call a manual selection tool. Background tool/Stop/SessionStart
  events do not change the followed chat.
- Read `ccp_connection_status` and `ccp_list_sessions` for observed metadata.
  The `followed` field identifies the chat; the workspace label is not identity.
  Tool status does not confirm displayed pixels. Do not infer focus from the
  foreground window, project name or transcript.
- An empty session list means no lifecycle events have registered with this
  runtime. Have the user review/trust CCP's hooks in Codex and send a new prompt.
  Never synthesize hook events or edit trust records to make the device react.
- Until a genuine prompt arrives, screen 15 shows Connected / Waiting for Codex.
  Merely opening another tab is not a documented hook event; do not claim that
  tab focus is tracked. The next user prompt establishes that chat automatically.
- `Stop` means response ready, not task success. Missing activity is unavailable;
  a retained pairing or live PC does not establish a currently working chat.
- A broker restart waits for the next genuine prompt; ordinary LAN reconnect
  retains the followed context. Disabling the plugin stops its future hooks.
