# Deferred work

Activate a stable ID in [TODO.md](TODO.md) before starting deferred work.

## Boundaries that still apply

- Physical voice, walking/classifier and speech-to-Codex trials remain deferred.
  Keep HAR observational, using original labels and actual timestamps. Training
  and automatic shutdown require separate product decisions.
- Keep the accepted SD/PCB standby and wake workaround. Further investigation
  of the underlying SD fault requires a separate decision.
- The experimental WAV queue uses public ingress. Model-side speech understanding
  still needs verification. Keep separately billed transcription out of scope.
- Firmware build, host tests, flash and physical acceptance are separate evidence.
  Authorization follows [AGENTS.md](AGENTS.md).

## Implementation and product decisions

- [ ] **CLARITY-01 - Split route/frame responsibilities.** Extract coherent
  presenters from `CODY_onboarding.cpp` and stages from `draw_companion_frame()`;
  keep orchestration, lifetime, lock ownership and errors explicit.
- [ ] **CLARITY-02 - Share bounded marquee calculations.** The activity and approval
  headers still duplicate first-glyph/offset/capacity/centering math. Share only
  that calculation; retain text, viewport and pausable clocks with each owner.
- [ ] **CLARITY-03 - Replace narrow uses of full snapshots.**
  Replace snapshot() by-value reads with bounded, fallible projections,
  starting with storage formatting. Budget the nested stack before removing the
  API. Keep the v1 scene-font reader until its assets migrate and continue
  rejecting invalid calibration records.
- [ ] **OTA-01 - Upload, boot selection and rollback.**
  Implement authenticated Wi-Fi upload, boot selection and rollback on boards
  with two supported OTA slots. Define shared storage policy and observe
  rollback on target.
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
- [ ] **BATTERY-ENDURANCE-01 - Repeatable runtime.**
  Measure continuous runtime through confirmed battery shutdown. Compare Debug
  and Release with matched workload, battery and starting conditions. Treat a
  source gap or unknown interruption as an incomplete observation.
- [ ] **WAKE-RECONNECT-01 - Wake-to-Codex latency.**
  Measure the interval from wake request through completed wake, association,
  IP and authenticated READY. Check saved-network startup, immediate progress
  after IP and recovery choices after 15 seconds offline.
- [ ] **AUDIT-TARGET-01 - Memory/failure recovery.**
  Measure nested stack margins during wake and callbacks. Exercise
  rejected-model cleanup and incremental model-tamper rejection.
- [ ] **FAULT-04 - Codex-choice reboot report.**
  If selecting a Codex choice triggers a reboot, capture the first panic from
  the actual choice and decode it against the running ELF.
- [ ] **FAULT-02 - Touch/caress freeze.** Capture the first rejected packet or
  transition; review two contacts, D000/AB ACK, malformed fifteen-FF isolation,
  normal-mode return, invalid samples and release-before-readmission. Then observe
  an eligible 300-ms caress and its sound, distinguishing gaps/speed/zone exit.
- [ ] **FAULT-03 - Renderer stop.**
  Capture a renderer stop from its first failing frame with the matching ELF.
  Inspect SPI acquisition deadlines and measure sustained rendering on the
  affected route.
- [ ] **FAULT-PERIPHERAL-01 - Peripheral/USB errors.** Diagnose codec/PMU I2C,
  I2S disable-before-enable, logger loss and interrupted serial captures separately.
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

- [ ] **CCP-01 - Security rollout on target.**
  Identify the paired-PC revision and observe pairing, recovery, packet/ACK
  loss, restart, consecutive questions and routing. Measure ingress bursts and
  nested stack use on target.
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
- [ ] **CCP-06 - Acting and notices.**
  Observe Thinking/Editing/Running, attention blink, exact-turn reply and
  interruption, genuine PC approval, source gaps, hidden routes,
  battery/charging thresholds and expiry.
- [ ] **CCP-07 - Price bands and panel timing.**
  Observe the displayed price band, compaction priority, full marquees and
  tap/shake transitions. Verify PC adoption and current-image stack headroom.
- [ ] **CCP-USAGE-01 - Native telemetry on the display.** Observe Home counts and
  genuine compaction animation/sound. Reload clears followed-chat state until a
  new prompt; do not synthesize recovery or infer bills/thresholds from model names.
- [ ] **UI-ACTIVITY-02 - Connection/activity presentation.**
  Observe discovery, the ten-minute search and three-minute Working standby,
  the valid-battery wake prelude, genuine activity text and four-panel Home
  metadata and geometry.
- [ ] **UI-ACCENT-01 - Animation acceptance.**
  Observe gears and clock accents, rotations and completed partial repaint on
  the device. Measure nested stack headroom.
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
- [ ] **UI-07 - Diagnostics/Memory.**
  Observe the measured overview and heap bars in Debug and Release. Cover
  unavailable states, Back/Normal restoration, standby, allocation errors and
  rotations. Reserve PWR for standby and shutdown and keep Debug BOOT
  independent. Keep facial channels internal.
- [ ] **ART-01 - Muse avatar.** Review physical wake, idle and tap playback,
  frame continuity, full display rotation and touch mapping. Keep additional
  interaction modes separate until their event sources are defined.
- [ ] **AUDIO-01 - Device sound.** Listen to BootRise and sixteen current Codex
  cues with frame/semantic admission, quiet/volume, no wake replay, microclick
  reveal deltas, cancellation and codec contention. Greeting C remains an audition.
- [ ] **AUDIO-02 - Windows sound.**
  Listen to the host tap and boot cues. Confirm
  cancellation and the reports from real playback errors.
- [ ] **BOOT-01 - Branding handoff.** Review the full 42-frame/3500-ms VibeWare
  cycle, readiness dwell, titles, failure/DMA parking, first/returning boot,
  rotations, sleep policy and Luna linked memory.
- [ ] **PERF-01 - Native budget.** Measure a truly static route and full stage/
  payload/DMA/CPU distributions, interruption,
  alignment/tails, log overhead and radio/HTTP/BLE load. Pixels and current-image
  nested headroom must accompany improvements against the 40-ms target.
- [ ] **PERF-02 - Host budget.** Measure ABI-2 raster/snapshot cost and memory,
  atomic import/failure, redaction/aging and sparse coverage. Review DPI/controls/
  gestures after a requested restart; cross-monitor DPI handling is unimplemented.

## Optional services and release coverage

- [ ] **STORE-01 - Logger/readback.**
  Measure recovered internal memory, queue pressure, archival failure,
  unavailable SD, optional failures and watermarks. Check CRC and session
  identity across passive or interrupted USB capture, busy storage, remount and
  standby.
- [ ] **STORE-02 - SD lifecycle.**
  On separately authorized disposable media, review format, cancel, progress,
  failure, removal, full-card states, mounted wake and reset recovery.
  Formatting requires its own single-use approval.
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
- [ ] **VOICE-01 - Recognition and end-to-end recording.**
  When physical trials resume, review Sophia, Jarvis, Computer and three-model
  packing separately. Cover capture/AEC ownership, CPU/PSRAM, standby/stale
  events, Home’s 420-ms double tap, recording countdowns and 350-ms
  arbitration, bounded PCM, ACK, gaps, replay, recovery, changed chats and
  uncertain END. Trace capture through WAV, the frozen-chat queue and Desktop
  consumption. Assess speech understanding separately.
- [ ] **TTS-01 - Audible SD speech.**
  With the accepted SD workaround in place, check availability, the boot
  diagnostic, bounded wait, errors and diagnostic-only quiet bypass. Listen for
  intelligible Italian.
- [ ] **TTS-02 - Variants/budgets.** Cover excluded, metadata-only SD and English
  flash profiles, locks/config/flash lists, SHA rejection and four-language UTF-8.
  Measure PCM, CPU/PSRAM/stacks, LVGL coexistence, cancel/quiet/codec/sleep and SD removal.
  Full Luna TTS and flash-English builds remain separate; new triggers need a decision.
- [ ] **HW-01 - Acquisition/stacks.**
  Measure deepest physical stack use for acquisition and staging, full-snapshot
  HTTP, Luna, DeepSleepRelease, BLE, callbacks, logging and wake.
- [ ] **LUNA-01 - Composition matrix.**
  Cover the Brookesia 0.8 display-off/all-services and audio I/O combinations.
  Observe display, touch, rotation, DMA, radios and sleep on target and cover
  the PMU-enabled, battery-service-disabled profile.
- [ ] **LUNA-02 - Document lifetime.** Observe lazy bootstrap/unload memory,
  navigation/history, zero-to-five commands, stale generations, scoped bindings,
  partial failure, reordered identities, pause/frame locks and BLE Settings responsiveness.
- [ ] **BOARD-01 - Leg Edition.**
  Review all five views with fresh sensors and peaks, controls, levels, audio,
  PWR, settings, rotations, excluded peripherals and disabled RTC status.
  Observe load-bearing behavior on the current image.
- [ ] **BOARD-02 - ToF.**
  Before enabling, review both ToF modules, R15/R16 isolation, shared TX and
  dual RX wiring, calibration, persistence, timeout and recovery, tilt and
  Settings.
- [ ] **TOOL-01 - Console runtime/package cases.**
  Exercise cancellation, ambiguous ports and artifact ownership during
  packaging, capture and concurrent operations.
- [ ] **DOC-02 - Wider links/anchors.**
  Review moved documentation links and anchors across the repository.
- [ ] **RELEASE-01 - Broader acceptance.**
  Define and execute the needed profile matrix and physical startup/branding
  acceptance when release work resumes. Resolve wider product and licensing
  requirements, including model-weight rights under ACTIVITY-01.

## Product ideas

| Area | Possible next work and limits |
| --- | --- |
| Other PC platforms | Implement Linux/macOS credential and launcher owners before claiming support. Keep Python additions neutral; no new Windows service/general PC observer. |
| Remote connection | Private ASP.NET Core/Aspire/OpenTelemetry relay source is developed separately from this public repository, using standard OS-mounted SMB/Samba storage without Azure APIs. Firmware integration remains deferred: key-bound account pairing, outbound HTTPS polling, exact event identity, expiry, receipts and revocation. Native-platform SMB validation, Dot connection and deployment remain acceptance gates; no live cloud support is claimed. |
| Built-in assistant audio | Recheck supported App Server audio access, formats, account availability and existing-chat attachment. No separate paid transcription. |
| Other assistants | Explore explicit adapters for Codex, Claude, Hermes, OpenClaw and possibly Your Dot; verify each public integration and real acknowledgements rather than assume a universal API. |
| Offline outbox | Explicit SD recording with original timestamp/clock validity, destination, bounded buffers, interrupted-write recovery, retention/deletion and uncertain-delivery reconciliation. No formatting/fallback; review stale actions before replay. |
| Reminders and focus | Start with user-started sessions, one-time reminders/countdown/snooze/daily overview; later consider lists, notes and recurrence. Trusted time, quiet preferences and existing power/timer ownership; RTC INT cannot directly wake ESP. No home automation or inferred health/fatigue. |
| Message/reply UX | Research push-to-talk, preview, destination, Send/Cancel and drafts through a supported channel. Keep credentials on PC and distinguish sent, received and completed; additional speech triggers need a decision. |
| Wellbeing model | Still disabled: no session adapter/timer/selector/UI. Keep activity, rest, drink, meal, mood and reaction domains separate; motion stress/battery fatigue retain their causes. Lost liveness is Unknown, not counted work. One latest eligible cue, no decorative queue; snooze, quiet, expiry and acknowledged events remain distinct. A caress is not proof of a break/drink. |
| Character | Consider other Muse interaction modes after physical review of wake, idle and touch. |
| Automatic power | Only after observer validation: persistent observations, absent interaction, cancellable warning and measured false-shutdown rate. Keep this separate from the classifier and explicit shutdown. |
| Protocol security | Evaluate standard PAKE/high-entropy QR against offline guessing and standard AEAD with nonce/replay discipline; current HMAC adds authenticity, not confidentiality. Review optional plaintext HTTP separately. Bound startup runtime.log with multiprocess ownership. |
| Dependencies/BSP | Deferred removal of indirect FatFS/SDMMC when SD is off; split bus/display/audio/SD/expander owners and review board locks, including rectangular ES8311 input. Replace pinned LVGL-adapter/default feature workarounds with supported upstream configuration. |
| Energy research | Measure AFE/transport cadence and actual component currents. BOOT interrupt work needs debounce/rearm/hold ownership; idle CCP still needs deadlines. Compare Wi-Fi power-save, 40-MHz floor/Light-sleep and USB reconnect; retain unsafe SOF exclusion. Consider 2-Hz dim acquisition only with Full Snapshot cadence and measured 600-ms P99 wake. Battery-absent ADC disable needs BAT_INSERT reenable/settling evidence. Future PCB may route PMU IRQ to an RTC GPIO. |
| Maintenance | Transactional banner updates only after a banner-policy decision; scoped GNSS parser extraction with separately requested coverage; extract hardware/audio/Leg/settings presenters without changing navigation/bindings or promising Cody RAM gains. |

