# Completed implementation and evidence

This is a compact checkpoint record, not a release certificate. Open validation
and implementation gaps remain in [TODO.md](TODO.md); deferred work is in
[planned-todo.md](planned-todo.md). Historical revision details remain in Git
and referenced architecture/artifact records.

## Cody and connectivity — 2026-10-02 to 2026-10-05

- Measured the S3 `simd` kernel on the same fixed 100% Pearl Mist route for 40
  seconds: 697 frames, 63,000 submitted pixels each, frame mean 55,846 us and
  full-run p95 56,495 us. Jelly mean 35,880 us; 31,434,700 pixels used SIMD,
  139,400 used scalar tails and 11,567,412 skipped all-zero footprints. All
  697 frames exceeded the 40 ms budget. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-100.log`.
  A fresh C run is required before attributing a measured gain to SIMD.
- Flashed the S3 Debug verifier with all written hashes accepted. In the second
  80-second reset/capture, the actual PIE kernel passed 65,536 weight pairs and
  262,144 reference samples, then compared 30,267,144 live RGBA samples across
  489 measured frames without a mismatch. The benchmark finished, restored the
  scalar renderer and excluded contaminated timing data. The first capture was
  interrupted by touch after 229 measured frames; neither capture reported a
  panic. Evidence: `artifacts/build-cody-companion-debug/evidence/cody-simd-verify-2.log`.
  This proves observed parity for the fixed workload, not a performance gain.
- Built `0883de76` as ESP32-S3 Cody Debug with ESP-IDF 6.0.2 and eight jobs.
  The S3 assembly and C++ validation source compiled; the final ELF links
  `cody_jelly_bilinear_s3`. App image: 3,725,904 bytes, 75% partition free;
  ELF SHA256 `5f615d5437f90b4d762642b9647f92db319751f3b657f0c75bb591d964b94908`.
  The authorized host-check attempt stopped before assertions with Windows
  `WinError 216`: the specified ESP Clang produced an executable Windows cannot
  launch. After updating one stale screenshot-control assertion to the authored
  hidden behavior, the full `cody-raster-benchmark` suite passed with MSVC,
  including the host SIMD arithmetic model over all 65,536 weight pairs. Reports
  were retained under `artifacts/build-cody-companion-debug/evidence/` before
  transient-artifact cleanup. Target execution is still pending.
- Implemented an opt-in ESP32-S3 PIE bilinear sampler (source review only):
  two RGBA pixels per eight 16-bit lanes, 40-bit accumulation with the same three
  rounded lerps, aligned row scratch, unchanged RGB565 composition and scalar
  odd tails. Explicit `scalar`, `simd_verify` and `simd` benchmark selections
  preserve backend identity and restore the ordinary scalar path on exit.
  Added an independently computed 256x256-weight/guard contract and live sample
  comparisons for `simd_verify`; this mode suppresses performance summaries.
  Extended host contracts with a labeled arithmetic model, packer/byte-order/
  clipping/tail checks and deliberate mismatch injection. Generated configuration
  selects verification, not a claimed fast path. At the source checkpoint,
  tests, build and S3 execution had not run; no speed or target parity is claimed.
  The preceding `5947db83` detail-only flash command was cancelled during CMake
  reconfiguration, before flashing, when the user withdrew COM access. No build/
  flasher processes remained. Last verified device image remains `4ac6c812`.
- Built and flashed raster baseline `4ac6c812` with ESP-IDF 6.0.2, Debug, eight
  jobs and COM10 at 460800 baud; all written hashes verified. App: 3,721,952
  bytes, 75% partition free. Matching ELF SHA256:
  `ecce55a8922eaffe430db7531c2084f9c2618b29c8fa3decf8c1b956a7d7bf97`.
  The completed 80-second reset/capture contains one boot and no captured panic
  or reboot. Fixed Ready/Pearl Mist at 100%, coarse profiling, produced 684
  measured frames in the 40-second window, each submitting 63,000 pixels:
  mean 56,922 us, p95 57,611 us, max 58,605 us, all 684 over the 40 ms budget.
  Stage means: geometry 2,781 us, raster 40,886 us, transfer 12,243 us;
  artwork subsets: jelly 37,080 us, vector face 3,003 us. Logged steady FPS was
  about 17.0--17.2, with worker-core CPU around 85--86%. The run completed,
  restored the transient standby override/look and entered the deferred greeting.
  Minimum logged worker free stack: 3,440 of 12,288 bytes. SD initialization
  timed out three times and unrelated long log records were truncated; these
  conditions remain part of this baseline. Host contracts, interruption cases,
  other sizes/detail mode and SIMD gains remain unverified. Evidence and exact
  ELF/bin are retained under
  `artifacts/build-cody-companion-debug/evidence/cody-4ac6c812*`.
- Reworked the temporary raster benchmark in `feat/simd-perf-opt` (source review
  only): worker-owned Debug/Pearl entry, forced fixed Ready redraw, 5-second warmup
  and 40-second measurement, core-1 affinity, bounded PSRAM full-run p95 and
  40 ms misses. Restores transient look/standby settings and deferred greeting;
  power, capture, touch and workload changes interrupt rather than certify a run.
  Coarse fused rendering is separate from opt-in 16-pixel block profiling; shared
  profile types no longer depend on the Pearl feature gate. Added finite-window
  and reference pixel-parity contracts through `cody-raster-benchmark`, not run.
  Regenerated runtime settings/manifest through the public generator; its demo
  channels now also match the already-authored/current generated rig presets.
  No tests, firmware build or physical capture ran for this checkpoint, and no
  SIMD implementation or measured gain is claimed.
- Captured 28 deterministic `CODY_companion` stack overflows in 100 seconds
  after flashing `dd9f05d8`. The matching ELF shows the worker/frame/coverage
  call chain alone needs 8,176 bytes against an 8,192-byte stack; enabling cheek
  coverage added 2,496 bytes at that depth. Raised only the PSRAM worker stack
  to 12 KiB, preserving the requested artwork and internal DMA headroom, and
  added Debug minimum-free-stack logging sampled at most every two seconds.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-dd9f05d8-reboot-loop.log`.
- Removed the boot website branding together with its layout, fade clock,
  raster layer and damage cache. Sleeping/Waking/Ready and first-setup timing
  remain unchanged; this does not remove attribution from the planning PDFs.
- Built/flashed correction `d766e08c` (eight jobs, Debug, COM10, verified writes).
  App: 3,715,104 bytes, 75% partition free; matching ELF SHA256 starts `70961649e`.
  The first capture stopped after 43 seconds with a closed host serial stream,
  not a captured firmware panic. The following 100-second capture completed:
  one boot, no panic/reboot and successful inactivity standby/radio teardown.
  Cody's measured minimum free stack was 3,524 bytes (peak used 8,764/12,288).
  Steady screen 05 ran about 24.4 FPS with sampled artwork around 14 ms and
  geometry around 3 ms; this is a narrow baseline, not a full-scene guarantee.
  Repeated wake/visual/audio/storage checks remain open. Retained ELF and logs:
  `artifacts/build-cody-companion-debug/evidence/cody-d766e08c*`.
- Raster optimization stage 1 (source only): all-zero bilinear footprints skip
  the original three packed lerps; zero-alpha samples preserve destination bytes
  without load/swap/store. Other samples retain their arithmetic. Added separate
  jelly/vector microsecond timings within artwork and reference contracts for
  transparent prefixes, fractional samples, backgrounds, byte order and guards.
  No test/build/device comparison ran; an extra nonzero-footprint branch may
  offset the saving. The device still runs `d766e08c`, not this optimization.
- The earlier Cody BLE startup mitigation reserved a 30,720-byte internal DMA
  block before display startup and parked display frames during radio startup.
  It passed an eight-job Debug build, flash and short capture, but a later
  100-second capture exposed intermittent BLE DMA preflight failure and a
  separate NimBLE teardown panic at standby. The short capture was not proof
  that the reboot loop was fixed. Evidence preserved before flash cleanup:
  `artifacts/build-cody-companion-debug/evidence/cody-standby-wake-20261004.log`.
- Added a larger held BLE startup lease and ordered NimBLE stop/deinit so the
  host cannot free its dynamic context before the stop caller finishes. These
  source changes were built and flashed in `dd9f05d8`; repeated cold boot and
  standby/wake behavior are open in TODO.md.
- Authored distinct Worried (06) and Afraid (08) faces from the planning atlas,
  kept the white 06 QR layout, routed 08 Retry back to 05 only after confirmed
  cleanup, and enabled the Debug FPS/CPU HUD on authored scenes without visible
  bottom actions. These changes were built and flashed in `dd9f05d8`; current
  physical display and pairing checks remain open.
- Refined the full atlas face catalog and softened Cody's cheeks. Replaced the
  blue boot source frames with Imagegen pearl Sleeping/Waking/Ready frames,
  packaged as flash-owned indexed assets. Added session-controlled PWR capture
  with a retired-frame 3, 2, 1 countdown, SD result feedback and an original
  shutter cue; the old touch camera control is hidden. These changes were built
  and flashed in `dd9f05d8`; no current simulator or physical behavior check ran.
- Built `dd9f05d8` Cody Debug with ESP-IDF 6.0.2 and eight jobs, then flashed
  COM10 at 460800 baud. Bootloader, partition table and 3,715,936-byte app
  passed written-data hash verification; the device was reset after flash.
  The app partition retains 75% free space. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-dd9f05d8-flash.log`.
  No additional test suite or serial capture was run; successful flashing
  does not establish reboot-loop, standby/wake, graphics or audio behavior.
- Created the English A5 front/back CCP pairing quick-start leaflet in Obsidian
  and `docs/Cody/`, with the proposed ZIP/local-marketplace installation path,
  display-code pairing, ten-second Idle flow, atlas illustrations and note lines.
  Both pages were rendered and visually inspected. No ZIP release, plugin
  install/runtime, firmware, LAN or hardware validation is claimed.
  [Artifact record](docs/planning/2026-10-04-ccp-pairing-quickstart.md).
- Checkpoint `f4cc7e97`: adopted Alpha terminology in active code/tools; boot
  cues, runtime authoring and appearance assets have coordinated names. Persisted
  keys, routes, skin IDs, parameter values, PCM/WAV and image bytes are unchanged.
  Real tests, test doubles and synthetic-host labels remain truthful.
- Final Cody validation on 2026-10-04: ESP-IDF 6.0.2 Debug/Release clean and
  incremental builds and size passed with eight jobs. App binaries are 3,612,288
  and 3,481,856 bytes respectively. All 21 focused host/static checks passed,
  including 32 Alpha tests, 15 response tests, native raster/runtime contracts,
  seven simulator groups, active-profile dependency/legacy closure, whitespace
  and a 99-file changed-secret scan including local checkpoints. Eight host
  captures were visually reviewed (four rotations, three boot states, greeting).
  Native checks explicitly used Windows Python/MSVC; initial environment-selection
  failures remain recorded. PDF binary attributes correct Git classification;
  document bytes were not changed. A separate validation correction aligns the
  Debug scene's brows with its declared Calm preset (`ef184a57`).
  Evidence: `artifacts/build-validation-evidence/journey-20261004/`.
  Global profile validation still fails on the missing `display-touch` canonical
  lock; wake-word locks are also unresolved. No physical device, phone, flash,
  LAN, CCP install/end-to-end session or runtime memory soak was validated.
- Created the 14-page English Codex Connection Atlas with three ImageGen
  concept scenes, circular previews, visual flows and handwriting space on
  every page. All pages were rendered and visually reviewed; the PDF is mirrored
  in Obsidian and `docs/Cody/`. Existing atlases remain unchanged. Document
  review does not validate runtime/LAN/device behavior.
  [Artifact record](docs/planning/2026-10-04-codex-connection-atlas.md).
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
  Done footer until an explicit row tap and no first-run Back. At checkpoint
  `ce6ac300`, screen 15 still remained gated on a future real Codex/PC producer;
  the later CCP checkpoint below replaces that gap. Added visually reviewed A04
  pages to both append-only atlases. Source/artifact review only; no build,
  simulator, automated test or device validation.
- Added isolated Cody's Codex Plugin (CCP) packaging with portable and Codex
  compatibility manifests, a standard-library Python MCP STDIO server, a bounded
  CCP1 UDP protocol and DPAPI-protected PC state. Firmware now owns a canonical
  non-secret Codex-link snapshot, six-character display-code pairing, retained
  PC identity and authenticated FIND/READY liveness. The PAIRED/CONFIRM/ACTIVE
  commit reports success only after both retained records exist. Screen 12
  cannot self-advance; verified pairing enters 13, Let's go shows native Idle
  for ten active seconds, 14 searches for the retained PC and only authenticated
  READY enters 15. This is commissioning/liveness, not the future Codex
  message/session transport. Source review only; plugin install/run, checks, build,
  simulator, firewall, LAN and physical-device behavior remain unverified.
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

## Cody service documentation — 2026-10-04

- Created the ten-page English CCP Service Atlas with discovery/pairing,
  persistence, 100-PC identity filtering, liveness and security-boundary diagrams.
  Added Umberto Giacobbi and hello@umbertogiacobbi.biz to all five Cody PDFs,
  visibly and in metadata. All ten new pages and 58 historical footers were
  visually reviewed; historical page bodies remain pixel-identical at 96 DPI.
  No runtime, LAN or physical-device validation was performed for this document.
  [Artifact and preservation record](docs/planning/2026-10-04-ccp-service-atlas.md).
