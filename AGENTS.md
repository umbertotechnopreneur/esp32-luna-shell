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

## Code clarity

- Cody text is English, bounded printable ASCII; no Unicode/localization layer.
- Use readable steps, descriptive names and helpers with one clear responsibility;
  no dense branches, unrelated one-line statements or speculative abstractions.
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

- Keep firmware/simulator build output, generated build state, logs, captures,
  reports, temporary files and local simulator environments under `artifacts/`,
  not root build trees.
- Before every authorized firmware build/flash, including combo/recover, use the
  shared transient-artifact cleanup helper. Preserve active firmware, locks,
  generated configuration and Python caches. `fullclean` affects only the selected
  build; never delete versioned assets as runtime cleanup.
- Make focused local checkpoint commits at coherent implementation boundaries;
  preserve unrelated work. Push needs separate user authorization.
- `TODO.md`: active open work. `planned-todo.md`: deferred work; move an item to
  TODO when activated. `done.md`: compact completed implementation/evidence,
  without claiming pending validation. Keep recurring lessons in `lessons.md`
  and read them before boot/startup/Brookesia changes.
