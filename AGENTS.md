# Repository working agreements

Applies throughout ESP32 Luna Shell. This is the only instruction file;
do not add nested agent files or parallel Copilot contracts.

## Scope and delivery

- Implement in the existing checkout and branch. Creating a branch, worktree or
  PR needs an explicit request for that operation. Preserve unrelated work.
- Implementation permits static source review, not tests, validation scans,
  simulator execution, configure/build/size, installation or physical-device
  operations (serial, flash, erase, recover, capture or monitor).
- Before each test or suite, announce the exact command and obtain fresh explicit
  authorization. Earlier test approval or permission to implement/build/flash/
  capture does not authorize another run (owner reaffirmed 2026-10-15).
- Finish implementation, integration and static review before requested final
  validation. No intermediate configure/build/size passes. Use at most eight
  compiler jobs and allow at least 240 seconds per build. After cancellation,
  confirm Ninja/Xtensa processes stopped before another run. Fix compiler/linker
  causes; never globally suppress warnings to hide them.
- A requested complete final package covers clean/incremental builds, size,
  simulator checks, `git diff --check`, dependency/legacy scans and a changed-source
  secret scan at GitHub publication. Narrower requests cover only named operations.
  Build, host and flash results do not prove physical behavior; report deferred work.
- Secret scans belong to authorized publication, never local compilation, flash
  or capture. Inspect changed source before push; never scan firmware images.
- For code, make focused local checkpoint commits at coherent boundaries. Push
  needs separate authorization. For documentation-only work, editing authorizes
  neither commit nor push: when delivery is requested, use the current branch and
  `[skip ci]`, without builds, tests or CI/CD. No PR unless explicitly requested;
  report protection blocks instead of changing the delivery route or rules.

## Platform and tools

- Use documented public integration points. Hacks, private protocols, undocumented
  APIs and fragile startup/runtime overrides need explicit exception approval
  after explaining the limitation, supported alternatives and maintenance risk.
  General implementation/build/install permission is not that approval.
- Target ESP-IDF **6.0.2** and official ESP-Brookesia **0.8** only. No IDF 5.x,
  Brookesia 0.6/0.7 or removed Phone framework shims, wrappers, migrations or fallbacks.
- Luna uses LVGL below Brookesia System Super/Core and declarative GUI packages.
  Cody and standalone display/touch use native 2D without LVGL or adapter dependencies.
  Keep physical-board, interface, optional-service and build-mode choices separate.
- `scripts/device.ps1` is the sole public CLI/menu for firmware, simulator,
  generators and checks. Never call the IDF Python entrypoint directly.
  Under `scripts/`, use `commands/`, `common/`, `generators/` and `checks/` for
  implementations, helpers, generators and checks respectively.
- Build Debug applications/packages from current source, never installed or
  prebuilt application copies. Missing dependencies come from official sources.
  Keep first-time external imports aside for one targeted on-demand Defender
  scan of the archive and extracted/imported dependencies; record source,
  version/hash, command and clean completion. Reuse evidence for unchanged imports.
  Failed/unavailable scans block import; no whole-system scans or detection bypass.
  Keep a short requirement in maintained READMEs; do not certify old imports.

## UI contracts

Read [display geometry](docs/bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
before layout changes and [Cody layout](docs/cody/layout.md) for control details.

- Canvas: 466 x 466, center (233,233), radius 233. Baseline safe square:
  320 x 320 at (73,73), half-open [73,393). Admit artwork and complete hit bounds
  against every occupied row's circular chord; rounded margins are not strips.
  Preserve QR quiet zones and navigation clearance.
- Back: left lateral circle at (10,203), 60 x 60, radius 30; one centered left
  chevron, no text, full touch target, only with a previous route. Keep it separate
  from content and primary/footer actions, never beside or below a footer.
- Rendering, clipping and touch share logical 0/90/180/270-degree coordinates.
  Back stays visually left; never mirror text or rotate touch independently.
- Cody Home keeps character/status above rotating Tokens, Model, Connection and
  Chat panels. Tap advances; deliberate shake returns to Tokens. Long titles and
  activity finish scrolling before Chat advances; Connection also allows full
  build-date, PC-timezone and hostname reading. Home has no Back; Setup,
  remote questions and Luna retain routes.
- The @CCP footer makes three full passes per display-owner lifetime, then hides.
  Source gaps, standby and chat/panel changes pause it without replay.
- Diagnostics (2026-10-16): one entry in Debug and Release, with measured CPU,
  free RAM/PSRAM, FPS, IP and SD. Memory shows heap capacity/fragmentation: used,
  largest free block and other free space, not addresses. Back returns to overview,
  then retained route; authenticated Normal restores that route. No build-mode
  choice or facial-channel UI/storage; retain internal animation channels.
- Cody actions (2026-10-14): text-only captions; two rounded gradient cards side
  by side, three as that pair plus one centered below, one as a wide centered
  capsule. Question choices are numbered single-line full-width rows, at most
  two visible, with scrolling and full detail review before Send. Use semantic
  colors and restrained gradients. Approved references are
  `docs/cody/ui-proposals/20261009/selected-multi-action.png` and
  `selected-single-action.png` in the same directory.
- Reveal complete controls after title/main body finish; admit touch only while
  visible. Captions have no typewriter timing or character-audio counts.

## Code and data

- Write repository docs, code comments, UI text, logs and commits in English.
  Revised PDFs include English titles, diagrams, captions, footers and historical
  text while retaining dates, screen IDs and evidence. Chat may remain Italian.
  Cody text is bounded printable ASCII; no Unicode/localization layer.
- Prefer readable steps, descriptive names and helpers with one responsibility.
  Avoid dense branches, unrelated one-liners and speculative abstractions.
  Above each new/modified function, give each parameter and applicable exception
  its own comment line. Explain non-obvious ownership/invariants inside functions.
- Use typed enums and unique names/IDs for domain identities; reject duplicates
  and ambiguity, preserve identity through reordering. Indices are storage/API details.
- Budget large objects, arrays, snapshots and copies against the owning stack and
  deepest nested call chain, including callbacks/logging. Prefer bounded projections
  or synchronized owner buffers; PSRAM requires valid access/cache conditions.
  Never blindly enlarge a task stack. After schema growth, revisit callers;
  inspect compiled frames and measure device headroom before runtime claims.
- Return original or more specific errors; never turn partial failure into
  `ESP_OK`, discard init/read/write/persistence/service failures or guess recovery.
  Log rejected values and failed operations with component, operation and cause.
  Startup-critical failure stops its stage. Only documented optional failures may
  let orchestration continue; the component returns/publishes failure in the snapshot.
- `DeviceSnapshotService` is the only runtime/sensor source for consumers.
  Only acquisition owners read hardware/BSP/I2C. Extend those owners for new
  fields/cadences; no consumer diagnostics, parallel caches or freshness fallbacks.
- Publish measurements only after successful reads, with original timestamps,
  validity, health and actual errors. Failed/unavailable samples remain explicit;
  no fabrication, interpolation, zero filling or stale-as-current values. Check
  validity before numeric UI and show UNAVAILABLE, NO FIX, STALE or the error.
  Unmeasured metrics stay unavailable. Label synthetic Windows data and never
  publish it into firmware snapshots.
- Default only absent settings: log key/default/first-boot decision and persist.
  Read/type/CRC/storage errors are not absence. Never silently clamp, repair,
  migrate or overwrite invalid settings; reset/repair needs explicit action and
  a visible result. Internal LittleFS/NVS and removable SD have separate purposes.
- Never implicitly format SD. Require a present card, verified format-required
  state and explicit single-use authorization, expiring if the card disappears
  or formatting cannot be attempted immediately.

## Files and records

| File | Keep here |
| --- | --- |
| `AGENTS.md` | Current working agreements |
| `TODO.md` | Active work and its remaining acceptance gates |
| `planned-todo.md` | Deferred work, stable IDs and activation conditions |
| `done.md` | Short dated completion/evidence summaries |
| `archive.md` | Detailed historical records and superseded plans |
| `lessons.md` | Reusable lessons from observed failures |

- Move history to `archive.md`; do not duplicate queues or add alias TASKS/TODO/
  lessons files. Archived plans are not active instructions. Never mark pending
  validation done. Read `lessons.md` before boot/startup/Brookesia changes.
- Keep generated builds, state, logs, captures, reports, temporary files and local
  simulator environments in `artifacts/`. Before each authorized firmware build/
  flash (including combo/recover), use shared transient cleanup. Preserve active
  firmware, locks, generated configuration and Python caches. `fullclean` affects
  only the selected build; never delete versioned assets as runtime cleanup.
- The README ends with `This is VibeWare`: canonical manifesto, approved logo,
  project/VibeWare GitHub links, personal site, Terms and Privacy. Explain the
  initiative without claiming finished software or using a generic AI disclosure.
  Website links retain `utm_source=github`, `utm_medium=referral`,
  `utm_campaign=esp32-luna-shell` and placement-specific `utm_content`.
  Preserve root and third-party license boundaries.
- Copy every created/revised documentation PDF byte for byte, same filename, to
  `C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.
- Each requested PDF revision keeps a final revision register: date and type
  (Security, Errata, Addendum or Change) left; description, original page/screen
  IDs and optional images right. Preserve earlier corrections/dates and unrelated
  content, consolidating errata/addenda there. Security has its own labeled section.
  This layout also applies to PDFs in scope outside the repository.
- Exception: `docs/cody/cody-ccp-pairing-quickstart-a5-v1.pdf` keeps four A5
  instructional pages for nontechnical users, updated in place; never append
  a revision register, errata, addenda or technical security appendix.
