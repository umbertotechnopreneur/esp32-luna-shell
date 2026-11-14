# Latest validation round

## 2026-11-15 · Cody Studio P04 host acceptance

The explicitly authorized command
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio`
completed successfully: all 13 tests passed. The suite covers catalog identity,
strict scenarios, coherent synthetic observations, capture receipts and file
integrity, comparison compatibility and pixel differences, baseline preservation,
review persistence and stale source/library rejection using isolated fixtures.

The separately authorized command
`pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'`
successfully compiled the Linux native device library from production C/C++
and host adapters. Compiler warnings remain visible; none were globally suppressed.

The native Studio suite passes 14/14:
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--native'`.
Two fresh processes produced identical P04 RGB pixels with matching effective
destination, scenario revision, capture time and native completed-frame identity.

The real Tk/native suite passes 14/14 under Xvfb:
`xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--ui'`.
It covers bounded input edits and A->B->C->A->B replay, scenario save/load,
unchanged baseline bytes, persistent capture reviews, invalid input retention,
keyboard focus, expander/column restoration and visible controls/proportional
frames at 1440x960 and 720x540. Wide, comparison and small screenshots were
visually inspected. Closing/reopening leaves no pending poll callback.

The public `snapshot-integrity`, `build-profiles`, `script-contracts` and
`cody-runtime` checks pass. Script contracts cover six scripts and 34 actions.
Four negative Studio CLI cases reject legacy flag mixing, missing scenarios
and unknown identities before rendering. `git diff --check` passes.

These checks include repaired portable validator paths and fixture setup that
matches the current owners; production permission/lifecycle behavior was not
changed to satisfy a test. Acquired coverage is one static P04 family only.
The ten-case pilot, remaining inventory mappings, animation/routes, automatic
atlas/PDF work, Windows acceptance and physical-device acceptance remain open
under `CODY-NEXT-06`. No physical device was contacted or firmware flashed.
Generated artifacts are retained; cleanup remains manual.
