---
name: cody-debug
description: Request either live diagnostic screen on a paired Cody in Release or Debug, and restore its normal screen through authenticated CCP.
---

Use `ccp_debug_screen` only for the diagnostic view requested by the user on the intended paired target. Obtain its identity from `ccp_connection_status`; an old pairing record is not current presence. Require a recent authenticated screen capability. The same two views are supported in Release and Debug; never ask the user to choose a build mode to open them. Do not pair again to work around an unavailable capability.

- `face_channels` shows actual composed channels in a scrollable list. The ACK returns the real `channel_count`; never assume a fixed count. `scroll` selects its absolute first row. Touch can scroll locally.
- `character` shows the native character with canonical snapshot diagnostics underneath, including actual IP or explicit unavailable/stale state.
- `normal` restores the retained normal screen. Views are session-only.

There are two diagnostic screens: `face_channels` and `character`. `normal` restores the retained screen and is not a third diagnostic view. Older firmware that does not advertise the authenticated capability needs a firmware update; do not bypass capability or pairing checks.

Use `include_simulator: true` only for an explicitly requested simulator and label results SIMULATED. These commands cannot execute shell commands, override channels or write settings.

Report `applied` only from an authenticated worker ACK; report `rejected` with the actual error. A timeout means `unknown`: never infer success or automatically repeat the command. `pixels_verified: false` means an ACK does not attest visible pixels. Checks, captures, hardware operations and simulator execution require separate authorization.
