---
name: cody-debug
description: Open live device diagnostics on a paired Cody in Release or Debug, and restore its normal screen through authenticated CCP.
---

Use `ccp_debug_screen` only for the diagnostic view requested by the user on the intended paired target. Obtain its identity from `ccp_connection_status`; an old pairing record is not current presence. Require a recent authenticated screen capability. The same device diagnostics are supported in Release and Debug; never ask the user to choose a build mode to open them. Do not pair again to work around an unavailable capability.

- `character` shows the native character with canonical snapshot diagnostics: CPU utilization for both cores, free RAM/PSRAM, FPS, IP and SD status/free space. Unmeasured or stale fields remain explicitly unavailable/stale. RAM/PSRAM values are free amounts, not used percentages.
- `normal` restores the retained normal screen. Views are session-only.

The overview's **Memory** action opens internal RAM/PSRAM heap details: used/free
capacity, largest free block, minimum free amount, free-block count and a
fragmentation index. Bars show capacity categories, not block addresses. This is
local touch navigation, not another MCP mode. Back returns to the overview first.

There is one MCP diagnostic entry: `character`, with an overview and a local Memory page. The facial-channel list was removed at the owner's request to save device space; internal animation channels remain. `normal` restores the retained screen and is not another diagnostic view. Keep `scroll` at zero. The ACK's `channel_count` remains the actual internal catalog size, not a screen count. Older firmware that does not advertise the authenticated capability needs a firmware update; do not bypass capability or pairing checks.

Use `include_simulator: true` only for an explicitly requested simulator and label results SIMULATED. These commands cannot execute shell commands, override channels or write settings.

Report `applied` only from an authenticated worker ACK; report `rejected` with the actual error. A timeout means `unknown`: never infer success or automatically repeat the command. `pixels_verified: false` means an ACK does not attest visible pixels. Checks, captures, hardware operations and simulator execution require separate authorization.
