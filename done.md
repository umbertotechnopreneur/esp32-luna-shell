# Latest validation round

## 2026-11-15 · Cody Studio timeline and observed routes on Linux

The native host build succeeds with production C/C++ renderers and host owners:
`pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'`.
Warnings remain visible; none were globally suppressed.

The focused phase-E suite passes **22/22**:
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--phase-e'`.
Two independent native processes reproduce S01/S02 source-animation samples
in forward and backward order and execute P01 Settings → P04 Display → P01 via
owner-dispatched touch. Captures validate scenario, source sample and atomic
route/frame identity. Checks cover logical orientation 180°, invalid origins,
receipt and timeline tampering, ABI output buffer capacities and an external
contact already pending. The specific interleaving during power admission has
static review but is not claimed as observed runtime evidence.

The ten-case pilot suite passes **23/23**:
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--pilot'`.
The real Tk/native suite passes **22/22**:
`xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--ui'`.
The window checks play, pause, step, speed, backward keyframe seek, route
selection and both real gestures, along with the prior pilot, comparison,
review, focus, resize and baseline checks. Timeline and observed-route
screenshots were visually inspected. The public `snapshot-integrity` and
`cody-runtime` checks pass; `git diff --check` passes.

The timeline samples compiled S01/S02 native source frames with a fresh owner
preparation per sample. The monotonic worker clock remains frozen; this does
not establish asynchronous worker, timer, audio or startup-lifecycle replay.
Only the two Settings/Display gestures have observed route evidence. Other
routes, general controlled-worker replay, remaining inventory mappings,
automated atlas/PDF work, Windows and physical acceptance remain open under
`CODY-NEXT-06`. No physical device was contacted or firmware flashed. Generated
artifacts are retained; cleanup remains manual.
