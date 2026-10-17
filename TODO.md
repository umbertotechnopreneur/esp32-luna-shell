# Active work

## Authorized Cody delivery - 2026-10-17

The owner requested the Debug footprint, two dependency locks and PDF fixes,
all tests/compilations, checkpoint commits, push and USB flash. Work remains on
the existing codex/cody checkout; no branch/worktree/PR was created.

- [x] **BUILD-DEBUG-OTA-01:** Lossless nonzero-alpha sprite rectangles save
  302,904 flash bytes without changing logical pixels, raw planes or PNGs.
  Debug passes at 7,081,968 bytes with 192,528 bytes free in the existing OTA slot.
  The original compiler policy, diagnostics, task budgets and storage geometry
  are retained. Native logical-pixel/render parity and both compiled-frame
  checks pass; nested physical stack headroom remains separate.
- [x] **PROFILE-LOCKS-01:** display-touch and full-wake-word canonical locks
  are generated with IDF 6.0.2. Catalog checks and both Release builds/size
  reports pass. Cody dependency identities and versions are unchanged.
- [x] **CODY-DOC-TOPOLOGY-01:** All four document contracts pass with rendering.
  The CCP guide reflects shared diagnostics; the quickstart retains exactly
  four A5 instructional pages. Both revised PDFs match their Obsidian copies.
- [x] Four selected firmware builds and size reports, two native preparations
  and all 48 automatic check invocations pass. The initial full-run PDF failure
  and final targeted pass are retained separately; see done.md for scope.
- [ ] Complete publication checks, push and flash the identified USB device.

planned-todo.md now excludes completed references, Windows capture and the stale
power/activity automatic-check gate. It distinguishes implemented behavior,
remaining source work and missing physical evidence. Earlier context remains in
Git b4738cbf; current evidence is in [done.md](done.md).

## Retained limits and deferred work

Physical pixels, touch, acoustics, measured wake latency, current, endurance and
nested target stack headroom are separate from host tests, builds and flash.
The authored shorter wake timeline is not a measured device timing result.
Voice/walking/classifier trials and speech-to-Codex follow-up remain deferred;
WAV delivery does not establish model-side speech understanding. Keep wake word,
the accepted SD/PCB limitation and observational HAR without model training or
automatic classifier-led shutdown. Broader work remains in
[planned-todo.md](planned-todo.md).
