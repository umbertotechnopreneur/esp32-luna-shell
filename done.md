# Latest validation round

## 2026-11-16 · Cody Studio maintenance inspection and native atlas

The shared source catalog contains **126 entries**: 83 executable, 26 unsupported
and 17 not applicable. The acquired cases map to **76 distinct document-local
inventory IDs plus seven secondary family states**. The original PDF's 100 local
IDs remain a separate verified documentary denominator. Of the 83 cases, 71 use
full runtime and 12 use isolated production presenters.

This increment adds P19 restart pending, P21 reset pending and the P21 family
reset failure. The three samples use the native C++ maintenance presenter, with
isolated static state and no admitted restart or reset operation. The failure
uses a synthetic ESP_ERR_TIMEOUT (0x107). The original firmware caption says
"Restart did not occur" under "Reset failed"; its wording requires review. Back
and Retry shown by the failure presenter are disabled in this fixture. Navigation
and physical operation are not established by these captures.

Host validation through `scripts/device.ps1`:

- The native graphics/runtime generator compiled the current C++ sources.
- `--inspection` passed **52/52**, including six repeated maintenance captures,
  six P04 restorations, completed-frame identity and the unchanged controlled
  monotonic clock. The touch and power-call guards were reviewed in source; the
  host API does not independently observe those calls in this check.
- `--coverage --audio-cues --host-audio` passed **62/62**. All **83 cases**
  produced identical pixels and scenario identities in two independent processes,
  with reversed preparation order; **42 native routes** were observed. All
  **26 firmware cues** completed with PCM; volume, mute, quiet hours and
  cancellation were checked, and intro/Pet cues reached the PC audio backend.
- The three new native frames were visually inspected. Full-catalog generation
  published **83 captured, 26 unsupported, 17 not applicable and zero failed**.
  Its PDF has **66 pages** with IDs, modes, sources, explicit absences and three
  blank handwriting lines per page. All 66 pages were visually inspected; no annotation films were generated.
- Scoped source/index whitespace checks passed. Generated files remain beneath
  `artifacts/`; source paths remain repository-relative. Concurrent local work,
  including the preference-owner implementation, is outside these checkpoints.

Published artifacts:

- `artifacts/simulator/studio/atlas/runs/7f5e8d59217b44309db10dd6fdda196a/manifest.json`
- `artifacts/simulator/studio/atlas/exports/cody-native-atlas-7f5e8d59217b44309db10dd6fdda196a-97cfa6d1.pdf`

Remaining work includes canonical event/question detail and replies, maintenance
lifecycles and real operations, voice and sleep/wake. S06's native character/title
overlap, Wi-Fi operation replay, unavailable microphones/heaps and isolated
Memory navigation retain explicit limits. Precise general worker/timer scheduling
stays deferred under the owner's graphics/audio priority. Windows, human listening
and physical acceptance remain open. No firmware build/flash, device contact or
Git push was performed. Artifacts are retained; cleanup is manual.
