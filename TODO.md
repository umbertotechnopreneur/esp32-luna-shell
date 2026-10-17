# Active work

## Authorized Cody delivery - 2026-10-17

The owner requested fixes for BUILD-DEBUG-OTA-01, PROFILE-LOCKS-01 and
CODY-DOC-TOPOLOGY-01, all tests/compilations, checkpoint commits, push and USB flash.
The existing codex/cody checkout is retained; no branch/worktree/PR is requested.

- [ ] **BUILD-DEBUG-OTA-01:** Minimal nonzero-alpha sprite rectangles save
  302,904 flash bytes against the previous package with unchanged full raw planes
  and PNGs. Both modes retain the original compiler policy, diagnostics, task
  budgets and OTA/storage geometry. Final native parity, partition and size
  verification are pending. The first general-code size optimization still
  exceeded Debug's slot by 73,232 bytes and was removed in favor of this lossless
  resource change; no tests were run against an oversized image.
- [ ] **PROFILE-LOCKS-01:** display-touch and full-wake-word canonical locks
  are generated with IDF 6.0.2. New-graph Kconfig dependency rules and the existing
  pinned-LVGL patch application are corrected. Cody dependency identities and
  versions are unchanged. Final catalog checks and Release builds remain pending.
- [ ] **CODY-DOC-TOPOLOGY-01:** The checker counts the cover/contents separately
  from retained body pages and verifies real register/Security bookmarks.
  Canonical PDFs are unchanged. Full checks and rendered review remain pending.
- [ ] Run the four selected builds at eight jobs, size reports, native preparations
  and all 48 automatic check invocations; record real failures before delivery.
- [ ] Finish publication checks, commit/push and flash the identified USB device.

The owner also requested reconciliation of planned-todo.md. Completed references
and Windows capture were removed from that pending register, obsolete controls
were replaced with the current diagnostics path, and older voice transport
history moved to done.md. Remaining physical evidence and unimplemented work
are labelled separately. Historical TODO context remains in Git b4738cbf.

## Retained limits and deferred work

Physical pixels, touch, acoustics, measured wake latency, current, endurance and
nested target stack headroom are separate from host tests, builds and flash.
The authored shorter wake timeline is not a measured device timing result.
Voice/walking/classifier trials and speech-to-Codex follow-up remain deferred;
WAV delivery does not establish model-side speech understanding. Keep wake word,
the accepted SD/PCB limitation and observational HAR without model training or
automatic classifier-led shutdown. Broader source/board/product work remains in
[planned-todo.md](planned-todo.md); completed dated evidence is in [done.md](done.md).
