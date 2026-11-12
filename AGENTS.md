# Repository working agreements

These agreements apply throughout ESP32 Luna Shell. Use this file as the sole
agent contract, including for Copilot.

## Scope and delivery

- Use the current checkout and branch. Preserve unrelated work. A new branch,
  worktree or PR requires an explicit request.
- Implementation includes static source review. Get separate authorization
  for scans, simulator runs, configure/build/size, installation or device
  operations (serial, flash, erase, recover, capture or monitor). Announce the
  exact command and obtain fresh approval for each test or suite.
- Finish integration and static review before final validation. Skip interim
  configure/build/size passes. Allow at least 240 seconds and up to eight jobs
  per build. After cancellation, confirm Ninja and Xtensa stopped. Fix compiler
  and linker causes without suppressing warnings globally.
- A complete final package includes clean and incremental builds, size, simulator
  checks, `git diff --check` and dependency/legacy scans. Narrower requests cover
  named operations only. Scan changed source for secrets before an authorized
  GitHub push, excluding firmware images. Distinguish build, host, flash and
  physical evidence and report pending checks.
- Make focused local code checkpoints. Push with authorization. For requested
  docs-only delivery, commit only authorized docs with `[skip ci]` and push the
  current branch without builds, tests or CI/CD. Editing alone authorizes neither
  commit nor push. Report protection blocks and preserve the delivery rules.

## Platform and tools

- Target ESP-IDF **6.0.2** and official ESP-Brookesia **0.8**, without legacy
  SDKs or Phone framework compatibility code. Luna uses LVGL below Brookesia
  System Super/Core and declarative GUI. Cody and standalone display/touch
  use native 2D without LVGL or adapters. Keep board, interface, optional
  services and build mode independent.
- Use documented public interfaces. Explain limitations, supported options
  and maintenance risk before seeking an explicit exception for hacks, private
  interfaces or fragile overrides.
- Use `scripts/device.ps1` as the sole public CLI for firmware, simulator,
  generators and checks. Do not call the IDF Python entry point directly.
  Use `scripts/commands/`, `scripts/common/`, `scripts/generators/` and
  `scripts/checks/` for implementations, helpers, generators and checks.
- Build Debug apps and packages from current source. Obtain dependencies from
  official sources. For a first import of a compiled library or prebuilt
  executable, verify source and version and scan its archive and extracted
  binaries with Defender. Require a clean result and reuse it for unchanged
  imports in that staging session. Do not scan non-executable files or scripts.
  Keep gates in memory and a short rule in maintained READMEs. Stop on failed
  or unavailable required scans. No broad scans, bypass or old certifications.

## UI contracts

Before layout changes, read the [display geometry](docs/bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
and [Cody layout](docs/cody/layout.md) for chord limits, rotation, hit targets,
Back geometry and QR clearance.

- Product pages show a title and subtitle. QR pages show a title and the
  largest readable code that fits with its quiet zone. Tap returns to the
  previous route without submitting an action. Respect pairing cleanup.
- Home is an icon-only filled house on dark orange. Omit it in Wi-Fi/Bluetooth
  setup and Tools, Preferences and Device options. Back and Next use lateral
  chevrons where routes exist. Keep Back visually left and separate from content
  and footer actions. Fade chrome on inactivity and reveal on touch or valid IMU
  motion. Disable hidden hit targets. Use actual date/time, Wi-Fi strength,
  Bluetooth and battery with semantic colors.
- On Cody Home, character and status stay above Tokens, Model, Connection and
  Chat. Tap advances and deliberate shake returns to Tokens. Let long titles,
  activity, build date, PC timezone and hostname finish scrolling. Keep Setup,
  remote question and Luna routes. Home has no Back. The @CCP footer runs three
  full passes per display-owner lifetime, then hides. Pause it on source gaps,
  standby and chat or panel changes without replay.
- Use one diagnostics entry for Debug and Release with measured CPU, free
  RAM/PSRAM, FPS and IP. Show SD where supported. Memory shows used space,
  largest free block and remaining free space, without addresses. Back goes
  via overview to the retained route. Authenticated Normal restores it. Keep build-mode selection and
  facial channels out of UI/storage. Retain internal animation channels.
- Use actions without filled cards, gradients or borders, retaining existing
  icons before their text and the complete touch targets. Before adding a new
  icon to a text action, ask the owner. Questions show at most two numbered
  single-line full-width rows, with scrolling and full review before Send.
- Reveal complete controls after the title and body. Enable touch only while
  visible. Captions have no typewriter timing or audio count. Give each manga
  overlay one matching cue on appearance, subject to Sounds and quiet hours.
  Redraws never replay cues, and artwork never creates events.

## Code and data

- Use English for docs, comments, UI, logs and commits, including PDF titles,
  diagrams, captions, footers and corrections. Preserve screen IDs and evidence.
  Chat may be Italian. Bound Cody text to printable ASCII.
- Use descriptive names, readable steps and focused helpers. Above each new
  or changed function, give each parameter and exception its own comment line.
  Explain non-obvious ownership and invariants. Use typed enums and unique IDs
  stable across reordering. Reject duplicates and ambiguity. Keep indices as
  storage or API details.
- Budget large objects and copies for the deepest stack path, including logging
  and callbacks. Prefer bounded projections or synchronized owner buffers.
  Respect PSRAM cache and access limits. Revisit callers after schema growth.
  Budget stack changes. Inspect compiled frames and measure headroom before
  making runtime claims.
- Return original or more specific errors, including partial failures. Log
  rejected values and failures with component, operation and cause. Stop on
  startup-critical failure. Continue only for documented optional failures
  and return or publish the error through the owning component.
- `DeviceSnapshotService` is the sole consumer source for runtime and sensor
  data. Extend acquisition owners for new fields or cadences. They alone
  read hardware/BSP/I2C. No consumer diagnostics, parallel caches or freshness
  fallbacks. Publish successful reads with original timestamps, validity and
  health. Surface actual errors and unavailable, no fix or stale states.
  Never fabricate, interpolate, zero-fill or show stale data as current.
  Label synthetic Windows data and keep it out of firmware snapshots.
- Default only absent settings. Log key, default and first-boot decision and
  persist. Preserve read/type/CRC/storage errors. Clamping, repairing, migrating
  or overwriting invalid data requires explicit action and a visible result.
  Keep LittleFS/NVS separate from SD.
  Format only a present card with verified format-required status and explicit
  single-use approval that expires on removal or if formatting cannot start now.

## Files and records

| File | Purpose |
| --- | --- |
| `AGENTS.md` | Working agreements |
| `TODO.md` | Active work and open acceptance gates |
| `planned-todo.md` | Deferred work, stable IDs and activation conditions. Retain the existing development history for now. |
| `done.md` | Replace with one summary of the latest validation round |
| `lessons.md` | Reusable lessons. Read before boot, startup or Brookesia changes. |

- Document current behavior, settings, limits and open work. Keep one queue
  per purpose and leave unverified work pending. Do not accumulate archive or
  validation journals, old attempts, superseded results or CI/PR history.
  Do not move deleted history elsewhere.
- Use `artifacts/` for generated files, state, logs and local simulator environments.
  Run shared transient cleanup before authorized firmware builds or flashes,
  including combo/recover. Keep active firmware, locks, config, Python caches and versioned
  assets. Scope `fullclean` to the selected build. After handoff keep only
  requested deliverables. Report results in chat and retain no scan records,
  verification manifests or build history.
- End the README with `This is VibeWare`, the canonical manifesto and logo,
  project and VibeWare GitHub links, personal site, Terms and Privacy. Explain
  the initiative without claiming finished software or using a generic AI
  disclosure. Preserve root and third-party licenses and website parameters
  `utm_source=github`, `utm_medium=referral`, `utm_campaign=esp32-luna-shell`
  and placement-specific `utm_content`.
- Copy created or revised documentation PDFs byte for byte under the same name
  to `C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.
  Keep in-scope PDFs current, including outside the repo. When a correction is
  needed, use a final register with date and type on the left, description,
  original page/screen IDs and optional images on the right. Preserve dates
  and evidence and label security separately. Keep the
  [pairing quickstart](docs/cody/cody-ccp-pairing-quickstart-a5-v1.pdf)
  at four A5 instructional pages, revised in place without appendices.
