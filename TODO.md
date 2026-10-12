# Active work

Open gates only. Completed implementation and dated evidence live in
[done.md](done.md); deferred ideas live in [planned-todo.md](planned-todo.md).
Execution follows [AGENTS.md](AGENTS.md): documentation editing does not authorize
tests, scans, generators, builds, simulators or physical operations.

The 2026-10-11 Cody Debug wake-word build, personality/runtime checks, 34 CCP
simulator cases and five character Studio cases passed. These results do not
cover the later Codex acting/power-cue increment, physical behavior, every profile
or the entire remaining matrix below.
Stable IDs identify work; their order is not a domain identity.
The battery/audio source checkpoints are on `main`; the USB flash evidence is
prepared on `codex/voice-firmware-flash`. The daily Codex Standard cache, Home warning/carousel
and Model & Context badge are implemented; final validation remains open in
CCP-07. No rates, dollar amounts or session-cost estimation are sent to Cody.
Latest firmware `cody-135-gfda9e7e3-dirty` was built and flashed on COM10 on
2026-10-12 with five image hashes verified, the unchanged two-OTA-slot table and
NVS excluded. App 7,272,544 bytes; 1,952 bytes remain per wake-word slot. See
done.md for passive serial observations and corrected startup/cadence failures.
Physical interactions, stack headroom and host/simulator cases remain open.

## Cody power and pretrained activity observer - 2026-10-12

- [ ] **POWER-OFF-01 - Final firmware and physical evidence.** Source implements
  external-power inactivity inhibition, exclusive short-PWR standby/wake,
  validated three-second farewell/PMU shutdown, owner drains and visible rollback
  errors. Confirm battery-only 60-second/scene timing, USB/full/absent-battery
  behavior, first hold after boot, error recovery, actual configured power-on
  duration, complete frames, Sophia/touch/shake wake and whole-board off behavior.
  Hardware emergency hold can still cut power independently after 10 seconds.
  Customized PMU USB/battery/IRQ startup sources prevent a Power-only restart
  guarantee across all supply conditions. Do not claim physical behavior from a flash.
- [ ] **ACTIVITY-01 - Pretrained observer evidence.** Original fixed HAR weights
  were converted with ESP-PPQ 1.3.11 for ESP-DL 3.3.13, W8A16; no training. Integration
  uses actual timestamped 20-Hz windows, original labels and an experimental
  Activity-panel readout. The dependency lock and 172304-byte model are integrated;
  the application now builds and flashes within the unchanged OTA slots. Physical
  inference is observed after correcting startup DMA pressure and separating the
  acquisition core from voice. Some real cadence gaps still reject windows;
  improve continuity only from measured owner timing, never synthetic samples.
  Original preprocessing was recovered exactly against all 107 supplied calibration
  tensors; sensor/placement domain, accuracy, energy cost and stack headroom remain
  unverified. The 1952-byte OTA margin also limits further firmware growth.
- [ ] **POWER-ACTIVITY-CHECKS - Remaining validation.** Power/source/production-owner,
  native runtime/raster, console, Studio, CCP/reference simulator, selected Cody
  profile and snapshot checks passed after the user's continuous authorization.
  Physical input/rail evidence, compiled stack frames/headroom and final changed
  source scans remain open. Unselected display-touch/full-wake-word locks are absent
  in the whole-catalog check; do not report those variants as verified.
- [ ] **BATTERY-ENDURANCE-01 - Persistent battery-only timing.** User requested
  start, last-alive and first-wake pings for actual battery operation without USB.
  Preserve interrupted-run evidence across boot with unknown end cause; do not
  invent the time at which battery power disappeared. Implementation adds a bounded
  CRC-protected 112-byte NVS journal, canonical source validity, ordinary/failed
  shutdown markers and low-rate progress through the existing owner without an
  additional wake lease. Twelve real-service host scenarios and real settings-owner
  bounds/error/seal tests passed. Target size, flash, stack frames/watermarks and
  actual unplugged-battery duration remain pending.

## Reproduced device faults

- [ ] **UI-ACTIVITY-02 - Connection and activity presentation evidence.** Source
  adds silent discovery faces/lens, ten-minute search and three-minute Working
  standby, a four-visible-second valid-battery wake prelude, Model-only
  Cheap/Reasonable/Expensive chip, current tool identity and expanded technical
  panels with canonical IP/firmware identity. The integrated Debug wake-word/activity
  image is compiled and installed; visual geometry, interaction and physical
  stack-headroom evidence remain pending.
- [ ] **CCP-USAGE-01 - Authoritative Desktop usage source.** App Server documents
  `thread/tokenUsage/updated`, but current Desktop hooks provide no owned stream
  for this exact chat. Reasoning, context and token fields remain unavailable.
  Never infer last-turn usage, execution model or bills from price classes,
  transcripts or another Codex session.

- [ ] **FAULT-04 - Reported Codex-choice reboot loop.** The 12 October passive
  COM10 capture of `cody-114-g33377507` retained reset reason 4 (`ESP_RST_PANIC`)
  from the preceding boot, but no panic/backtrace occurred in the 30-second
  window or the later 60-second window (device uptime reached 277.954 seconds).
  Recover the first panic with the matching ELF while the actual choice occurs;
  the trigger and cause remain unconfirmed. Observed companion minimum free
  stack reached 2,736/12,288 bytes. Keep the original ELF and captures in
  `artifacts/build-cody-companion-wake-word-evidence/reboot-20261012-33377507/`.
- [ ] **UI-ACCENT-01 - Animated activity icons.** Source replaces working dots
  with three counter-rotating gears and waiting/compaction accents with a fast
  two-hand clock. Validate the final build, visible animation, partial repaint
  retirement, rotations and device stack headroom when explicitly authorized.
  The changed icon source is included in the current compiled/flashed image;
  simulator and visible physical animation remain unverified.

- [ ] **BATTERY-01 - Quiet battery notices.** Firmware source implements free-Home
  admission after ten seconds, four-second caption/manga dwell, 20/10-percent
  warnings, charging-entry and measured-full events. Validate canonical stale/error
  removal, startup/charging transitions, periodic reminders, route/touch deferral,
  caption/artwork clearance at all rotations, compiled frames and physical stack
  headroom. Studio automatic notice/caption parity remains pending; manual power
  studies keep their factual selector. The integrated Debug/wake-word image was
  built and flashed on 12 October; battery behavior and host cases are unverified.

- [ ] **FAULT-01 - SD warm-reset initialization.** Capture SD VDD and CLK/CMD
  across the known working-card-to-reset transition. ACMD41 stays busy before
  FAT; reinsertion restores access, while memory-only SDIO exclusion and explicit
  400-kHz probing did not fix it. Preserve original errors and the official
  one-bit GPIO2/1/3 baseline, external pull-ups and `SDMMC_SLOT_NO_CD`.
  No more unchanged reset loops/card swaps, implicit formatting or shared-rail
  cycling. Keep TTS blocked storage and Release endurance open separately.
- [ ] **FAULT-02 - CST9217 and caress freeze.** Identify the first rejected packet
  or terminal transition. Verify legitimate two-contact reports, D000/AB ACK,
  normal-mode restoration, malformed fifteen-FF-byte isolation, invalid snapshots,
  gesture interruption and re-admission only after a real release. Then observe
  a continuous eligible stroke above the 300-ms minimum and Affection SFX.
  Existing guards/logs must distinguish zone exit, speed and acquisition gaps.
- [ ] **FAULT-03 - Intermittent renderer stop.** Capture the first failing frame
  with a matching ELF. The checksum-validated `2bd0626` core identified infinite
  SPI bus acquisition; `c3dceaf` subsequently completed 240 + 90 + 90 s and 75
  periodic frames without QSPI/watchdog failure. Physical pixels, touch wake and
  long stability remain open. The older `bb8fd327` missing 204-314 s interval
  and later good captures cannot establish the trigger or a general fix.
- [ ] **FAULT-04 - Separate peripheral/USB errors.** Diagnose codec/PMU I2C errors,
  I2S disable-before-enable, diagnostic SD heartbeat contention at 177.060 s and
  logger truncation/loss independently of pairing success. Explain the historical
  130-s capture closing after 43 s and the later power capture's ~16-s USB loss;
  later completed captures do not identify either host/USB interruption cause.

## CCP, hooks and companion

- [ ] **CCP-01 - Coordinated security rollout.** Security implementation is
  retained in dedicated checkpoint `bbb11e10`. Matching READY/COMMITTED plus
  TECH v2 firmware from `8cfb6ce3` was built/flashed on COM10 on 2026-10-11.
  Confirm the active paired-PC service is CCP 0.6.4 for runtime observations.
  Inspect affected compiled frames and measured stacks; run prepared security
  regressions with fresh suite authorization. Observe real pairing, retained recovery, packet/ACK loss,
  broker restart, consecutive questions and notice routing; tune bounded ingress
  from measured bursts. Older 0.6.0/0.6.2 installation evidence is historical.
- [ ] **CCP-02 - Native hook delivery.** Restart/review modified hooks in `/hooks`
  and follow a genuine prompt. Diagnose real turn-correlation rejections without
  weakening stale-event admission. Verify autoload, latest-prompt selection,
  multi-chat leases, broker restart and LAN recovery. No fabricated activity,
  transcript inference or manual trust edits. Parser discovery is not execution.
- [ ] **CCP-03 - Physical decisions and delivery.** Observe screens 26/27/31,
  reversible selection, Send, received versus recipient-confirmed delivered,
  original TTL, cancellation, offline retention and terminal EVENTACK before
  the next question. Review read-only PR/check screens and notices, physical
  notification wake and audio. PC/SIMULATED reset tests do not prove physical
  restart or factory reset; those require their distinct current-message intent.
- [ ] **CCP-04 - Panels, mode and gestures.** Review the four-fact technical
  panels with Cody at (233,115), scale 50, and the separate five-row Options view.
  Cover strict Desk/Away persistence/failures, missing metadata, current chat/
  ACTIVITY revision, approval priority, genuine compaction leases/end sequence,
  Home visibility, paused released-tap cycling and deliberate shake versus walking.
  The character's 2026-10-11 synthetic tests cover their named cases only;
  complete panel interaction, physical rotations and stack headroom remain open.
- [ ] **CCP-05 - Character on Cody.** Observe gaze in all cardinal rotations,
  recognized-caress warmth, positive-only mood and exact matching compaction release.
  Preserve protective closure, root motion, audio mouth, source-gap rejection and
  Debug/preset/setup isolation. Inspect nested compiled frames/device stack minima.
  Expression-debug screen 38 remains a separate existing task.
- [ ] **CCP-06 - Codex acting final validation, 2026-10-11.** Regenerate the
  native Studio driver, then run the prepared runtime/personality/Studio cases
  with fresh command-specific authorization. Cover Thinking/Editing/Running
  accents, one attention double blink, exact-turn reply/interruption, repeated
  frames, source gaps, hidden routes/pause and the real PC approval view. Verify
  automatic Cody low-battery/charging cues, fractional threshold boundaries,
  missing percentage/presence and immediate source-expiry invalidation. The
  integrated Debug wake-word firmware from `8cfb6ce3` is built/flashed; inspect
  compiled frames and physical stack headroom
  for the 208-byte director and display-owned status projection. Earlier test
  results cover the preceding revision. Python additions are platform-neutral;
  the existing Windows credential/launcher boundary still needs a separate port.

## Cody routes and presentation

- [ ] **CCP-07 - Daily Codex price bands and Home visibility, 2026-10-11.**
  Portable Python daily cache, strict public Standard table, exact model IDs,
  durable once-per-day attempts and authenticated TECH v2 are implemented.
  Native Model & Context displays the band without displacing compaction or a
  measured context row. Home project/branch/model rotation and a persistent
  green/orange/red/gray band chip are integrated with compaction priority,
  partial-frame damage and native Studio previews. Obtain fresh named authorization
  for CCP, native aging/rendering, Studio/simulator and firmware runtime checks.
  Prepared cache, signed transport, expiry, raster and Studio cases have not run.
  Debug wake-word firmware from `8cfb6ce3` was built/flashed on COM10 on
  2026-10-11. Regeneration, compiled frames, device stack headroom and actual
  badge/compaction/tap/shake behavior remain unverified; the flash alone does
  not establish the active PC plugin version or reviewed hook execution.

- [ ] **UI-01 - Setup and retained onboarding.** Exercise first-run versus
  retained pairing, missing/saved Wi-Fi, reconnect/DHCP, unavailable/stale data,
  unpaired Yes/No, No/Later without NVS/new CCP work, forced-development replay,
  ordinary standby and real Deep-sleep boot. Clock routines must not displace setup.
  Review 09 -> 10 -> 36 -> 11 -> optional 12 -> 13 -> 16 -> 14 -> 15,
  row confirmation/Done gates and explicit Codex Kconfig admission.
- [ ] **UI-02 - Setup escape and holds.** Verify a stationary 1500-ms character
  hold, 12-pixel motion cancellation, 250-ms sample gaps and contact across wake,
  using fresh visible samples only. Welcome tap/hold and resting 05/06 must reach
  the same menu without bypassing active work/cancellation. Check busy routes,
  real release, rotations and preservation of Wi-Fi/pairing.
- [ ] **UI-03 - Pairing transitions and timers.** Exercise 05/06/07/08/09,
  immediate 07 entry, pending/ready repaint, error/online gates, cancellation
  readback, Retry returning to 05, first-boot write/readback and Debug-only 39.
  Review 30-awake-second route closure for 05/06, 90 s for real pairing, independent
  60/90-s standby overrides and the 180-s screen-12 code window starting at copy.
  Preserve settings, paused clocks, late worker results and real teardown errors.
  The 27.790323-s PBKDF2 capture proved code generation; later size/timer changes
  and the subsequent malformed-touch report still need physical review.
- [ ] **UI-04 - Approved controls and QR.** Review semantic paired cards,
  two-plus-one actions and icon-left capsule, two-line labels, title/body-complete
  control reveal and visible-only touch admission. Verify Setup/Network/Options/
  Look/Back paths, white-theme contrast, local circular chords, quiet zones,
  actual installation/session QR and real phone scans at every rotation.
  Reconcile 05/06/07's existing lateral-navigation suppression with the canonical
  Back rule when a previous route exists; technical panels retain their exception.
- [ ] **UI-05 - Fonts and native control raster.** Generate 4bpp scenes from the
  licensed source only when requested; committed glyphs remain 2bpp. Execute the
  new four-bit/coverage/tile/byte-order definitions and inspect compiled costs,
  native pixel parity, legibility and options/look scrolling on Cody.
- [ ] **UI-06 - Asynchronous Look settings.** Cover save/readback failure/retry,
  busy Back/Done, route reset during write, rotated touch, canonical errors,
  stable skin/palette IDs and bounded memory over repeated entries. Host
  persistence faults are covered; device races/leak soak remain open.
- [ ] **UI-07 - Debug controls and screenshots.** Validate PWR single click after
  the 500-ms decision window, separately acquired double-click reset, long holds,
  acquisition failure and Release exclusion. Review 3/2/1 timing, overlay-free
  atomic BMP, SAVING/SAVED/FAILED, status/HUD validity and hidden touch targets.
  Cover absent/removed/full SD, logging contention, Performance Mode, standby,
  rotations and PSRAM failure. Review graphite Back, fades and swipe/drag/scroll
  ownership on both Cody and Luna's separate input ports.

## Character, audio and performance

- [ ] **ART-01 - Petting and idle behavior.** Observe 300-ms caresses, independent
  gain while hearts are busy, heart interruption and stress-dependent eye taps.
  Cover upper/lower zones, native/host trace parity, cooldown, pause/wake and
  smoothness. Verify 20-40-s nonrepeating Yawn/Whistle/Greeting cycles, visible
  Stressed breathing and touch/hold/stress/route cancellation. Dormant wellbeing
  has no runtime adapter; richer facial/persistent mood stays deferred.
- [ ] **ART-02 - Manga channel.** Review all fourteen 256-pixel symbols,
  scale-dependent extent/anchor, upper-body overlap, circle/status/Back clearance,
  Main/Sleep eligibility, Micro suppression, bounded follow and old/new erasure.
  Confirm 400-ms fades, stable selection, completed-frame SFX, busy drops,
  cooldown/expiry, quiet/zero-volume/standby and idle-mute restoration. Measure
  complete frame, DMA, CPU and stack cost; static area counts are not performance.
- [ ] **ART-03 - Face and response.** Review all 34 typed face controls, rounded
  lip/eye curves, tongue clipping, wider breathing mouth, Whistle minimum width,
  cloud sizes/pearl warp, contour response, independent blink/breath, eye taps,
  drag and exact Debug presets. Verify canonical charge/microphone/motion axes,
  stress/fatigue recovery, pause/rotation and stale/error gating physically.
- [ ] **AUDIO-01 - Current boot and action sound.** Listen to BootRise and the
  sixteen current Codex cues, with true completed-frame/semantic admission,
  trusted-clock/quiet/volume policy, no standby-wake replay and retained errors.
  Historical greeting C remains an audition rather than the current default.
  Review microclick reveal deltas without scroll/replacement/resume clicks,
  excluded-audio profiles and cancellation/codec contention. Digital preparation
  and PCM success do not establish speaker loudness or perceived gentleness.
- [ ] **AUDIO-02 - Windows playback.** Listen to tap, visible manga identities,
  whistle, greeting glyphs and generated boot cues through the host owner.
  Verify cancellation and visible playback errors. Compiled DLL/interactive
  startup evidence does not prove audible playback.
- [ ] **BOOT-01 - Branding handoff.** Review VibeWare's complete 42-frame/3500-ms
  cycle, 630,600-byte resources, original dwell after real readiness, Cody title/
  subtitle, startup failures and DMA parking, first/returning boot and rotations.
  Confirm ordinary standby/deep-sleep presentation policy, Luna compilation/
  linked memory and separately requested previews/packaging.
- [ ] **PERF-01 - Native frame budget.** Establish a truly static normal-route
  baseline and measure jelly, face, preparation, RGB565 composition, payload,
  transfer/DMA wait, stage distributions and CPU. The historical 100% benchmark
  still missed 40 ms. Preserve exact pixels and one display owner; confirm compact/
  full parity, transparent skips, 50/75/100% timing, non-Pearl and benchmark-off
  behavior, touch/PWR/Sleep interruption, alignment/tails and logging overhead.
  Measure current-image stack/radio headroom, slow DMA and CPU0-radio/CPU1-render
  behavior under pairing, HTTP and BLE load. Historical branch timings stay in Done.
- [ ] **PERF-02 - Host batching and preview.** Measure ABI-2 raster/snapshot
  costs and memory rather than infer gains from fewer calls. Cover atomic import,
  publication failure, metadata/redaction/aging and sparse-coverage oracle in the
  still-needed simulator/native suites. Review DPI, controls, tabs and gesture
  feel only when a restart is requested; different-DPI monitor transitions are
  unimplemented. Reuse the 2026-10-11 driver/runtime results for their exact scope.

## Optional BSP services

- [ ] **STORE-01 - Logger and USB readback.** Establish linked/map/size evidence
  for the estimated ~68-KiB PSRAM/logger recovery. Cover queue pressure without
  mirror shutdown, boot replay/archival failure, loss timestamps, unavailable SD,
  disabled mirroring, optional failure without reset and measured watermarks.
  Verify current/previous log CRC/identity, interrupted USB, busy COM, passive
  attach, explicit reset, rotated/remounted files and standby/readback ownership.
- [ ] **STORE-02 - Explicit SD lifecycle.** On a disposable card and with separate
  single-use formatting authorization, review cancel/format/progress/failure/
  recovery, removal/full/unformatted states and cleanup precedence. Repeat mounted
  Debug standby/wake and actual Release sleep; one 2026-10-07 Debug cycle passed
  with mount attempt 4 and write sequence 48 -> 96, without proving reset recovery.
- [ ] **TIME-01 - Trusted time and routines.** Verify atomic timezone/quiet
  settings, overnight windows/Rome DST, missing/corrupt values and neutral stale/
  unavailable state. Cover first-IP NTP failure/recovery, unset/oscillator-stopped
  RTC reaching trusted time, offset/rate before correction, write/readback,
  offline reboot/backup retention, greetings/title damage and deferred boot cue.
- [ ] **VOICE-01 - Recognition variants.** Resolve/review `full-wake-word` and
  establish three-model packing, 1-MiB partition/flash inventory and startup.
  The 2026-10-11 Cody Debug build does not prove Jarvis/Computer recognition.
  Cover the sole off flag, capture/input/output closure, voice CPU0 versus render
  CPU1, PSRAM-only AFE, external BSS/stack cache safety and 64-KiB internal reserve.
  Measure Sophia false positives, AEC during playback, settled-standby-only wake,
  old-event rejection, diagnostic/Performance Mode pause/resume, CPU/PSRAM/current
  and model/capture failures. Single-model Sophia wake was physically confirmed
  on 2026-10-07; full/Release and broader reliability remain open.
  The new Cody source consumer starts capture from awake/standby Sophia events or
  acquired double taps. Validate the derived Cody/Wi-Fi/Codex-only gate, 420-ms
  single-tap delay, failed/stale touch and rotation, route restoration, explicit
  sleep with retained wake-word microphone, microphone pause, waveform
  layout/shake-only cancellation, the 60-second countdown/PCM bound, partial-band
  redraws at every orientation, genuine-data fade carousel and actual 8-FPS CPU
  savings. Exercise ten-second PSRAM
  pressure, lost/duplicate/reordered/forged ACKs, fresh-discovery replay binding,
  wake-word tail and first-command preservation without a fabricated pre-roll,
  expiry, native errors and changed followed chats. Prepared `cody-voice-input`
  host cases have not run.
  Audio reliability changes also need the four-packet cumulative ACK/gap/replay
  cases, stale/unavailable native readiness, same-chat turn changes, approval ID
  collision, short CCP outages and uncertain delivery after END. The additional
  ciphertext window is component-owned (at most 4,352 bytes); verify native socket
  recovery and the 5-ms/minimum-one-tick service cadence under actual load.
  Revisit all callers after the bounded voice snapshot
  growth; inspect compiled AFE/CCP/render frames and measure stack/PSRAM headroom.
  Install plugin 0.6.5 with its optional audio dependencies and configure the real
  loopback desktop endpoint; verify native localAudio account/model access and
  native approval routing. This session did not install the modified PC plugin.
  Integrated firmware build/flash completed on
  12 October; no host suite, native delivery, simulator or post-reset capture was run.
- [ ] **TTS-01 - Storage and audible speech.** Resolve FAULT-01 before claiming
  SD synthesis. The prepared four-language card and corrected `58757005` boot
  did not establish an intelligible Italian phrase. Observe the one-shot boot
  diagnostic, bounded initial wait, real failures and diagnostic-only quiet bypass.
- [ ] **TTS-02 - Variant contracts and budgets.** Validate engine/task/data
  exclusion in ordinary firmware, metadata-only SD and English-only flash,
  isolated configuration/locks/flash lists, package/SHA rejection and UTF-8 in all
  four languages. Measure continuous PCM, CPU0 synthesis/CPU1 Cody or LVGL,
  guarded LVGL patch, PSRAM/stack/CPU, cancellation, normal quiet policy, codec
  lifecycle, standby and changed/removed SD. Full Luna and flash-English builds
  remain open; additional automatic speech triggers require a product decision.
- [ ] **HW-01 - Acquisition and stack ownership.** Run still-needed hardware/
  status/RTC/snapshot/power/ToF suites with fresh authorization. Compile the
  GNSS-enabled path, finish R6 sensor/staging separation without changing locks,
  and inspect every growing canonical-snapshot caller including HTTP, Luna,
  DeepSleepRelease, BLE, callbacks/logging and wake. Boot headroom is measured;
  deepest panel/hardware paths and direct BLE watermarks remain unmeasured.
- [ ] **POWER-01 - Sleep/wake transactions.** Cover Debug software standby and
  Release Deep-sleep, dark Wake, retained route/text/pose clocks, resource order,
  rejected-entry rollback, optional-radio known-off cleanup and BLE reserve rearm.
  Repeat cold boots and at least two 60-s cycles; with rotation/diagnostics repeat
  at least five PWR cycles, including dim/readback, panel-off and fresh orientation.
  Verify PWR/qualified IMU/touch/voice wake, USB reconnect, QMI revision/CmdDone/
  held-IRQ recovery and saved timeouts. Review the proposed stop-provisioning/
  telemetry-until-explicit-restart policy separately from existing suspend/resume.
- [ ] **POWER-02 - Electrical evidence.** Measure board active/standby current
  with radios on/off, ES8311/ES7210 shutdown, dim recovery and IMU wake. Verify
  LC76G I2C ACK/recovery, TTFF/accuracy/current; true Backup needs a switched rail.
  Keep the validated 80-MHz floor; 40 MHz remains deferred. Logs alone do not
  establish electrical savings.
- [ ] **SAFE-01 - Fault injection and liveness.** On a disposable device, inject
  GUI/display/snapshot/NVS/provisioning/audio/Wi-Fi stalls and observe 12-s Task-WDT
  plus breadcrumbs. Measure settings, provisioning, boot-audio and other nested
  worker margins before stack changes; exercise standalone deadlines, DMA lease/
  quarantine, error/byte reporting and typed filters across UI-on/off source lists.

## Luna Shell, boards and tooling

- [ ] **LUNA-01 - Brookesia and profiles.** Finish official Brookesia 0.8
  integration/reviewed locks, including missing `display-touch`. Cover full,
  standalone, display-off, all-off/all-services and output-only/input-only
  composition; board selection must remain separate from UI. Verify mandatory
  display/touch, rotations, DMA, standby and radio coexistence. SD dependency
  removal remains deferred. Review PMU-on/battery-off, PWR, directional codecs,
  build-local cleanup and corrected environment-variable restoration.
- [ ] **LUNA-02 - Lazy documents and navigation.** Exercise bootstrap memory,
  one route/document owner, Back/Forward/history, zero-to-five commands, stale
  generations, scoped subscriptions/partial failures and unload memory. Cover
  reordered skin/YAML identities, separated face/contour, frame-lock exits,
  pause/resume and genuine binding-update suppression on unchanged frames.
  Verify Espressif-app BLE setup/persistence and responsive Settings under radios.
- [ ] **LUNA-03 - Product controls.** Add shared JSON geometry/design tokens,
  product-owned keyboard/confirmation dialogs, Settings access to existing
  nonblocking IMU calibration and opt-in HTTP without boot auto-start. Improve
  FFT layout/gradients and choose gain from measurement. Verify Bluetooth/GPS/
  CPU status and FFT detail latency/heap. Review native Brookesia dialog APIs.
- [ ] **BOARD-01 - Leg Edition.** Review all five latest views, fresh ACC/ROT/
  relative-peak values, lower controls/fades/dropdown, levels 1/2/3, boot/motion
  sound, PWR, saved preferences and rotations/reboot. Confirm excluded radios,
  GNSS, microphones and distance. Explain disabled RTC shown as error. Record
  current image identity; prior level-3/qualified-motion/~25-s-off evidence does
  not prove current pixels, acoustics or weight bearing.
- [ ] **BOARD-02 - ToF release gate.** Before enabling distance, verify both
  TOF400F modules, R15/R16 isolation, shared-TX/dual-RX UART wiring/pins,
  calibration/persistence, timeout/recovery, tilt correction, memory and Settings.
  The obsolete wiring guide was intentionally removed; deletion does not close
  this physical gate or establish correct wiring.
- [ ] **TOOL-01 - Console and composition.** Execute still-needed device-console/
  script-contracts and source/dependency checks through `scripts/device.ps1`.
  Cover deliberate COM selection, numeric/COM equivalence, cancellation, missing/
  ambiguous ports and retained native failures before IDF loads. Review relocated
  imports, menu routing, captures, packaging/cleanup and concurrent artifact locks.
  Normal COM10 flash evidence does not cover these edge cases.

## Decisions, documentation and publication

- [ ] **OTA-01 - Two application slots.** The aligned 16 MiB wake-word table,
  otadata initialization and ota_0 firmware were installed by USB on 12 October.
  The linked image fits: 7,011,312 of 7,274,496 bytes, 263,184 bytes remain per slot.
  Model/LittleFS remain independent shared partitions. Physical boot selection,
  explicit storage handling, authenticated Wi-Fi upload and rollback policy
  remain pending. This is partition/USB-flash evidence, not OTA-service validation.

- [ ] **PRODUCT-01 - Character presets.** Decide whether 04/05 share one size
  (three total) or retain separate presets (four); preserve response/QR clearance.
- [ ] **DOC-01 - Reference continuity.** The approved 2026-10-09 UI-proposal
  files were already missing locally before this consolidation. Restore or locate
  the intended references before artwork/layout changes; preserve that unrelated
  deletion state. Markdown/PDF folder names are now lowercase `docs/cody`.
- [ ] **DOC-02 - Deferred documentation checks.** When explicitly requested,
  check moved links/anchors, the updated HTTP/script contracts, whitespace and
  changed-file secrets. No test, validation scan, build, generator, simulator or
  physical operation accompanies this documentation consolidation.
- [ ] **RELEASE-01 - Final evidence and publication.** Complete the specifically
  requested profile/Debug/Release matrix and startup/UI branding, proprietary-name
  and legal review before GitHub publication. Source support, a narrower passing
  suite or a written image does not close other gates. Push is separately authorized.

Architecture and commands start at [docs/README.md](docs/README.md). Original
129 open entries and their chronological context are preserved in Git `9956446`;
their unresolved outcomes are grouped above without treating implementation as
physical validation. The current file is the authoritative active backlog.
