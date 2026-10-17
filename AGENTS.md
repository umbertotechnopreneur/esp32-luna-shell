# Repository engineering rules

These rules apply throughout ESP32 Luna Shell. This is the sole repository
instruction file; do not add parallel agent or Copilot contracts.

## Platform and tooling

- Use documented public integration points. Do not implement or install hacks,
  undocumented entry points, private-protocol bridges or fragile startup/runtime
  overrides. Explain the limitation, supported alternatives and maintenance risk
  to the owner before proposing an exception; obtain explicit approval before
  implementing it. General autonomy, build or installation authorization is not
  approval for such an exception.
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
  explicit authorization. Never start tests on your own initiative. Earlier test
  approval, general autonomy or approval to implement, build, flash or capture
  does not authorize another test run. This owner requirement was reaffirmed on
  2026-10-15; preparing tests does not authorize executing them.
- Create a branch, pull request or worktree only when the owner explicitly asks
  for that operation. General implementation, autonomy, commit or push requests
  do not authorize creating any of them. Use the existing checkout and branch.
- Finish implementation, static review and cross-component integration before
  requested final validation; no intermediate configure/build/size passes.
  Limit compilation to eight jobs and allow at least 240 seconds per build.
  After cancellation, confirm Ninja/Xtensa compiler processes have stopped before
  starting later validation. Fix compiler/linker causes; never globally suppress
  warnings to conceal them.
- An explicitly requested complete final package comprises clean/incremental
  builds, size, simulator checks, `git diff --check`, dependency/legacy scans and
  a changed-files secret scan at GitHub publication. A narrower request authorizes only its named checks.
  Host assertions, successful builds and flashes do not prove physical behavior.
- Secret scanning belongs to GitHub publication, not local configure/build/size,
  flash or capture. Inspect changed source files before an authorized push;
  never scan firmware images or run secret checks as part of local compilation.

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
  [circular-display-layout](docs/bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md) before
  changing layouts.
- Cody Home keeps the character and status fixed above rotating Tokens, Model,
  Connection and Chat panels. Tap advances; deliberate shake returns to Tokens.
  Long titles/activity hold Chat until their horizontal scroll completes;
  Connection also allows complete reading of build date, PC timezone and hostname.
  Home panels have no Back control. Setup, remote questions and Luna retain routes.
  The @CCP footer makes three complete passes per display-owner lifetime, then
  disappears; source gaps, standby and chat/panel changes pause it without replay.
- Cody diagnostic-screen decision (2026-10-16): Keep one device diagnostic entry
  in both Debug and Release, with measured CPU, free RAM/PSRAM, FPS, IP and SD
  status. Its Memory button opens internal RAM/PSRAM heap capacity and
  fragmentation details; Back returns to the overview, then to the retained route.
  Bars show used capacity, largest free block and other free space, not addresses.
  Remove the facial-channel list and its storage from the device UI;
  retain the internal animation channels. Never require a build-mode choice.
  Normal restores the retained route. Preserve authenticated capability checks.

## Code clarity

- Write repository documentation, code comments, user-facing text, log messages
  and commit messages in English.
  Conversation with the user may remain Italian. The English requirement also
  covers PDF titles, diagrams, captions, footers and historical text in revised
  documents; preserve original dates, screen IDs and evidence when translating.
- Keep the root README footer headed `This is VibeWare` with the canonical
  manifesto, approved logo, project/VibeWare GitHub links, personal site,
  Terms and Privacy. Explain the initiative without claiming the software is
  finished or using a generic "I created this software with AI" disclosure.
  Website footer links use `utm_source=github`, `utm_medium=referral`,
  `utm_campaign=esp32-luna-shell` and placement-specific `utm_content`.
  Preserve the root license and third-party license boundaries.

- Cody action layout decision (2026-10-14): captions use text without action icons.
  Two ordinary actions use rounded gradient
  cards side by side; three use the same pair plus one centered action below.
  One action uses a centered wide capsule. Question choices use numbered,
  single-line full-width rows, at most two visible, with scrolling and explicit
  detail review before Send. Apply semantic colors and restrained gradients to Cody
  screens. Preserve the separate lateral Back, QR quiet zones and local chords.
  Approved references: `docs/cody/ui-proposals/20261009/selected-multi-action.png`
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

- Whenever a documentation PDF is created or updated in this repository, copy
  the final file byte for byte to
  `C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`,
  retaining the same filename. This copy is a required part of the documentation
  task; do not regenerate or convert the PDF for the Obsidian copy.
- For every user-requested PDF revision, maintain a dedicated final revision
  register. Use a structured grid with date and intervention type (Security,
  Errata, Addendum or Change) in the left column, and the description, affected
  original page/screen IDs and optional supporting images in the right column.
  Preserve earlier corrections and unrelated content; consolidate older errata
  and addenda into this register without losing their dates or substance.
  Apply this reusable layout to PDFs in scope, including documents outside the
  repository. Keep security entries under a clearly labeled Security section.
- Exception: `docs/cody/cody-ccp-pairing-quickstart-a5-v1.pdf` is a four-page A5
  flyer for nontechnical users. Keep its instructions current directly in those
  four pages; never append a revision register, errata, addenda or technical
  security appendix to this file.
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
