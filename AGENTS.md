# Repository engineering rules

These rules apply throughout ESP32 Luna Shell. This is the sole repository
instruction file; do not add parallel agent or Copilot contracts.

## Platform and tooling

- Target ESP-IDF 6.0.2 and official ESP-Brookesia 0.8 only. No compatibility
  branches, shims, wrappers, migrations or fallbacks for IDF 5.x, Brookesia
  0.6/0.7 or the removed Phone framework.
- Luna uses LVGL below the official Brookesia System Super/System Core and
  declarative GUI packages.
  Standalone display/touch and Cody use the native 2D path without LVGL or its
  adapter dependencies. Keep physical-device selection separate from UI selection.
- `scripts/device.ps1` is the sole public CLI/menu for firmware, simulator,
  generators and checks, including configure/build/size/flash/erase/capture/monitor.
  Never invoke the ESP-IDF Python entrypoint directly. Implementation belongs in
  `scripts/commands/`, helpers in `common/`, generators in `generators/` and
  checks in `checks/`.

## Authorization and validation

- Implementation does not authorize tests, validation scans, simulator execution,
  configure/build/size or physical hardware/serial/flash/erase/recover/monitor.
  Run only the operations explicitly requested. Static source review is allowed;
  disclose deferred validation.
- Before each individual test or suite, announce exact commands and wait for fresh
  explicit authorization. Earlier test approval or approval to implement, build,
  flash or capture does not authorize another test run.
- Finish implementation, static review and cross-component integration before
  requested final validation; no intermediate configure/build/size passes.
  Limit compilation to eight jobs and allow at least 240 seconds per build.
  After cancellation, confirm Ninja/Xtensa compiler processes have stopped before
  starting later validation. Fix compiler/linker causes; never globally suppress
  warnings to conceal them.
- An explicitly requested complete final package comprises clean/incremental
  builds, size, simulator checks, `git diff --check`, dependency/legacy scans and
  a changed-files secret scan. A narrower request authorizes only its named checks.
  Host assertions, successful builds and flashes do not prove physical behavior.

## Circular display

- Canvas: 466 x 466; center (233, 233), radius 233. Central safe square:
  320 x 320 at (73, 73), half-open bounds [73, 393), pixels 73 through 392.
  Rounded margins are usable for navigation/status/artwork, not uniform strips:
  the lateral width is 73 pixels only at the horizontal center.
- Back occupies the left lateral circle: bounds (10, 203), 60 x 60, radius 30,
  center (40, 233). Show only with a valid previous route. Use one centered
  left-chevron, no text; retain the full touch target. Keep it separate from
  central content and primary/footer actions, never beside or below a footer.
- Derive the circle's available chord at every occupied y. Keep artwork/control
  and hit bounds inside it, preserve QR quiet zones and avoid navigation overlap.
  The safe square is a baseline, not the only usable area.
- Rendering, clipping and touch share logical coordinates at 0/90/180/270 degrees.
  Back remains visually left in the chosen orientation; never mirror text or
  rotate touch independently. Read
  [circular-display-layout](docs/architecture/circular-display-layout.md) before
  changing layouts.
- Cody companion technical panels are a scoped exception to lateral Back:
  a short passive tap advances Activity, Model & Context, Session & Link, then
  Activity again; a deliberate shake returns directly to Home. These panels
  have no Back control. Setup, remote questions and Luna retain their routes.

## Code clarity

- Write repository documentation, code comments, user-facing text, log messages
  and commit messages in English.

- Cody action layout decision (2026-10-09): two actions use rounded gradient
  cards side by side; three use the same pair plus one centered action below.
  One action uses a centered wide capsule with its icon to the left of its label.
  Apply semantic colors, restrained gradients and action icons to all Cody
  screens. Preserve the separate lateral Back, QR quiet zones and local chords.
  Approved references: `docs/Cody/ui-proposals/20261009/selected-multi-action.png`
  and `selected-single-action.png` in the same directory.
  Button/card captions do not use teletype. Reveal complete controls together
  after the title and main body line finish; admit touch only while visible.
  Keep control labels out of typewriter timing and audio counts.

- Cody text is English, bounded printable ASCII; no Unicode/localization layer.
- Use readable steps, descriptive names and helpers with one clear responsibility;
  no dense branches, unrelated one-line statements or speculative abstractions.
- Treat large objects, arrays, by-value snapshots and temporary copies as explicit
  stack-budget decisions. Check their size against the owning task and the deepest
  nested call chain, including callbacks and logging. Prefer bounded projections
  or component-owned buffers with clear synchronization; use PSRAM only where the
  access and cache rules permit it. Never fix stack pressure by blindly enlarging
  a task stack. After schema growth, revisit affected callers; inspect compiled
  frames and measure device stack headroom before claiming runtime validation.
- Domain identities use enums and unique names/IDs in typed namespaces. Reject
  duplicates/ambiguous references and preserve identity across reordering.
  Raw indices are for storage traversal/API boundaries, not domain meaning.
- Above every new/modified function, add a separate comment line for each
  parameter and a separate line for applicable exceptions. Inside functions,
  explain non-obvious decisions, ownership and invariants without narrating
  obvious code or adding filler.

## Errors and canonical data

- Never turn a failure/partial failure into `ESP_OK`, discard an init/read/write/
  persistence/service error or guess at ambiguous recovery. Return the original
  `esp_err_t` or a more specific error. Log every rejected value and operation
  failure with component, operation and real cause; logs complement structured state.
- Startup-critical failure stops its stage. An orchestrator may continue only
  after a documented optional failure; that component still returns its error
  and publishes failure in `DeviceSnapshotService`.
- The canonical snapshot is the only sensor/runtime-data source for Cody, UI,
  animations and consumers. Only its acquisition owner reads sensors/BSP/I2C.
  No consumer-side reads, temporary diagnostics or freshness fallbacks; extend
  the owner explicitly for new fields/cadences.
- Publish measurements only after successful hardware reads, with original
  timestamps, validity/health and real errors. Failed/unavailable samples carry
  explicit invalid/unavailable state, real error and applicable timestamps.
  Never fabricate, interpolate, zero-fill or present stale data as current.
  Check validity before numeric UI;
  show `UNAVAILABLE`, `NO FIX`, `STALE` or the error. Unmeasured CPU/metrics
  remain unavailable. Label synthetic Windows simulator data visibly and never
  publish it into firmware snapshots.

## Settings and storage

- Default only a genuinely absent key: log key/default/first-boot decision, then
  persist. Read/type/CRC/storage failure is not absence. Never silently clamp,
  repair, migrate, overwrite or substitute invalid persisted values.
  Reset/repair requires explicit user action and a visible result.
- Internal LittleFS/NVS and removable SD have separate owners/purposes, not
  fallback relationships. Never implicitly format SD: require a present card,
  verified format-required state and one explicit single-use authorization.
  Card disappearance or inability to attempt immediately expires authorization.

## Artifacts and checkpoints

- For every user-requested PDF revision, append a final "Errata and corrections"
  page with the revision date, a concise list of changes and affected original
  page/screen IDs. Preserve earlier errata and unrelated pages; apply this rule
  to any PDF in scope, including documents stored outside the repository.
- Keep firmware/simulator build output, generated build state, logs, captures,
  reports, temporary files and local simulator environments under `artifacts/`,
  not root build trees.
- Before every authorized firmware build/flash, including combo/recover, use the
  shared transient-artifact cleanup helper. Preserve active firmware, locks,
  generated configuration and Python caches. `fullclean` affects only the selected
  build; never delete versioned assets as runtime cleanup.
- Make focused local checkpoint commits at coherent implementation boundaries;
  preserve unrelated work. Push needs separate user authorization.
- Keep one canonical root record for each purpose: `AGENTS.md` for repository
  rules, `TODO.md` for active work, `planned-todo.md` for deferred work,
  `done.md` for compact completed implementation/evidence and `lessons.md` for
  recurring lessons. Move content into these files instead of adding nested or
  alias `AGENTS.md`, `TASKS.md`, `TODO.md` or lessons files. Do not claim pending
  validation as done; read `lessons.md` before boot/startup/Brookesia changes.
