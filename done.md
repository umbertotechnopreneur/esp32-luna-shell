# Latest validation round

## 2026-11-15 · Cody Studio contracts and native host build

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

No simulator was launched, no native P04 frame was acquired and no physical
device was contacted. Two-process repeatability, interactive visual acceptance
and hardware acceptance remain pending under `CODY-NEXT-06`.
Contract-test images are not native coverage.
Generated artifacts are retained; cleanup remains manual.
