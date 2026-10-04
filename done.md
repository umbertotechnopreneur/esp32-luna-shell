# Completed implementation and evidence

This is a compact checkpoint record, not a release certificate. Open validation
and implementation gaps remain in [TODO.md](TODO.md); deferred work is in
[planned-todo.md](planned-todo.md). Historical revision details remain in Git
and referenced architecture/artifact records.

## Cody and connectivity — 2026-10-02 to 2026-10-04

- Implemented standalone Cody boot assets, front-facing named cloud rig, palette,
  face/contour channels, debug overlays, responsive Windows inspector and
  synthetic stimuli. Added the canonical-snapshot response engine, fatigue/
  stress, charge/microphone/playback response, eye taps and elastic drag.
  [Rig and response](docs/architecture/cody/rendering.md).
- Implemented typed scene phases, first-boot greeting/branding, measured HUD,
  shared cardinal auto-rotation, reusable entrance/teletype and face lifecycle:
  ordinary Calm/Happy/Calm, completed Sleep and dark Wake preparation.
  [Runtime and scenes](docs/architecture/cody/runtime.md).
- Ported the user-approved pearl scanline artwork and exact face warp; added
  pre-raster old/new damage, compact tiles, word conversion, prepared strokes/
  markers, edge shortlists and frame-owned DMA. Narrow historical captures
  reached about 29 FPS; CPU/frame targets remain open for the current image.
  [Rendering](docs/architecture/cody/rendering.md).
- Implemented screen 11's five typed internal-flash looks, stable-ID persistence,
  display-owned activation and asynchronous save. Defaults apply only to an
  absent key; real read/write/unknown-ID failures preserve the previous look.
  [Onboarding](docs/architecture/cody/onboarding.md).
- Added the post-pairing write/readback of `cody_firstboot=false`, the compile-time
  Debug-only screen 39, screen-09 60-second standby, and the source route
  09 -> 10 -> 36 -> 11 -> optional 12 -> 13 -> 14. Screen 11 now has a disabled
  Done footer until an explicit row tap and no first-run Back; screen 15 remains
  gated on a future real Codex/PC session producer. Added visually reviewed A04
  pages to both append-only atlases. Source/artifact review only; no build,
  simulator, automated test or device validation.
- Integrated async Start/Retry/Cancel, per-attempt proof/session QR, credential
  adoption and BLE handoff on main; HTTP remains opt-in. Added white QR themes,
  bounded integer scaling/quiet zones, rounded controls and gesture routing.
  [Pairing](docs/architecture/cody/onboarding.md).
- Checkpoint `f7d1925f`: completed OOBE footer actions, immediate route 07 with
  the current QR, real error/online gates, per-scene transient 60-second standby,
  retained software-wake route/active clocks/paused pose, and separate title/
  body/footer/look/status typing. Cold/deep-sleep reset remains distinct.
  Added the original flash microclick with bounded audio-owner notifications,
  coherent cancellation, quiet/volume/time gates and optional failure reporting.
  Source review/asset preparation only; no new tests/build/simulator/device
  validation. Private archive-cue auditions have unconfirmed rights and are
  not firmware assets.
- Implemented native snapshot-only status and Screenshot chrome with atomic SD
  BMP writing and temporary PSRAM; recent preview evidence covers control
  visibility only, not firmware status or storage behavior.
- Implemented trusted clock/quiet preferences, offline greetings, optional first
  RTC sample, deferred first-IP SNTP, measured RTC offset/rate before correction,
  and Cody RTC profile activation. No alarm scheduler/calibration or measured
  power-saving claim. [Clock and RTC](docs/architecture/cody/clock.md).
- Implemented optional Sophia wake-on-word profiles, capability gates, flash
  model, canonical microphone/events, AEC/pause ownership and software wake.
  Locks, full firmware closure and physical behavior remain open.
  [Wake-on-word](docs/architecture/wake-on-word.md).

## Recorded validation boundaries

- Authorized rig/response/native/Tk host checks passed at their checkpoints,
  including native/host model and raster parity. On 2026-10-04, snapshot-integrity,
  cody-native, cody-runtime, cody-response (15) and cody-poc (32) passed, covering
  real host persistence error/retry/activation paths. They do not prove async
  UI races, physical output or changes made afterward.
- Cody greeting/intro/render checkpoints were explicitly built/flashed on COM10.
  Revision `0295963d` later built in Debug with eight jobs and flashed a
  3,550,736-byte app with verified hashes/reset; no monitor or UI/leak-soak
  observation accompanied it. Later source increments need their own validation.
- Earlier live captures showed continuous frames/rotation and did not reproduce
  the intermittent stop/reset report. The first failure capture lacked the
  trigger; neither later captures nor the working-device report establish a fix.
  [Failure evidence](docs/architecture/cody/rendering.md).
- Wake-word integration received build-mode/power/snapshot and console host
  checks plus limited syntax/provenance checks, not a full firmware link/device
  test. Physical audio, rotation, standby, electrical and stack evidence remains
  explicitly open.

## Platform, runtime and tools

- Reorganized 82 owned source/private-header files under responsibility-based
  `src/` folders; kept public headers, component IDs, configuration, vendor code
  and assets in place. Updated CMake and source-contract consumers, added short
  folder READMEs, consolidated 13 Cody guides into five, and shortened root
  rules/backlogs without closing unresolved gates. Static integration review
  only; compilation, checks and device validation remain unperformed.
- Separated physical display/touch from LVGL UI, added standalone hardware locks/
  RGB565/input APIs and consolidated the device console behind `scripts/device.ps1`.
  Added deliberate COM selection, shared native-error formatting, menu/help,
  artifact cleanup/containment and bounded compiler jobs. Recent console changes
  have source review only.
- Implemented SD boot-backlog/previous-log retention and CRC/revision-protected
  native USB readback. Monitor attaches without reset by default; reset is
  explicit. No physical SD/readback validation for this increment.
  [Offline logging](docs/architecture/offline-logging-usb.md).
- Added Debug/Release modes and touch/IMU Deep-sleep policy with explicit wake/
  rejected-entry recovery. Integrated current main source policies and hardware
  consumption inventory; actual current/wake behavior remains unverified.
  [Build modes](docs/architecture/firmware-build-modes.md),
  [Deep-sleep](docs/architecture/release-deep-sleep.md).
- Implemented frame deadlines/watchdogs/DMA leases, retained failure/byte reports,
  grouped companion state and separated snapshot reporters/log/pairing/document
  actions. Source splits and prepared regressions do not complete their release
  gates. [Mitigations](docs/architecture/display-touch-runtime.md).
- Earlier Luna checkpoints implemented canonical snapshots/freshness/CPU,
  synthetic device twin, user-started BLE provisioning, resident bootstrap/lazy
  route documents, guarded SD formatting, stationary calibration, transactional
  standby/Performance Mode/rotation, opt-in HTTP and measured FFT visualization.
- Added isolated profiles/reviewed locks, timestamp-driven generated staging,
  resource budgets, hot-path caches, bounded logger/memory reporting, diagnostic
  sample reuse, parked idle acquisition, PMU battery validity, RTC oscillator-stop
  retention and trusted-clock coalescing. Hardware savings are not established.
- Implemented 12-second watchdog/liveness domains, retained breadcrumbs, bounded
  mutex/SDK-worker lifetimes and error-preserving transactional recovery.
  Forced-stall and stack-margin device checks remain open.

## Leg Edition — 2026-09-27

- Implemented the five-page measured IMU UI, activity/relative-peak indicators,
  lower navigation, fades/dropdown styling, saved motion/sound policy, embedded
  boot WAV and louder movement cues. Serial evidence recorded deliberate level-3
  cues, no repeated table-vibration cue, qualified motion wake and approximately
  25-second display-off. Playback `ESP_OK` does not prove audible output.
- Latest recorded navigation image:
  firmware SHA-256 `69C47732DAC8CD712F71767F16994B52AC2CCCFD0D37A174E66BD7032E7AD492`;
  LittleFS `8282F023B283A4792C22C17B35287C0B96CE6B9701C3BC2A6C85A6BD0996FCC5`.
  Both were verified/flashed on COM8; previews/captures are under
  `artifacts/leg-*`. Latest physical UI, acoustics, levels 1/2, PWR,
  persistence/rotation and disabled-RTC summary remain open.
