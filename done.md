# Latest validation round

## 2026-11-16 · Cody Studio native atlas

The latest isolated native atlas captured **104 of 104 selected cases**: 73
full-runtime and 31 isolated presenters. The 130-entry source catalog also
contains eight unsupported and 18 not-applicable entries. Acquisitions map to
90 inventory-local IDs, eleven secondary family states and three source
identities without an inventory mapping.

Linux host validation through `scripts/device.ps1`:

- The native graphics/runtime build succeeded after correcting the Debug BLE
  caption and isolating the synthetic voice microphone projection.
- `--native --inspection --events-ui` passed **65/65**. Voice frames repeated
  and restored P04; two different previous synthetic microphone levels did
  not change the Listening frame, and the original observation was restored.
  Debug provisioning, 15 Codex event observations and P04 also repeated. Native
  Studio opened the event views. Touch and external service calls remain
  source-reviewed rather than independently observed.
- `--atlas-pdf` passed **52/52**, including native image pixels, inventory
  identities, preserved baselines and three handwriting lines on every page.
- The supported atlas acquired 104 cases without failures. The compact PDF in
  `docs/cody/cody-native-screen-atlas.pdf` has 35 A4 pages with three frames per
  page, short adjacent references and no text appendices. All pages were
  inspected visually; source screenshots remain synthetic host evidence.
- The earlier 98-case coverage check also compared independent native sessions
  in reverse order and observed 42 declared routes. That broader reverse-order
  check has not been repeated for all 104 cases.

Remaining work includes editable event fixtures, actual reply and Retry,
populated notification lists/paging, physical voice/audio and sleep/wake.
Lost32 remains unsupported because the dashboard can mask its scene. S06
character/title overlap and the reset failure wording remain open. No firmware
build or flash, physical-device operation, human audio acceptance or push was
performed; Windows and device acceptance remain separate. Generated artifacts
are retained; cleanup is manual.
