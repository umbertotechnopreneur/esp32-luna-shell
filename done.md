# Latest validation round

## 2026-11-16 · Persistence, recovery and maintainability

Widget settings now preserve read/validation failures and refuse replacement
until the file is repaired and reloaded. Studio review writes are atomic;
capture-result preparation is separate from Tk, and recoverable load/image
failures retain the prior capture while polling continues. General artifact
cleanup preserves scenarios, baselines, notes and their referenced files.

Interactive Studio releases the build writer after preparation while retaining
its session and output reservations. Native builds publish from staging and
load verified immutable library copies. Storage formatting uses a bounded,
fallible snapshot projection; storage freshness is shared with speech and
diagnostics. The unbuilt companion runtime and obsolete manual atlas writer
were removed without changing the production renderers.

Linux host validation through `scripts/device.ps1`:

- Native graphics/runtime build succeeded.
- `check -Check cody-studio -ToolArguments @('--native','--ui')`: **48 tests
  passed**, including independent P04 captures, actual Tk editing, comparison,
  baseline preservation, saved notes, reopening, focus, resize and pilot views.
- `check -Check cody-studio-recovery`: **10 tests passed**, including failed
  capture/catalog/review reads, image allocation and atomic review writes.
- `check -Check cody-runtime-publication`: **12 mocked tests passed**, covering
  verified immutable loading, publication failures, races and MSVC output paths.
- `check -Check cody-widget-settings`: shared .NET Debug build succeeded with
  no warnings/errors; **166 checks passed**, including invalid settings, I/O
  errors, symlinks, retained bytes and explicit reload after repair.
- `check -Check device-console`: **19 checks passed**. The separate
  `device-concurrency` check passed with real child dispatchers and a fake
  simulator process: concurrent generation, conflicting outputs, cleanup
  rejection and release on exit.
- `check -Check cody-runtime -ToolArguments '--storage'`: the compiled production
  freshness helper passed boundary, reversed-clock and error-preservation cases.
- Project-owned changes passed `git diff --check`. The newly committed upstream
  vendor sources retain their existing whitespace warnings unchanged.

The owner authorized committing all remaining local source/documentation changes;
those pre-existing edits are not all covered by this focused validation. No full
firmware build, flash, physical operation, Windows UI/build, human audio acceptance
or push was performed. Storage lock failure and SD formatting still require target
acceptance. Wide Tk screenshots exceeded the virtual display, so they do not
establish complete visual acceptance of the inspector or preference warning.
The existing atlas PDF was not regenerated in this round.

Generated artifacts remain under `artifacts/`; cleanup is manual. Immutable
runtime copies are deduplicated but retained; remove unused generations only
after closing simulator processes. Remaining work stays in `TODO.md` and the
existing `CLARITY` entries in `planned-todo.md`.
