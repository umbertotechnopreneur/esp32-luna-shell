# Completed implementation and evidence

This is a compact checkpoint record, not a release certificate. Open validation
and implementation gaps remain in [TODO.md](TODO.md); deferred work is in
[planned-todo.md](planned-todo.md). Historical revision details remain in Git
and referenced architecture/artifact records.

## Cody and connectivity — 2026-10-02 to 2026-10-05

- Corrected source admission for the automatic Idle clock greeting: its retained
  Greeting SceneFrame no longer disables the main personality channel or
  petting. Explicit onboarding greetings and other routes remain excluded.
  Added bounded contact-start/exclusion/release logs for actual acquired input
  so scene suppression and incomplete/rejected strokes can be distinguished.
  The raw touch failure remains unresolved; driver acceptance/recovery has not
  been relaxed. Static integration reviewed; final validation/deployment pending.

- Investigated the reported caress freeze on device firmware `99342b7a`:
  the first capture attached after a retained touch `ESP_ERR_INVALID_RESPONSE`,
  while periodic firmware snapshots continued. Source review establishes that
  an acquisition error terminates the Cody worker and leaves later IRQs pending;
  it does not identify the original malformed packet. Reset captures booted to
  ready touch/display and later entered normal inactivity standby without a
  captured caress or panic. One live capture was interrupted when Codex restarted;
  it is incomplete evidence. The user reports a later stroke neither froze nor
  showed a heart; physical gesture recognition remains open in TODO.
  `device.ps1 check -Check cody-personality` passed 1,041 native assertions,
  122 controller parity frames and 118 host assertions. `cody-runtime` passed
  500 model/host ticks after its obsolete screenshot hit-target expectation
  was corrected to the already implemented hidden/noninteractive control.
  Initial check attempts retained a missing compiler environment and that stale
  expectation in their command logs; MSVC developer environment was restored.
  Checks ran through the public CLI. Evidence and command logs are under
  `artifacts/build-cody-companion-debug/evidence/caress-*`; current host reports
  are in `artifacts/checks/cody-personality/` and `artifacts/checks/cody-runtime/`.
  No firmware source fix, new build, new flash or simulator launch was performed.

- Integrated remote main `649cbc7f` into `feat/personality-icons`, preserving
  petting, happiness, symbol arbitration and the Studio DPI/renderer changes.
  Ordinary S3 Pearl rendering now uses the upstream SIMD kernel and transparent
  row/block skipping; automatic benchmark and detailed block profiling remain
  disabled. Resolved native profiling conflicts and added missing personality
  sources/includes to two host-check recipes. Static integration reviewed.
  Debug firmware `99342b7a` built through `scripts/device.ps1 build` with eight
  jobs and flashed through `scripts/device.ps1 flash` to the USB ESP32-S3 on
  COM10; both commands returned success, all written hashes verified, and
  esptool performed its final hard reset. App size is `0x3d2330` bytes
  (4,006,704), with 73% of the app partition free. Evidence is in
  `artifacts/build-cody-companion-debug/evidence/` as
  `personality-simd-main-merge-build-20261005.log` and
  `personality-simd-main-merge-flash-20261005.log`. No new tests, monitor,
  capture or simulator session ran. Startup behavior and physical performance
  of this combined image remain unverified.

- The first Studio UI `679fbda2` opened and responded after the authorized
  restart; the user reported blurred Windows text and ~100 ms host ticks, then
  closed it. Implemented explicit PMv2 before Tk, actual startup DPI point/pixel
  scaling and physical monitor work-area bounds, scanline edge buckets and sparse
  coverage composition with C-backed
  opaque copy and the original exact partial RGB565 blend. Visible inspector
  readouts are capped at10Hz, status at5Hz; model/gesture clocks retain per-tick
  advancement. Desktop redraw defaults to30Hz with an explicit15/25/30/60 limit;
  native firmware cadence is unchanged. Added pure host raster regression check source through the public
  CLI. Static review only for these corrections; no new suite, restart, benchmark,
  firmware build or COM access. No claimed FPS/CPU improvement. Different-DPI
  monitor transitions remain open.

- Created requested pre-UI checkpoint `b4a7a75` on `feat/personality-icons`.
  Refreshed the Windows inspector in source with a local Tkinter/ttk dark theme,
  navy/teal palette, Segoe UI, image-backed switches and slim sliders. Added a
  fixed studio header, grouped controls, independent tab scrolling, a wrapping
  expression grid, target-value readouts and a two-line diagnostic footer.
  Clean view/Face are initial desktop preferences; inspection remains explicit.
  Firmware, native character geometry and model rules are unchanged. Static
  integration review completed; subsequent startup is recorded above. No new
  suite, build or COM access. Earlier build/parity results below belong to the
  preceding UI revision.

- Integrated remote `origin/main` at `4d026d13` into `feat/personality-icons`.
  Preserved both personality/YAML work and upstream mouth/tongue, greeting,
  pairing and BLE/display DMA fixes. Resolved the worker conflict using main's
  atomic parked-frame observation while retaining happiness advancement,
  audio suspension and transient heart cleanup. Both Done histories are kept.
  Merge checkpoint `355c7f04` passed the authorized Debug build with eight jobs
  on ESP-IDF6.0.2: image `0x3d02e0` bytes, 73% application partition free.
  The authorized focused suite passed 1,041 native assertions, 122 native/Python
  controller parity frames, 32 host petting, 32 happiness and 54 wellbeing
  assertions, plus fourteen icon/audio contracts. Evidence is preserved under
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261005-merged-main/`
  and `personality-main-merge-debug-build-20261005.log` in its parent.
  Corrected the Windows simulator's obsolete blue boot PNG references to the
  versioned pearl frames; the Rig window opened and was responding. That
  host-only reference correction occurred after the suite and has startup
  evidence, not another suite run. No COM access, flash or hardware validation.

- Expanded build-time personality authoring to strict `cody.personality.v2`:
  permanent typed event/zone/metric/icon IDs, `caress = 1` for both anatomical
  zones, gesture/mood/channel/follow/anchor policies and global/per-cue SFX gates.
  Generated four immutable headers through the public CLI. Added priority
  arbitration (LowBattery900 > Affection200 > Charging100), smooth urgent
  handoff, per-identity repeat cooldown retained across hidden scenes and
  canonical low-battery hysteresis (enter15%, exit20%). Native/host fixtures
  and simulator sources are integrated; the corrected suite passed as recorded above.

- Authored commented dormant wellbeing policies with explicit session events,
  minute-based fatigue, screen break 60min/5min, movement 30min and eye 20min/20s/
  20ft. Recorded HSE/NHS/NEI sources. Water 60min is labeled a product default,
  since NHS specifies no hourly cadence. Engine and delivery stay disabled;
  pure native/host deadline logic is implemented without a runtime adapter
  or visible reminders. This does not change the sole active happiness MVP.

- The expanded focused suite compiled native portable sources on 2026-10-05
  and ran 1,041 native assertions. Two handoff expectations incorrectly checked
  rendered cue identity at zero opacity; corrected them to inspect the channel
  identity and require an empty frame. This first attempt did not reach Python
  parity or independent host fixtures; the corrected post-merge suite subsequently
  passed as recorded above. Host controller state is616B,
  including112B repeat timestamps, not a firmware stack result. Failed-run
  report/trace retained under
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261005-first-attempt/`.

- Added a source-reviewed single-value happiness MVP: canonical
  `assets/cody/personality.yaml`, strict schema and immutable generated flash
  rules (generation through `scripts/device.ps1` completed). Session happiness
  starts at 50, recognized carezze add 10, and each 60 awake seconds subtracts
  one point, with intentional 0/100 saturation and retained partial minutes.
  Sadness is derived as 100 minus happiness. Facial/motion bindings remain
  disabled. Native and synthetic host source expose values independently of
  heart occupancy; the passing expanded suite, build and simulator startup are
  recorded above. Physical hardware behavior remains unverified.

- Added a source-reviewed gentle petting experiment on 2026-10-05 in normal Main view:
  body-contained AboveEyes/BelowMouth zones, acquired-time/path/speed/gap
  recognition, one reaction/contact and six-second cooldown. An idle symbol
  channel admits a four-active-second Affection request; occupied channels
  drop it, explicit selection remains authoritative, and existing fades/audio
  admission/body drag remain in use. Native helper and Windows pointer preview
  share authored thresholds. Added focused native and independent host gesture
  fixtures; new gesture trace parity is not claimed.
  Needs/mood/session state exploration is documented separately and deferred.
  The expanded suite attempt is recorded above; no new firmware build,
  interactive simulator execution or hardware action has run.

- The earlier `cody-personality` suite passed before petting changes: 748 native
  assertions, 86 native/Python parity frames and fourteen icon/fourteen audio
  contracts. Preserved evidence:
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261004/report.json`.

- Configured and built the personality implementation `2518f83a` as Debug with
  ESP-IDF 6.0.2, `scripts/device.ps1` and eight jobs. Compilation/link succeeded;
  binary is `0x3c5870` bytes with 73 percent application-partition free. Evidence:
  `artifacts/build-cody-companion-debug/evidence/personality-icons-debug-build-20261004.log`.
  Authored a focused `cody-personality` host suite covering actual controller,
  finite following, Sleep/pause, dirty raster, native/simulator parity and icon/
  sound asset contracts; corrected `cody-native` source linkage. The suite later
  passed as recorded above; new petting tests await fresh command-specific
  approval. No device upload/access for this branch.

- Extended the personality channel to Main/full-size Sleep only, with one latest
  target, 300-ms stability, 400-ms fade in, 2.5-second hold and 600-ms fade out.
  Semantic Sleep has explicit priority/release; device invalidity clears false
  facts. Configurable face anchoring, bounded geometric following, fixed chrome
  exclusions and explicit blocked-layout status replace adaptive relocation.
  Simulator source mirrors the channel. Created fourteen original 16-kHz mono
  PCM SFX (103,360 sample bytes) and a 12.33-second audition reel through
  `scripts/device.ps1 generate -Generator cody-personality-audio`. Completed
  frames trigger an optional platform-owned worker with busy drops, expiry,
  8-second cooldown and quiet/standby gates. Asset generation and static review
  only; no listening, checks, builds, simulator or hardware validation.

- Implemented the isolated `feat/personality-icons` worktree from `290b6da3`:
  fourteen standalone ImageGen manga groups, deterministic non-reserved shared
  palette indices with separate alpha coverage, individual indexed PNG/bin
  assets, exact crop/component/hash provenance and internal-flash tables.
  Generation ran through `scripts/device.ps1 generate -Generator
  cody-personality-icons`; payload is 115,200 bytes plus descriptors/code.
  Native rendering uses one selected typed cue, every existing size except
  Micro, bounded above-body admission, 85-ms delayed position/squash and old/new
  frame damage. Session narrative/device selectors keep device truth canonical.
  Simulator source mirrors the feature and labels host device-cue previews
  synthetic. Only generation and static source/artwork review were performed;
  compilation, automated checks, simulator and physical behavior remain open.

- Integrated the selected 16-pixel SIMD and transparent row/block skips with
  current main's pairing, standby, mouth and boot-audio changes. Ordinary S3
  Pearl rendering now starts on SIMD in Debug and Release; portable host
  rendering starts on scalar. Benchmark autostart/detail are disabled, normal
  placement is retained, and RGB565 composition is unchanged. Added a host
  assertion for initial backend selection. Portable native, SIMD-model and raster
  benchmark contracts pass after updating the installation-scene expectations to
  main's current heading and scale. Combined Debug and Release builds pass at
  `e714737` through `device.ps1` with eight jobs; both have benchmark autostart
  disabled. Build logs, image hashes and portable reports are retained in each
  mode's `artifacts/build-cody-companion-{debug,release}/evidence/` directory.
  The combined image was not flashed or physically timed; earlier fixed-route
  measurements below belong to the SIMD worktree benchmark image.
- The same 16-pixel SIMD and row-coverage path completed fixed 40-second device
  windows at smaller scales. At 75%, 975 frames submitted 35,256 pixels each:
  mean 33,888 us, p95 34,512 us, maximum 35,147 us, and zero budget misses.
  At 50%, 975 frames submitted 15,600 pixels each: mean 21,834 us, p95
  22,335 us, maximum 23,179 us, and zero misses. These are SIMD-only scale
  measurements, not new C/SIMD A/B comparisons. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-coverage-75.log`
  and `cody-simd-coverage-50.log` beside it. The authored scale and flashed
  firmware were returned to 100%; the restored app payload matches the
  measured 100% payload, with only image metadata/checksum bytes differing.
- The fixed 100% Pearl Mist S3 run with 16-pixel SIMD blocks and generated
  per-row coverage completed 766 frames at 63,000 submitted pixels per frame:
  mean 50,715 us, p95 51,463 us, jelly mean 30,736 us, and 766/766 frames over
  40 ms. Coverage skipped 14,544,808 pixels (18,988 per frame) before packing;
  host scalar/SIMD-model parity and full generated coverage checks passed. The
  frame mean is 4,897 us lower than the 16-pixel zero-skip version and 6,235 us
  lower than the fresh C control. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-coverage-100.log`.
- The intermediate 16-pixel all-zero skip completed 699 frames: mean 55,612 us,
  p95 56,374 us. Enlarging blocks to 64 pixels regressed to 59,794 us mean and
  p95 60,519 us over 652 frames because fewer transparent blocks were excluded.
  Replacing constant `/255` in RGB565 composition with an exact shift/add form
  preserved host pixels but regressed to 51,497 us mean and p95 52,299 us over
  752 frames, so that source change was removed. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-16-zero-skip-100-2.log`,
  `cody-simd-64-100.log`, and `cody-simd-coverage-div255-100.log` beside it.
- Intrusive SIMD profiling at 100% completed 508 frames. Per-frame jelly mean
  rose to 57,052 us because 3,952 blocks triggered many clock reads. Its
  accumulated segments divided by 508 were approximately preparation 19,673 us,
  interpolation 6,947 us and RGB565 composition 19,216 us; the difference from
  jelly total is unassigned instrumentation/loop time. These numbers rank work,
  not uninstrumented costs or FPS evidence. The 64-pixel experiment above
  demonstrated why larger packed blocks are unsuitable for this artwork.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-simd-detail-100.log`.
- Fresh C control on the same 100% route: 684 frames, mean 56,950 us, p95
  57,634 us, jelly mean 37,106 us; all missed 40 ms. Against the 697-frame SIMD
  run below, SIMD reduced frame mean by 1,104 us (1.94%), p95 by 1,139 us
  (1.98%) and jelly mean by 1,226 us (3.30%). Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-scalar-100-fresh.log`.
  This is a measured fixed-pose gain; it does not meet the 25 FPS budget.
- Measured the S3 `simd` kernel on the same fixed 100% Pearl Mist route for 40
  seconds: 697 frames, 63,000 submitted pixels each, frame mean 55,846 us and
  full-run p95 56,495 us. Jelly mean 35,880 us; 31,434,700 pixels used SIMD,
  139,400 used scalar tails and 11,567,412 skipped all-zero footprints. All
  697 frames exceeded the 40 ms budget. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-100.log`.
  The fresh C control above confirms a small gain for this fixed scene.
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
- Real pairing 07 now allows 90 awake seconds without genuine activity; 05/06
  retain 30 seconds. Its scene standby override is also 90 seconds and refreshes
  on canonical progress, without synthesizing touch or changing saved settings.
  Backend deadline remains five minutes. Debug `0a4f1b87` built with eight jobs,
  flashed to COM10 with verified hashes and booted with the matching ELF identity.
  Source assertions updated but not run; physical 90-second behavior is pending.
  Saved Wi-Fi credentials reconnect with DHCP and SNTP in this boot capture.
  SD initially timed out before FAT after reinsertion; after the owner's full
  power cycle, the snapshot reported mounted=yes and card-response=verified on
  attempt 1, without formatting. The owner confirmed a saved PWR screenshot.
  Ordinary idle standby completes at 64.697 s with BLE budget rearm.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-0a4f1b8-deploy.md`.
- Captured the intermittent wake complaint: the earlier renderer had already
  stopped with INVALID_ARG before standby was requested; the first terminating
  operation was outside the capture. After an explicit reset, `bb8fd327` entered
  standby at 64 s and woke on touch at 179 s with wireless ESP_OK, confirmed by
  the owner. No new renderer termination was captured. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-bb8fd32-renderer-repro-20261005.log`.

- Corrected false monolithic BLE admission: SDK 51d9dfd EMI uses separate
  allocations; `0x7800` in the inspected routine is an address offset, not a
  malloc size. Preserve the 34,816-byte aggregate budget with four 8-KiB chunks
  and one 2-KiB chunk, all-or-nothing acquisition and real SDK error handling.
  No SDK patch/allocation hook or Wi-Fi buffer reduction. Debug `bb8fd327`
  built with eight jobs and flashed to COM10 with verified write hashes.
  The owner confirmed the real pairing QR. The passive log records healthy
  BLE wake, provisioning startup, clean inactivity closure with budget rearm,
  then a second provisioning startup without `ESP_ERR_NO_MEM`. Phone credential
  transfer/DHCP remain unverified; no tests or simulator were run.

- Lifetime BLE DMA ownership: one static 34,816-byte reserve survives standby
  and pre-controller failures. After actual SDK teardown Cody parks display DMA
  and rearms the budget; cleanup and reservation errors have separate outcomes
  and canonical pairing fields, so clean Retry/known-off standby remain valid
  if a future budget cannot be restored. Limits and Wi-Fi configuration stay
  unchanged. Debug `a19e0093` built/flashed successfully, but physical Pair
  reproduced `0x101`. Its complete boot capture proved SDK startup succeeds;
  after first clean standby teardown the monolithic rearm fails with 97,443
  bytes free and largest 21,504. The chunked correction above follows this evidence.

- Captured explicit Pair failure on main `a154b677`: the 34,816-byte internal
  DMA hold failed with 70,955 bytes free but largest block 21,504, before manager
  startup or session QR. Explicit Pair now drains/parks display DMA before BLE
  teardown/admission and keeps the gate through controller startup; every path
  attempts restoration and preserves real errors. Hold/headroom limits and Wi-Fi
  configuration remain unchanged. Pre-controller failures retain an existing
  lease; no new lease is allocated by Cancel. Frame admission captures intentional
  parking under the lock to avoid terminating Cody after a fast failed handoff.
  Debug `7e45bf32` built and flashed with verified write hashes. The following
  passive monitor reproduced allocation failure after the drain: 96,243 bytes
  free, largest 21,504. No panic/reboot was observed; the standby path discarded
  the earlier reserve. This led to the lifetime-ownership correction above.
- Debug `143fdb22` built with eight jobs and flashed to COM10 / ESP32-S3
  `a4:cb:8f:db:42:d8`. Esptool verified written data and reset the board.
  Application: 3,755,536 bytes, 75 percent of the app partition free. Includes
  greeting C, regenerated mouth/tongue/QR assets and corrected Retry teardown.
  No tests, simulator or post-flash monitor ran; physical behavior remains
  unverified. ELF/BIN hashes and transcript are preserved in
  `artifacts/build-cody-companion-debug/evidence/cody-143fdb22-deploy.md`.
- Prepared three private vocal greetings with ffmpeg and explicit read-only
  source recipes. The owner selected C (`cody-hello-murmur`) for the boot cue;
  A/B and chime-only remain configurable. Shared authoring helpers pin source
  and rendered identities, while runtime composition retains the original
  1.3-second chime plus 100-ms gap. The selected combined asset needs 90,412
  PCM bytes at 22,050 Hz (about 2.05 seconds); no new device decoder/worker or
  canonical-clock/quiet-policy changes. Local asset generation and independent
  static review completed. On 2026-10-04 the owner confirmed purchased perpetual
  commercial-use licences for the SFX packages; the selected PCM/WAV and matching
  manifest are now included in the asset checkpoint. Debug build/flash completed
  as `143fdb22`; physical acoustics and updated host checks remain unverified.
- Mouth source refresh: widened the shared lip controls 18 percent, reused the
  existing breathing phase for opening/width motion and added a coral tongue
  clipped against the evaluated mouth. Native and host coverage intersect the
  same geometric spans. Fixed budgets are 200 points, 62 cubics, 16 paths and
  124 debug tangents; existing raster arrays/worker stack are unchanged.
  Source implementation, independent review and rig/jelly regeneration are
  complete and deployed as `143fdb22`. Physical appearance and timing remain pending.
- QR copy now distinguishes installation (`Scan to install`) from the live
  session (`Scan to pair`, `Scan me in the app.`). Install/session routes share
  compact cloud placement; actual 40/25-pixel font cells govern title/caption
  separation. Existing QR quiet zones, footer bounds and white theme remain.
  Scene regeneration is complete; physical checks are pending. The Obsidian planning
  atlas has corrected page 23 and a dated final errata page 27; other original
  pages are preserved. Backup and visually inspected page renders are retained
  under `artifacts/build-cody-companion-debug/evidence/qr-atlas/`.
- Passive `9fde2b48` logs confirm the reported graphical wake works even after
  the BLE reservation fails (34,816 bytes requested, largest DMA block 21,504).
  Subsequent Retry contacts fail during Cancel cleanup, with primary result
  `ESP_OK`: teardown incorrectly reserves memory for a future session.
  Removed that future admission from teardown; next Start retains strict
  reservation/preflight and its original error. Cancel now retains the failed
  attempt's cause alongside the actual cleanup result, allowing clean Retry
  navigation to 05. Source integration reviewed and deployed as `143fdb22`;
  physical Retry remains unverified. Capture starts after the original Pair Cody failure, so its
  exact startup stage is not established:
  `artifacts/build-cody-companion-debug/evidence/cody-9fde2b48-pairing-before-reset.log`.
- Passive monitoring of `d766e08c` recorded wake input followed by failed BLE
  DMA reservation (34,816 bytes required; largest block about 21-22 KiB), then
  rollback to standby. A later Interrupt-WDT backtrace enters the log mutex
  from CST9217's point logging inside a critical section. Preserved evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-d766e08c-wake-failures-20261004.log`.
  Removed that logging in the reproducible vendor patch. Standalone graphical
  wake now accepts a radio failure only after confirmed shutdown, preserves the
  original error until verified recovery and still rejects uncertain cleanup.
- Added a bounded 30-second awake-inactivity policy for 05/06/07 returning to
  Welcome 04 without replaying boot. Real touch/pairing progress refreshes it;
  animation pause does not stop the timeout, standby does. Detached cancellation
  blocks new pairing and rejects late route promotion. Returned Welcome waits
  for a fresh tap, keeping the normal typewriter presentation.
- Boot audio now waits for verified canonical time instead of permanently
  suppressing that boot's cue when RTC is invalid. A single temporary 4 KiB
  PSRAM worker respects quiet hours/active state, with pending-only 1 Hz reads
  and real playback/error publication. Static integration review found no
  remaining actionable issue; physical confirmation remains open.
- Built/flashed `9fde2b48` on COM10 (Debug, eight jobs, no test runs): app
  3,721,616 bytes, 75% partition free; all written hashes verified. Matching
  ELF SHA256 starts `c572bfaefa`; ELF/build/flash evidence is preserved under
  `artifacts/build-cody-companion-debug/evidence/cody-9fde2b48*`. This image
  includes the wake/touch/inactivity/deferred-audio changes and raster stage 1;
  successful writes do not establish physical wake, audio or performance behavior.
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
- Raster optimization stage 1: all-zero bilinear footprints skip
  the original three packed lerps; zero-alpha samples preserve destination bytes
  without load/swap/store. Other samples retain their arithmetic. Added separate
  jelly/vector microsecond timings within artwork and reference contracts for
  transparent prefixes, fractional samples, backgrounds, byte order and guards.
  Included in the `9fde2b48` Debug build/flash; no tests or device comparison
  ran. An extra nonzero-footprint branch may offset the saving.
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
  validation. Archive-cue auditions are not firmware assets; their perpetual
  commercial-use licences were subsequently confirmed by the owner on 2026-10-04.
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
