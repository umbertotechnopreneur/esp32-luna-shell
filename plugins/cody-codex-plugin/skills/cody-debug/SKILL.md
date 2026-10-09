---
name: cody-debug
description: Request the live face-channel list or character diagnostics on a paired Cody Debug device, and restore its normal screen through authenticated CCP.
---

Use `ccp_debug_screen` only for the debug view requested by the user on the intended paired target. Obtain its identity from `ccp_connection_status`; an old pairing record is not current presence. Require a recent authenticated Debug capability. Do not pair again to work around an unavailable capability.

- `face_channels` shows actual composed channels in a scrollable list. The ACK returns the real `channel_count`; never assume a fixed count. `scroll` selects its absolute first row. Touch can scroll locally.
- `character` shows the native character with canonical snapshot diagnostics underneath, including actual IP or explicit unavailable/stale state.
- `normal` restores the retained normal screen. Views are session-only.

Use `include_simulator: true` only for an explicitly requested simulator and label results SIMULATED. Release firmware does not offer debug capability. These commands cannot execute shell commands, override channels or write settings.

Report `applied` only from an authenticated worker ACK; report `rejected` with the actual error. A timeout means `unknown`: never infer success or automatically repeat the command. `pixels_verified: false` means an ACK does not attest visible pixels. Checks, captures, hardware operations and simulator execution require separate authorization.
