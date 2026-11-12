# Repository working agreements

These project rules complement the global agreements and apply to all agents,
including Copilot. Use the current checkout and branch; preserve unrelated work.
Create a branch, worktree or PR only when explicitly requested.

## Scope, validation and delivery

- Implementation includes static source review. Obtain separate authorization
  for scans, simulator runs, configure/build/size, installation and device
  operations (serial, flash, erase, recovery, capture or monitoring). Announce
  the exact command and obtain fresh approval for each test or suite.
- Complete integration and static review before final validation. Avoid interim
  configure/build/size passes. Allow at least 240 seconds and up to eight jobs
  per build. After cancellation confirm Ninja and Xtensa have stopped. Fix
  compiler/linker causes without globally suppressing warnings.
- A complete final package requires clean and incremental builds, size,
  simulator checks, `git diff --check` and dependency/legacy scans. Narrower
  requests cover named operations only. Before an authorized GitHub push, scan
  changed source for secrets, excluding firmware images. Report pending checks
  and distinguish build, host, flash and physical evidence.
- Make focused local code checkpoints; push only when authorized. For requested
  docs-only delivery, commit only authorized docs with `[skip ci]` and push the
  current branch without builds, tests or CI/CD. Editing alone authorizes
  neither commit nor push. Report protection blocks.

## Platform and tools

- Target ESP-IDF **6.0.2** and official ESP-Brookesia **0.8**. Luna uses LVGL
  below Brookesia System Super/Core and declarative GUI; Cody and standalone
  display/touch use native 2D without LVGL or adapters. No legacy SDKs or Phone
  framework compatibility. Keep board, interface, services and build mode
  independent.
- Use documented public interfaces. Explain limitations, supported alternatives
  and maintenance risk before requesting an exception for private interfaces,
  fragile overrides or hacks.
- Use `scripts/device.ps1` as the sole public CLI for firmware, simulator,
  generators and checks; never call the IDF Python entry point directly. Keep
  implementations under `scripts/commands/`, `scripts/common/`,
  `scripts/generators/` and `scripts/checks/`.
- Build Debug apps and packages from source. Verify the official source and
  version of imported compiled libraries and prebuilt executables. The owner
  has revoked Defender scans for the Cody build pipeline; do not initiate one.

## UI

Before layout changes read the [display geometry](docs/bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md),
[Cody layout](docs/cody/layout.md) and [design system](docs/cody/design-system.md).
Treat their geometry, routes and interaction behavior as contracts.

- Product pages have title/subtitle. QR pages prioritize a title and the largest
  readable code with quiet zone; tapping returns without submitting an action.
  Respect pairing cleanup.
- Home uses an icon-only filled house on dark orange, with no Home in Wi-Fi or
  Bluetooth setup, Tools, Preferences or Device options. Back/Next use lateral chevrons; Back
  stays visually left and separate from content and footer actions. Fade chrome
  on inactivity and reveal it on touch or valid IMU motion. Hidden controls have
  no hit targets; status uses real date/time, Wi-Fi strength, Bluetooth and
  battery with semantic colors.
- Preserve Cody Home's character, status, panel order, tap/shake navigation,
  long-text scrolling, Setup, remote-question and Luna routes. Home has no Back.
  The finite @CCP footer completes three passes per display-owner lifetime and
  pauses without replay; its detailed behavior is in the Cody layout.
- Use one diagnostics entry in Debug and Release with measured CPU, RAM/PSRAM,
  FPS and IP, and SD where supported. Memory shows used, largest free block and
  remaining space without addresses. Back goes through overview; authenticated
  Normal restores the retained route. Keep build mode and facial channels out
  of UI/storage while retaining internal animation channels.
- Actions have no filled cards, gradients or borders. Preserve existing icons
  before their text and complete hit targets; ask the owner before introducing
  a new text-action icon. Questions show at most two numbered, single-line,
  full-width rows, with scrolling and complete review before Send.
- Reveal complete controls after title/body; admit touch only while visible.
  Captions have no typewriter timing or audio count. Each manga overlay gets
  one matching cue on appearance, subject to Sounds and quiet hours. Redraws
  never replay cues and artwork never creates events.

## Code and data

- Use English for docs, comments, UI, logs and commits, including PDFs. Chat may
  be Italian. Keep Cody text printable ASCII; preserve screen IDs and evidence.
- Use descriptive names, focused helpers, typed enums and unique IDs stable
  across reordering. Reject ambiguity/duplicates. Above each new or changed
  function, document each parameter and exception on its own line; explain
  non-obvious ownership and invariants. Indices are storage/API details.
- Budget objects, copies and stacks for the deepest call, including logging and
  callbacks; prefer bounded projections or synchronized owner buffers. Respect
  PSRAM access limits and revisit callers after schema growth. Inspect compiled
  frames and measure headroom before runtime claims.
- Preserve original or more specific errors, including partial failures; log
  rejected values and failures with component, operation and cause. Stop on
  startup-critical failure. Continue
  only for documented optional failures and publish or return their errors.
- `DeviceSnapshotService` is the sole consumer source for runtime/sensor data.
  Acquisition owners alone read hardware/BSP/I2C and add fields or cadences.
  Do not add consumer diagnostics, parallel caches or freshness fallbacks.
  Preserve timestamps, validity, health and actual errors; surface unavailable,
  no-fix and stale states. Do not fabricate,
  interpolate, zero-fill or present stale data as current. Label synthetic
  Windows data and keep it out of firmware snapshots.
- Default and persist only absent settings, logging the key, default and first-boot
  decision. Preserve read/type/CRC/storage errors. Clamping, repair, migration
  or overwriting invalid data requires explicit action and visible outcome.
  Keep LittleFS/NVS separate from SD. Format only a present card with verified
  format-required status and explicit single-use approval that expires on
  removal or if formatting cannot start now.

## Records and documentation

- `TODO.md` owns active work and open gates; `planned-todo.md` owns deferred
  work, stable IDs and activation conditions (retain its existing history for
  now). `done.md` holds only the latest validation summary. Read `lessons.md`
  before boot, startup or Brookesia changes.
- Document current behavior, settings, limits and open work. Keep one queue per
  purpose and leave unverified work pending. Do not retain validation journals,
  old attempts, superseded results or CI/PR history, or move deleted history
  elsewhere.
- Use `artifacts/` for generated files, state, logs and simulator environments.
  Do not clean artifacts automatically before or after builds or flashes. Remind
  the owner that artifact cleanup is manual. Run cleanup only on explicit request;
  preserve active firmware, locks, config, Python caches and versioned assets,
  and scope `fullclean` to the selected build. Report results in chat without
  scan records, verification manifests or build history.
- End the README with `This is VibeWare`, the canonical manifesto/logo, project
  and VibeWare GitHub links, personal site, Terms and Privacy. Do not claim
  finished software or add a generic AI disclosure. Preserve root and
  third-party licenses and website parameters `utm_source=github`,
  `utm_medium=referral`, `utm_campaign=esp32-luna-shell` and placement-specific
  `utm_content`.
- Copy created or revised documentation PDFs byte for byte, with the same names, to
  `C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.
  Keep in-scope PDFs current in both places. Corrections use a final register:
  date/type on the left, description and original page/screen IDs on the right;
  preserve dates/evidence and label security separately. Keep the [pairing
  quickstart](docs/cody/cody-ccp-pairing-quickstart-a5-v1.pdf) at four A5
  instructional pages without appendices.
