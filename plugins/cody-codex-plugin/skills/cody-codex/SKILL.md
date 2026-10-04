---
name: cody-codex
description: Discover, pair, or inspect a physical Cody device through Cody's Codex Plugin on the local network. Use only for explicit Cody device connection requests.
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
