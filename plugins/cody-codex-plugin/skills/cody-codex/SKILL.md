---
name: cody-codex
description: Connect a physical Cody device or explicitly select the Codex chat it follows, using CCP on the local network. Use for Cody connection, activity or chat-association requests.
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

For following a chat after pairing:

- Read `ccp_connection_status` and `ccp_list_sessions`. Choose exact device and
  session identities from those results, following the user's selection. Do not
  infer identity from a project label or choose the first of several chats.
- If the current chat identity is known and the user asked Cody to follow this
  chat, that is selection authorization. Otherwise ask which observed chat.
- Call `ccp_select_session` with an explicit, short ASCII project display label.
  The result confirms local selection only; it does not confirm displayed pixels.
- An empty session list means no lifecycle events have registered with this
  runtime. Have the user review/trust CCP's hooks in Codex and send a new prompt.
  Never synthesize hook events or edit trust records to make the device react.
- `Stop` means response ready, not task success. Missing activity is unavailable;
  a retained pairing or live PC does not establish a currently working chat.
- `ccp_clear_session` stops following a chat without erasing the device pairing.
  Broker restart requires explicit selection again; ordinary LAN reconnect does not.
