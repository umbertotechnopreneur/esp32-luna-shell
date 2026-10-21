# Deferred work

Last reconciled: **2026-10-21**. These items are parked, not an execution queue.
[TODO.md](TODO.md) holds active follow-ups; [done.md](done.md) records completed
work. Stable IDs are retained below. The
[archived register](archive.md#superseded-deferred-work-register) preserves the
original detailed checklists and dated investigations.

## Boundaries that still apply

- The owner deferred physical voice, walking/classifier and speech-to-Codex trials
  on 13 October. Listing them here does not resume them. HAR stays observational:
  original labels, actual timestamps, no training or automatic shutdown.
- The accepted SD/PCB limitation and standby/wake workaround remain accepted.
  Storage entries do not reopen the old SD fault investigation.
- The public experimental WAV queue is implemented. The 15 October pilot delivered
  a file intact, but did not establish model-side speech understanding. The old
  native-loopback endpoint/launcher is historical, not a current setup blocker.
  Separately billed transcription was declined; future ingress must be public/supported.
- Firmware build, host tests, flash and physical acceptance are separate evidence.
  Authorization follows [AGENTS.md](AGENTS.md). Older check counts and image sizes
  live in the archive, not as requirements to rerun them.

## Implementation and product decisions

- [ ] **CLARITY-01 - Split route/frame responsibilities.** Extract coherent
  presenters from `CODY_onboarding.cpp` and stages from `draw_companion_frame()`;
  keep orchestration, lifetime, lock ownership and errors explicit.
- [ ] **CLARITY-02 - Share bounded marquee calculations.** The activity and approval
  headers still duplicate first-glyph/offset/capacity/centering math. Share only
  that calculation; retain text, viewport and pausable clocks with each owner.
- [ ] **CLARITY-03 - Replace narrow uses of full snapshots.** Live Luna callers
  still use by-value `snapshot()`; the storage-only format request is one example.
  Add fallible bounded projections and migrate real callers before deleting the API.
  Budget nested stacks. The committed v1 scene-font reader is still needed until
  assets migrate; rejecting old calibration records is not a fallback to delete.
- [ ] **HARDWARE-PORT-01 - Enclosed Waveshare boards.** Add typed board selection
  independently of UI; port 1.75C pins, power/IMU wiring and actual peripherals
  first. For 1.43C resolve CO5300/SH8601, 8-MB flash/OTA/speech fit, CST820,
  audio wiring and ADC/charger acquisition. It lacks the IMU; neither schematic
  has GNSS/SD/the old expander, and 1.75C external RTC remains disputed.
  [Board comparison](docs/bsp/boards/purchased-amoled-comparison.md).
- [ ] **OTA-01 - Upload, boot selection and rollback.** Two OTA slots and shared
  model/LittleFS partitions exist; authenticated Wi-Fi upload, explicit storage
  policy and observed boot/rollback still need implementation/acceptance.
- [ ] **LUNA-03 - Product controls.** Shared JSON geometry tokens, product keyboard/
  confirmations, Settings access to existing IMU calibration and opt-in HTTP,
  plus measured FFT gain/layout/latency and status presentation. Use public APIs.
- [ ] **PRODUCT-01 - Character presets.** Decide whether 04/05 share one size
  (three presets) or retain four; preserve response/QR clearance.
- [ ] **UI-05 - Font generation and raster.** Committed glyphs are 2bpp. Generate
  4bpp only when requested; then inspect coverage, tiles, byte order, compiled cost,
  native parity, legibility and Options/Look scrolling.

## Physical power, activity and fault evidence

- [ ] **POWER-OFF-01 - Shutdown behavior.** Observe battery-only 60-second/scene
  inactivity, USB/full/absent battery, first hold, three-second farewell, owner
  drains/rollback and configured power-on timing. Verify Sophia/touch/shake wake
  and whole-board off. The independent 10-second emergency hold and supply-dependent
  PMU restart sources prevent an unconditional Power-only restart claim.
- [ ] **ACTIVITY-01 - Pretrained observer.** Preserve recovered preprocessing,
  original labels and real 20-Hz timestamps; validate placement, accuracy, cadence
  gaps, calibration/standby/pressure boundaries, energy and stack headroom before
  relying on classifications. No synthetic samples or training. Resolve original
  weight redistribution rights before publishing an embedding firmware binary.
- [ ] **BATTERY-ENDURANCE-01 - Repeatable runtime.** The first run retained
  6 h 55 min 42.870 s with `source_gaps=1` and `INTERRUPTED_UNKNOWN`; it did not
  establish exact battery death. Obtain continuous-source evidence and compare
  Debug/Release only with matched workload, battery and starting conditions.
- [ ] **WAKE-RECONNECT-01 - Wake-to-Codex latency.** Measure request, completed
  wake, association, IP and authenticated READY; the reported 10-30-second delay
  is not closed by source changes. Check saved-network startup, immediate IP
  advance and recovery choices after 15 seconds offline.
- [ ] **AUDIT-TARGET-01 - Memory/failure recovery.** Observe nested stack margins,
  rejected-model cleanup, incremental model-tamper rejection and wake/callback
  paths. Recorded builds and individual frames do not prove these runtime cases.
- [ ] **FAULT-04 - Codex-choice reboot report.** Capture the first panic with
  matching ELF during the actual choice. The `33377507` captures retained a prior
  panic reset but no new panic in their 30/60-second windows; cause is unconfirmed.
- [ ] **FAULT-02 - Touch/caress freeze.** Capture the first rejected packet or
  transition; review two contacts, D000/AB ACK, malformed fifteen-FF isolation,
  normal-mode return, invalid samples and release-before-readmission. Then observe
  an eligible 300-ms caress and its sound, distinguishing gaps/speed/zone exit.
- [ ] **FAULT-03 - Renderer stop.** Capture the first failing frame with matching
  ELF. The `2bd0626` core identified infinite SPI acquisition; later `c3dceaf`
  captures do not establish long stability or explain the missing `bb8fd327` interval.
- [ ] **FAULT-PERIPHERAL-01 - Peripheral/USB errors.** Diagnose codec/PMU I2C,
  I2S disable-before-enable, logger loss and interrupted serial captures separately.
  This is the renamed second historical FAULT-04, not the reboot-loop item.
- [ ] **POWER-01 - Sleep/wake transactions.** Observe Debug standby and Release
  Deep-sleep: dark restore, retained clocks/routes, failed-entry rollback, known-off
  radio cleanup/BLE reserve, rotations and QMI ACK/held IRQ. Include cold boots,
  two 60-second cycles and five rotated/diagnostic PWR cycles. Proposed permanent
  provisioning/telemetry stop is a separate decision from existing suspend/resume.
- [ ] **POWER-02 - Electrical behavior.** Measure board/radio/codec/IMU current,
  dim recovery and GNSS ACK/TTFF/accuracy. True GNSS Backup needs a switched rail.
  Retain the 80-MHz floor; lower clocks are research, not established savings.
- [ ] **SAFE-01 - Fault injection.** On an explicitly authorized disposable device,
  exercise GUI/display/snapshot/NVS/provisioning/audio/radio stalls, Task-WDT,
  breadcrumbs, deadlines and DMA quarantine; measure deepest worker margins.

## CCP and device presentation

The four-panel Home contract is in [Cody layout](docs/cody/layout.md).
Old three-panel layouts and character coordinates in the archive are superseded.

- [ ] **CCP-01 - Security rollout on target.** Record the actual paired-PC
  revision and observe pairing/recovery, packet/ACK loss, restart, consecutive
  questions and routing. Measure ingress bursts/nested stacks; old rollout
  versions and passing host suites are not current device acceptance.
- [ ] **CCP-02 - Genuine hook delivery.** After an authorized reload, follow a
  real prompt through autoload, latest-prompt selection, chat leases, broker/LAN
  recovery and turn correlation. No replayed activity, transcript inference or trust edits.
- [ ] **CCP-03 - Decisions and delivery.** Observe screens 26/27/31, selection,
  Send, received versus recipient-confirmed delivered, original TTL, cancel/offline
  retention and terminal EVENTACK ordering. Include notices and read-only PR/check
  views; physical restart/reset require their own explicit intent.
- [ ] **CCP-04 - Home, mode and gestures.** Review all four current panels and
  five-row Options, strict Desk/Away persistence/errors, metadata freshness,
  approval/compaction priority, paused tap cycling and deliberate shake.
- [ ] **CCP-05 - Character response.** Observe cardinal-rotation gaze, caress
  warmth, positive-only mood and exact compaction release, preserving source-gap,
  setup/preset/Debug isolation and protected channels. Measure nested stack minima.
- [ ] **CCP-06 - Acting and notices.** Observe Thinking/Editing/Running, attention
  blink, exact-turn reply/interruption, real PC approval, source gaps, hidden routes,
  battery/charging thresholds and expiry. Host automation is already recorded.
- [ ] **CCP-07 - Price bands and panel timing.** Observe the actual badge,
  compaction priority, full marquees and tap/shake transitions; verify PC adoption
  and current-image headroom. Cache/transport/raster implementation is complete.
- [ ] **CCP-USAGE-01 - Native telemetry on the display.** Observe Home counts and
  genuine compaction animation/sound. Reload clears followed-chat state until a
  new prompt; do not synthesize recovery or infer bills/thresholds from model names.
- [ ] **UI-ACTIVITY-02 - Connection/activity presentation.** Observe discovery,
  ten-minute search/three-minute Working standby, valid-battery wake prelude,
  genuine activity text and current Home metadata/geometry. Use today's layout,
  not the retired firmware-identity or three-panel presentation.
- [ ] **UI-ACCENT-01 - Animation acceptance.** Observe gears/clock accents,
  rotations and completed partial repaint. Host raster/runtime/frame evidence exists;
  device pixels and nested headroom remain open.
- [ ] **BATTERY-01 - Quiet notices.** Observe ten-second free-Home admission,
  four-second dwell, 20/10-percent warnings, charge/full transitions and stale/error
  removal with route/touch deferral. Studio automatic notice/caption parity and
  physical clearance/headroom remain; manual power studies keep factual selectors.

## Routes, character and sound

- [ ] **UI-01 - Setup/retained onboarding.** Observe first-run/saved pairing and
  Wi-Fi, DHCP/recovery, No/Later without writes, development replay, standby/Deep-sleep
  and 09 -> 10 -> 36 -> 11 -> optional 12 -> 13 -> 16 -> 14 -> 15 confirmation gates.
- [ ] **UI-02 - Escape/holds.** Observe the 1500-ms hold, 12-pixel cancellation,
  250-ms sample gaps, across-wake contacts, busy-route cancellation, actual release
  and rotations without clearing Wi-Fi/pairing.
- [ ] **UI-03 - Pairing/timers.** Observe 05/06/07/08/09, pending/ready/error,
  cancel/readback/Retry, first-boot persistence and Debug 39. Cover awake 30-second
  route closure, 90-second pairing, independent 60/90-second standby overrides
  and screen 12's 180-second copied-code window, including late worker teardown.
- [ ] **UI-04 - Controls/QR.** Review semantic cards/capsules, numbered rows,
  completed-text reveal, visible-only touch, contrast/chords/quiet zones and real
  QR scans at all rotations. Reconcile 05/06/07's suppressed lateral Back with
  the previous-route rule; Home retains its explicit no-Back contract.
- [ ] **UI-06 - Look persistence.** Observe save/readback retry, busy Back/Done,
  reset-during-write, rotated touch, stable IDs and repeated-entry leak/race behavior.
- [ ] **UI-07 - Diagnostics/Memory.** Observe the same measured overview and heap
  bars in Debug/Release, unavailable states, Back/Normal restoration, standby,
  allocation errors and rotations. Facial-channel UI and PWR screenshot/double-click
  are retired; do not reintroduce them. PWR owns standby/shutdown; Debug BOOT is separate.
- [ ] **ART-01 - Petting/idle.** Observe 300-ms caresses, heart/gain interruption,
  stress-dependent eye taps, zones/cooldowns/pause, nonrepeating 20-40-second idle
  actions, stressed breathing and cancellation.
- [ ] **ART-02 - Manga.** Review all fourteen symbols, scale/anchors/clearance,
  Main/Sleep versus Micro admission, follow/erasure, 400-ms fades, completed-frame
  sound, drops/cooldowns/quiet and idle mute. Measure full frame/DMA/CPU/stack cost.
- [ ] **ART-03 - Composed face.** Observe lip/eye/tongue/breath/whistle/cloud/pearl
  shapes, independent blink, taps/drag, canonical charge/mic/motion axes and
  stale/error/rotation behavior. Internal channels remain; their old debug UI does not.
- [ ] **AUDIO-01 - Device sound.** Listen to BootRise and sixteen current Codex
  cues with frame/semantic admission, quiet/volume, no wake replay, microclick
  reveal deltas, cancellation and codec contention. Greeting C remains an audition.
- [ ] **AUDIO-02 - Windows sound.** Listen to host tap/manga/whistle/greeting/boot
  cues, cancellation and real playback errors; a built DLL does not prove audibility.
- [ ] **BOOT-01 - Branding handoff.** Review the full 42-frame/3500-ms VibeWare
  cycle, readiness dwell, titles, failure/DMA parking, first/returning boot,
  rotations, sleep policy and Luna linked memory.
- [ ] **PERF-01 - Native budget.** Measure a truly static route and full stage/
  payload/DMA/CPU distributions, 50/75/100% and non-Pearl parity, interruption,
  alignment/tails, log overhead and radio/HTTP/BLE load. Historical 100% missed
  40 ms; pixels and current-image nested headroom must accompany improvements.
- [ ] **PERF-02 - Host budget.** Measure ABI-2 raster/snapshot cost and memory,
  atomic import/failure, redaction/aging and sparse coverage. Review DPI/controls/
  gestures after a requested restart; cross-monitor DPI handling is unimplemented.

## Optional services and release coverage

- [ ] **STORE-01 - Logger/readback.** Establish linked/actual evidence for the
  estimated ~68-KiB recovery; observe pressure, archival failures, unavailable SD,
  optional failure without reset and watermarks. Review CRC/session identity,
  interrupted/busy/passive USB, remount and standby ownership.
- [ ] **STORE-02 - SD lifecycle.** Only if separately resumed on a disposable
  card, review format/cancel/progress/failure/removal/full states and mounted wake.
  Formatting needs its own single-use approval; one old Debug cycle proves no reset recovery.
- [ ] **TIME-01 - Clock/routines.** Observe atomic timezone/quiet settings, overnight
  windows/Rome DST, corrupt/missing values, NTP failure/recovery, RTC trust/write/readback,
  backup retention, greeting damage and deferred boot sound.
- [ ] **VOICE-PRESENCE-01 - Recording lifecycle.** When voice trials resume,
  observe complete-frame/three-second start, five acquired seconds of silence,
  cancellable three-second send, late cancellation and real delivery. MCP transport
  EOF is not independent GUI-process/crash detection. See VOICE-01.
- [ ] **VOICE-POLL-UI-01 - Voice timing/resources.** Measure AFE notification waits,
  disconnected cadence, ribbon pacing/gain, 350-ms send/double-tap arbitration,
  shake/discard, rotations, rollback, stacks and current. No battery-gain claim from code alone.
- [ ] **VOICE-01 - Recognition and end-to-end recording.** Physical trials stay
  deferred. Sophia was observed; Jarvis/Computer and three-model packing are separate.
  On resumption, cover capture/AEC/ownership, CPU/PSRAM, standby/stale events,
  Home's 420-ms double tap, recording's countdowns and 350-ms arbitration, bounded
  PCM/ACK/gap/replay/recovery, changed chats and uncertain END. Verify capture -> WAV
  -> frozen-chat queue -> Desktop consumption separately from speech understanding.
- [ ] **TTS-01 - Audible SD speech.** Confirm SD availability after the accepted
  workaround, then the boot diagnostic, bounded wait, real errors and diagnostic-only
  quiet bypass. Prepared files and corrected boot did not prove intelligible Italian.
- [ ] **TTS-02 - Variants/budgets.** Cover excluded, metadata-only SD and English
  flash profiles, locks/config/flash lists, SHA rejection and four-language UTF-8.
  Measure PCM, CPU/PSRAM/stacks, LVGL coexistence, cancel/quiet/codec/sleep and SD removal.
  Full Luna TTS and flash-English builds remain separate; new triggers need a decision.
- [ ] **HW-01 - Acquisition/stacks.** Review sensor/staging owners and full-snapshot
  HTTP/Luna/DeepSleepRelease/BLE/callback/logging/wake paths. Host checks and the
  GNSS-enabled build passed; deepest physical paths/BLE watermarks remain unmeasured.
- [ ] **LUNA-01 - Composition matrix.** Brookesia 0.8 and repaired display-touch/
  full-wake-word builds/locks passed. Broader display-off/all-services and audio I/O,
  physical display/touch/rotation/DMA/radio/sleep and PMU-on/battery-off remain open.
- [ ] **LUNA-02 - Document lifetime.** Observe lazy bootstrap/unload memory,
  navigation/history, zero-to-five commands, stale generations, scoped bindings,
  partial failure, reordered identities, pause/frame locks and BLE Settings responsiveness.
- [ ] **BOARD-01 - Leg Edition.** Review five views, fresh sensors/peaks, controls,
  levels, audio/PWR/settings/rotations, excluded peripherals and disabled RTC status.
  Use current-image evidence; old level-3/motion/off timing is not weight-bearing acceptance.
- [ ] **BOARD-02 - ToF.** Before enabling, review both modules, R15/R16 isolation,
  shared-TX/dual-RX wiring, calibration/persistence, timeout/recovery, tilt and Settings.
  Removing the obsolete wiring guide did not validate wiring.
- [ ] **TOOL-01 - Console runtime/package cases.** Retain cancellation, ambiguous
  ports and artifact ownership under packaging/capture/concurrency. Automated source
  fixtures are complete; a normal flash does not cover these remaining runtime paths.
- [ ] **DOC-02 - Wider links/anchors.** A repository-wide moved-link/anchor review
  remains beyond the scoped documentation review. Recorded PDF topology/rendering
  and script/HTTP checks are already complete for their recorded revisions.
- [ ] **RELEASE-01 - Broader acceptance.** Other profile matrices, observed startup/
  branding and wider product/licensing acceptance remain beyond the completed
  selected-build/host/USB delivery. Model-weight rights are retained under ACTIVITY-01.

## Ideas, not commitments

Activate a specific item in TODO.md before implementation. These are consolidated
from the 4-12 October product/research notes; detailed proposals remain in the archive.

| Area | Possible next work and limits |
| --- | --- |
| Other PC platforms | Implement Linux/macOS credential and launcher owners before claiming support. Keep Python additions neutral; no new Windows service/general PC observer. |
| Remote connection | Explore genuine host events -> ASP.NET Core/Azure relay -> outbound TLS from Cody, with user/device identity, ordering, expiry and recovery. Aspire implementation/deployment remain deferred. |
| Built-in assistant audio | Recheck supported App Server audio access, formats, account availability and existing-chat attachment. The 0.147.0 schema observation is historical. No separate paid transcription. |
| Other assistants | Explore explicit adapters for Codex, Claude, Hermes, OpenClaw and possibly Your Dot; verify each public integration and real acknowledgements rather than assume a universal API. |
| Offline outbox | Explicit SD recording with original timestamp/clock validity, destination, bounded buffers, interrupted-write recovery, retention/deletion and uncertain-delivery reconciliation. No formatting/fallback; review stale actions before replay. |
| Reminders and focus | Start with user-started sessions, one-time reminders/countdown/snooze/daily overview; later consider lists, notes and recurrence. Trusted time, quiet preferences and existing power/timer ownership; RTC INT cannot directly wake ESP. No home automation or inferred health/fatigue. |
| Message/reply UX | Research push-to-talk, preview, destination, Send/Cancel and drafts through a supported channel. Keep credentials on PC and distinguish sent, received and completed; additional speech triggers need a decision. |
| Wellbeing model | Still disabled: no session adapter/timer/selector/UI. Keep activity, rest, drink, meal, mood and reaction domains separate; motion stress/battery fatigue retain their causes. Lost liveness is Unknown, not counted work. One latest eligible cue, no decorative queue; snooze, quiet, expiry and acknowledged events remain distinct. A caress is not proof of a break/drink. |
| Character | Wake stretch/idle variants and richer clock personality only after physical review. Choose antialiasing/tessellation/damage from timings. Review the host-only 99-vertex/138-triangle cloud before native work; rejected billows-v5 stays historical, approved pearl renderer stays separate. |
| Automatic power | Only after observer validation: persistent observations, absent interaction, cancellable warning and measured false-shutdown rate. Keep this separate from the classifier and explicit shutdown. |
| Protocol security | Evaluate standard PAKE/high-entropy QR against offline guessing and standard AEAD with nonce/replay discipline; current HMAC adds authenticity, not confidentiality. Review optional plaintext HTTP separately. Bound startup runtime.log with multiprocess ownership. |
| Dependencies/BSP | Deferred removal of indirect FatFS/SDMMC when SD is off; split bus/display/audio/SD/expander owners and review board locks, including rectangular ES8311 input. Replace pinned LVGL-adapter/default feature workarounds with supported upstream configuration. |
| Energy research | Measure AFE/transport cadence and actual component currents. BOOT interrupt work needs debounce/rearm/hold ownership; idle CCP still needs deadlines. Compare Wi-Fi power-save, 40-MHz floor/Light-sleep and USB reconnect; retain unsafe SOF exclusion. Consider 2-Hz dim acquisition only with Full Snapshot cadence and measured 600-ms P99 wake. Battery-absent ADC disable needs BAT_INSERT reenable/settling evidence. Future PCB may route PMU IRQ to an RTC GPIO. |
| Maintenance | Transactional banner updates only after a banner-policy decision; scoped GNSS parser extraction with separately requested coverage; extract hardware/audio/Leg/settings presenters without changing navigation/bindings or promising Cody RAM gains. |

The dormant wellbeing defaults superseded the early 120-minute exploration;
that history is not a current recommendation or a shipped reminder feature.
