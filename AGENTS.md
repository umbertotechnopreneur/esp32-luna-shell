# Repository engineering rules

These rules apply to the entire ESP32 Luna Shell firmware repository.

## Supported platform

- Target ESP-IDF 6.0.2 and the official ESP-Brookesia 0.8 architecture only.
- Do not add compatibility branches, shims, wrappers, migrations, or fallback
  paths for ESP-IDF 5.x, Brookesia 0.6/0.7, or the removed Phone framework.
- The Luna UI uses LVGL only below Brookesia System Super, System Core, and its
  declarative GUI package. Physical display and touch also support a standalone path with
  that UI disabled; this path must not admit LVGL or its adapter dependencies.
- Run every ESP-IDF configure, build, size, flash, erase, capture, and monitor
  workflow through `scripts/device.ps1`. Never invoke the ESP-IDF Python
  entrypoint directly.

## Circular display geometry

- Use a 466 x 466 logical canvas, nominal center (233, 233), and radius 233.
  The canonical central safe square is 320 x 320 at (73, 73): both axes use
  half-open bounds [73, 393), corresponding to pixel indices 73 through 392.
- Keep the rounded regions outside that square available for navigation,
  status, and suitable artwork. At the horizontal center there are 73 pixels
  on each side of the square; this width narrows toward the top and bottom.
  Do not discard these regions or assume they are uniform rectangular strips.
- Place Back in the left lateral region at logical (10, 203), with a 60 x 60
  bounding box, circular radius 30, and center (40, 233). Keep it separate from
  the central content and primary action. Do not place Back beside a footer
  action or below it. Show it only when a valid previous route exists.
- Render Back as a single left-facing chevron icon, centered in that control.
  Do not display the word Back or another text label inside the control.
  Preserve its full touch target independently of the icon's smaller bounds.
- Derive the available horizontal chord from the circle at every occupied
  vertical position. Keep the full visible control and its hit target inside
  the display, preserve QR quiet zones, and prevent overlap with lateral
  navigation. The safe square is the baseline, not the only usable area.
- Rendering, clipping, and touch must share these logical coordinates through
  0/90/180/270 degree rotation. Back stays visually on the left in the chosen
  orientation; never mirror text or independently rotate the hit target.
- Read `docs/architecture/circular-display-layout.md` before changing layouts.
  Its geometry comes from the existing Luna UI; Cody layouts must use the
  standalone 2D renderer without admitting LVGL dependencies.

## Code clarity and identities

- Prefer readable steps and clear variable/method names over fewer lines.
  Do not compress branches or unrelated statements onto one line.
- Address domain controls and identities with enums and unique names/IDs.
  Raw indices belong only to internal storage traversal or API boundaries;
  never use list position as the meaning or identity of a domain element.
- Give authored entities unique IDs within explicit typed namespaces. Reject
  duplicates and ambiguous references; preserve names across reordering.
- Above each new or modified function, describe each parameter on its own
  comment line and applicable exceptions on a separate line. Add useful
  comments inside methods for non-obvious choices, ownership and invariants.
- Keep comments natural and occasionally informal where helpful. Explain the
  decision; do not narrate obvious code or add filler comments.
- Extract helpers around a clear responsibility. Avoid speculative abstraction
  and check that removing repetition also improves understanding.

## Fail-fast and error visibility

- Never convert a failure or partial failure into `ESP_OK`.
- Never discard an initialization, read, write, persistence, or service-start
  error. Propagate the original `esp_err_t` or a more specific error.
- Log every rejected value and failed operation with the component, operation,
  and real error. Logs complement structured state; they do not replace it.
- Startup-critical failures stop the current startup stage immediately.
- A higher-level orchestrator may explicitly continue after a component that is
  documented as optional fails, but the component call must still return its
  real error and publish the failure in `DeviceSnapshotService`.
- When classification or recovery is ambiguous, fail fast. Do not guess.

## Sensor and runtime data

- Publish a measurement only after a successful hardware read.
- Never manufacture, interpolate, zero-fill, or reuse a stale value as if it
  were a current sensor reading.
- A failed or unavailable sample must carry an explicit invalid/unavailable
  state, the real error, and its validity or sample timestamp where applicable.
- UI code must check validity and health before rendering numeric telemetry. It
  must show `UNAVAILABLE`, `NO FIX`, `STALE`, or the real error instead of a
  plausible numeric placeholder.
- CPU usage or any other metric that is not actually measured must be labelled
  unavailable; it must never be estimated.
- Synthetic data is allowed only inside the Windows simulator and must remain
  visibly labelled synthetic. It must never enter firmware snapshots.

## Persistent settings

- First-boot defaults are allowed only when a key is genuinely absent. The
  default, its key, and the first-boot decision must be logged, then persisted.
- A missing key and a failed/corrupt read are different states. Do not treat
  read, type, CRC, or storage errors as "not found".
- Never silently clamp, repair, migrate, overwrite, or substitute an invalid
  persisted value. Log it and return an error.
- Reset or repair requires an explicit user action with a visible result.

## Storage and destructive actions

- Internal LittleFS/NVS and removable SD have separate owners and purposes;
  neither is a fallback for the other.
- Never format removable storage implicitly.
- SD formatting requires a present card, a verified format-required state, and
  one explicit, single-use user authorization. If the card disappears or the
  immediate attempt cannot run, the authorization expires.
- Do not access physical hardware, serial ports, flash, erase, recover, or
  monitor flows unless the user explicitly authorizes that operation.

## Validation

- Run tests and validation workflows only when the user explicitly requests
  them. Do not automatically run configure/build/size passes, simulator checks,
  test suites, or validation scans during implementation or before delivery.
  Static code review is allowed; report deferred validation clearly.
- Before running any test suite or individual test, tell the user exactly which
  tests and commands you intend to run and wait for fresh explicit authorization.
  Build, flash, capture, implementation approval, or an earlier test approval
  never implicitly authorizes another test run.
- Treat compiler and linker errors as evidence; fix their root cause rather
  than suppressing warnings globally.
- Firmware compilation can take up to 240 seconds; allow at least a 240-second
  timeout for build invocations before treating them as stalled.
- Do not run intermediate configure, build, or size passes while implementation
  and integration work is still in progress. Finish the code changes, static
  review, and cross-component integration first; run a final validation
  package only on explicit user request after the task is complete. Limit compiler parallelism to at
  most 8 jobs, and after cancelling a build confirm that no Ninja or Xtensa
  compiler process remains before starting any later validation.
- When the user requests the complete final validation package, run a clean
  build, an incremental build, the size report, simulator checks,
  `git diff --check`, dependency/legacy scans, and a changed-files secret scan.
  A narrower request authorizes only the requested checks.
- Do not claim hardware behavior from host-only validation.

## Work Tracking

- Keep `AGENTS.md` as the single repository instruction file. Do not add
  parallel Copilot instruction files or duplicate agent contracts.
- Keep generated firmware and simulator build output under `artifacts/`; do not
  create profile build trees in the repository root.
- Use `scripts/device.ps1` as the sole public CLI/menu for firmware, simulators,
  generators and checks. Keep implementation in `commands/`, reusable helpers
  in `common/`, generators in `generators/`, and checks in `checks/`.
- Before every firmware build/flash (including combo/recover), clean transient
  artifacts through the shared artifact helper. Preserve active firmware,
  dependency locks, generated configuration and Python caches; `fullclean`
  explicitly cleans the selected build. Keep logs, captures, reports, temporary
  files and local simulator environments under `artifacts/`. Never delete
  versioned source assets as runtime-output cleanup.
- Treat `TODO.md` as the active backlog for open work.
- Treat `planned-todo.md` as the deferred, non-priority backlog, grouped by
  macro activity. Move an item to `TODO.md` when it becomes active work.
- Move completed items to `done.md`, stored next to `TODO.md`, during periodic
  cleanup so the active list stays focused.
- Record recurring repository lessons in `lessons.md` and check it before
  changing boot, startup, or Brookesia integration behavior.
