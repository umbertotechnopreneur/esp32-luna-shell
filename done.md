# Latest validation round

## 2026-11-15 · Cody Studio committed-source validation on Linux

All 364 ordinary inputs of the validated native build are now versioned and
match the committed source content. The three managed QR inputs remain the
official Espressif dependency `0.1.0~2`, with its component hash pinned in the
versioned lock. Unrelated desktop, experiment and physical-device work remains
outside these Studio commits.

A source-only `git archive` of `9bb250df` was extracted beneath
`artifacts/studio-commit-verification/9bb250df/`. Its only supplied source
prerequisite was the installed QR component matching that lock. No local
untracked Studio sources or prebuilt native library were copied into it.

The exported tree successfully built through
`pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'`.
It then passed **33/33** Studio checks through
`pwsh -NoProfile -Command "& ./scripts/device.ps1 check -Check cody-studio -ToolArguments @('--native','--live-audio','--guide')"`.
These cover repeated native P04 captures, scenario/receipt integrity,
comparison and navigation provenance, real firmware PCM with Sounds on/off,
and both exits from the guide QR page. The exported tree also passed
`pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-runtime`.
The staged changes pass `git diff --check`.

This verifies that the committed Studio sources rebuild and execute on Linux
with the documented external prerequisite. Windows, human speaker listening,
physical Cody acceptance and complete inventory coverage remain open. The next
implementation slices are phase F (isolated atlas generation and inventory-linked
PDF) and phase G (remaining families and measured coverage). Exact scheduling
of unrelated workers remains deferred in favor of graphics and audio fidelity.
No device was contacted or flashed. Artifacts are retained for manual cleanup.
