# Latest validation round

## 2026-11-15 · Cody Studio Python contracts

The explicitly authorized command
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio`
completed successfully: all 13 tests passed. The suite covers catalog identity,
strict scenarios, coherent synthetic observations, capture receipts and file
integrity, comparison compatibility and pixel differences, baseline preservation,
review persistence and stale source/library rejection using isolated fixtures.

This suite did not load a native library, build or launch the simulator, acquire
a native P04 frame or contact a physical device. Native build, two-process
repeatability, interactive visual acceptance and hardware acceptance remain
pending under `CODY-NEXT-06`. Contract-test images are not native coverage.
Generated artifacts are retained; cleanup remains manual.
