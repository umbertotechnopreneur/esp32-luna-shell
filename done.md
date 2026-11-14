# Latest validation round

## 2026-11-15 · Cody Studio ten-case Linux host acceptance

The authorized native build passes:
`pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'`.
It compiles production C/C++ renderers and host adapters; compiler warnings
remain visible and none were globally suppressed.

The pilot suite passes **18/18**:
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--pilot'`.
It includes 16 strict catalog/scenario/capture/comparison/provenance contracts,
two fresh-process captures of each of the ten inventory cases, and an isolated
shared-session sequence with forward/reverse preparation and error/recovery.
Every repeated case has identical RGB pixels and matching same-frame target,
scenario revision, mode and sample time. The shared sequence also covers C04
scope replacement, repeated battery 80/20 edits, rejected inputs/build changes,
unchanged baseline bytes and ABI-capacity sentinels around both output buffers.

The real Tk/native suite passes **17/17**:
`xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--ui'`.
It opens all ten destinations directly, edits/restores C04 call records,
synchronizes saved-capture history, and checks scenario/review persistence,
baseline preservation, comparison, focus/Tab scrolling, expander restoration
and proportional geometry at 1440x960 and 720x540. A separate automatic CLI
worker leaves the interactive frame, receipt, sensor snapshot, NVS, scenario
and saved artifacts intact. Window screenshots and all ten native captures
were visually inspected; the suite retains a labelled contact sheet.

The public `snapshot-integrity`, `build-profiles`, `script-contracts` and
`cody-runtime` checks pass. Script contracts cover six scripts and 34 actions.
`git diff --check` passes. Generated artifacts are retained; cleanup is manual.

Acquired host coverage is **ten static cases**: S01, S02, S17, P01, P04, P12,
W02, W04 and S15 use the full native runtime; C04 uses the production browser
presenter with a typed synthetic query owner. This does not establish domain
transport/reducer or general pagination acceptance. S01/S02 inspect explicit
source-animation samples; S15 retains the scripted transient timeout cause.
General timeline/worker replay, observed IN/OUT routes, automatic atlas/PDF,
remaining inventory mappings, Windows and physical acceptance remain open
under `CODY-NEXT-06`. No physical device was contacted or firmware flashed.
