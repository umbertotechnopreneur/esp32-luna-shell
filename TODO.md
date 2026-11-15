# Active work

This file tracks open implementation and acceptance. See [planned-todo.md](planned-todo.md) for deferred work and development history. Apply [AGENTS.md](AGENTS.md) to each operation. Activating work does not authorize builds, tests, simulator runs, asset generation or device operations.

## Current owner queue

0. Complete the device-code simulator and native screen atlas: link the
   unchanged display/onboarding owners and their host service adapters, then
   integrate the unchanged audio owner with platform PCM output and settings
   policy. Replace Python-drawn device pages only after their native routes and
   touch behavior work. Capture all active screens from the native runtime and
   generate the complete PDF. The native Linux host library builds successfully;
   remaining routes, full audio coverage and physical-device acceptance are outstanding.
   Continue this same work under `CODY-NEXT-06`: Studio A-D integrates strict
   catalog/scenario contracts, direct native preparation, completed-frame
   identity, saved scenarios, comparison and ten pilot cases. The Linux native
   build, 32-test pilot suite and 31-test real Tk/native UI suite pass. All ten
   have identical pixels across fresh processes and out-of-order shared-session
   preparation. Baseline/review/scenario persistence, capture history, visible
   keyboard focus/scrolling, expander restoration and wide/small window geometry
   have host evidence. Availability and loaded-capture review filters, baseline
   restoration and unavailable baseline retention have host evidence. A separate
   automatic capture preserves interactive pixels, receipt, sensors, NVS and
   scenario. Physical acceptance remains pending.
   Snapshot integrity, build-profile closure, maintenance-script contracts and
   portable Cody runtime checks pass after validator/fixture alignment.
   Phase C has Linux host acceptance: S01, S02, S17, P01, P04,
   P12 (quiet-hours editor), W02, W04, S15 (provisioning error) and C04 (tool
   calls). The original inventory is now identified by date/baseline/hash in
   the machine catalog. Nine cases use the full runtime; C04 uses the production
   C++ browser presenter and typed synthetic queries, without domain transport/
   reducer acceptance. S15 preserves its transient timeout cause. Phase E now
   also has Linux host evidence for isolated S01/S02 display-worker replay,
   live boot replay with firmware PCM, P01 ↔ P04 and ten adjacent guide gestures
   S17–S22 (29/29 phase-E checks). S19 is conditional on wake-word support.
   Captures retain navigation history, and direct/route frames are not assumed
   to have the same animated state. Remaining routes, general worker scheduling,
   Windows and physical acceptance remain open. Phase F now integrates isolated
   suite generation, per-case progress/errors, cancellation/reaping, atomic
   last-valid publication and opt-in validated-build watching. Publication
   verifies the exact selection plus build/catalog/scenario dependencies.
   Studio preserves the interactive owner, NVS, pending form and explicit
   baseline; thumbnails and differences update after publication. PDF export
   retains native pixels, inventory revision/IDs, source functions and absence
   reasons. Linux atlas contracts, native suites, PDF integrity/visual checks
   and the 32-check Tk suite pass, including automatic generation and a visible
   native frame at 720x540 with Atlas open.
   Next, extend actual coverage in phase G. The shared source catalog contains
   108 entries, including retained families; the earlier 100 was an unverified
   owner estimate. Prioritize audio playback and visual fidelity. General
   worker/timer precision can remain deferred.

## First-run tour

- With separate authorization, build and verify the first-boot melody, normal
  restart ding, sound-off and quiet-hours suppression, interrupted playback,
  birth replay and one-shot persistence/readback failure. Measure flash and
  audio/memory headroom; the first-boot melody is 4.6 seconds at 16 kHz mono.
- Complete separately authorized source builds and size checks for the added
  indexed artwork. Verify first-run and returning-device migration, page resume,
  skip and storage failures, button instructions in both voice configurations,
  Sofia availability, QR scanning, and navigation on the physical device.
- The Sofia screen explains availability; an interactive recognition exercise
  and explicit voice-preference choice are not yet implemented in the tour.
  The button and touch pages explain the controls but do not require a practice
  gesture or provide exercise-specific success feedback.

## Codex actions from Cody

- Continue the existing Codex interaction flows described in the
  [device action direction](docs/cody/runtime.md#the-road-ahead). Voice input
  changes remain separately scoped by the owner.
  Verify public Desktop capabilities before enabling chat input, work control
  or desktop focus; keep unsupported and unconfirmed outcomes explicit.
- Select and audition the owner's SFX. Complete static review and separately
  authorized platform and physical acceptance.

## CODY-NEXT - Multi-chat domain and Muse

Work is tracked on `feat/cody-vNext`. Keep the status below aligned with the [phase plan](planned-todo.md#cody-next---domain-model-and-muse-implementation-plan), its task markers and progress summary. Retain reusable screen sources and IDs. The vNext contracts may change without compatibility layers or automatic migration.

| Phase | State and remaining work |
| --- | --- |
| CODY-NEXT-00 | [Screen inventory](docs/cody/screen-catalog.md) accepted at G01/G02. Route and hardware acceptance remain in 09/11. |
| CODY-NEXT-01 | Domain contract reviewed at G01/G02; see the [domain types](cody-device/components/CODY_domain/include/CODY_domain_types.hpp) and [state protocol](cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md). Compile headers and verify memory layout and runtime behavior. |
| CODY-NEXT-02 | [Protocol, schema and examples](cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md) reviewed at G01/G02. Execute the schema, compile helpers and verify the live protocol. |
| CODY-NEXT-14 | Portable store, firmware/host composition, Muse frame boundary and Waveshare descriptor wired. Review current-target capacity, OS boundaries and scenario integration before G01. |
| CODY-NEXT-03 | Broker restores stable records and history under a new epoch and serves bounded state-v1 pages. Complete question correlation and executed acceptance. |
| CODY-NEXT-04 | Broker preserves exact question, answer and confirmation evidence. Signed CCP1 owns delivery. Complete native turn correlation, state-v1 question publication and restart/race acceptance. |
| CODY-NEXT-05 | DeviceSnapshotService serves generation-bound queries and the paired device restores counts, a bookmark and unresolved references. Verify storage and recovery. |
| CODY-NEXT-06 | Studio A-D, expanded E and F have Linux host evidence: native pilot/guide cases, live firmware PCM, isolated atlas jobs/build watcher and inventory-linked PDF. The 32-check Tk suite verifies session/baseline preservation and small-window frame visibility. Extend measured coverage in G; remaining routes, general worker scheduling, Windows and physical acceptance remain pending. |
| CODY-NEXT-07 | Native CODY2D floppy-to-Muse showcase changes pose every ten visible seconds. Complete domain-driven direction and physical acceptance. |
| CODY-NEXT-09 | Native Codex browser exposes Chats, Turns, Tool calls, Active calls, Events and Statistics. Finish Home/Tools gestures, two-dot chrome and hardware acceptance. |

Phase 11 executable acceptance remains pending. Compile headers and run schema/examples, chunk/revision helpers and phase-05 reducer scenarios covering missing chunks, epoch changes, lost ACKs and capacity/clock failures. Check record and stack sizes and measure available device memory before enabling the 4,096-call candidate.

## Creamwork 3 Muse avatar

Verify the VibeWare-to-Muse handoff, wake/idle motion, tap reaction, rotation and Options access on the board. Listen for the Cody cue with sound enabled, muted and during quiet hours.

## Creamwork 3 audio menu

- Add native CODY2D Audio with playback volume, Mic 1/2 meters, shared mic gain, Close and Power off. Keep the unavailable Synth Voice separate and preserve Home navigation, logical rotation, circular fit and bounded updates.
- Apply supported volume and codec dB steps through audio/capture owners. Confirm applied values, surface failures and respect Sounds, quiet hours and active playback. Preserve choices through codec reconfiguration. Share gain policy between AFE and diagnostic capture and persist through the settings owner without defaulting failed reads/writes.
- Use canonical per-channel mean/peak, timestamps, validity and errors from DeviceSnapshotService. Reuse AFE levels or request level-only acquisition. Exclude duplicate capture and spectrum processing. Two inputs show independent meters, one shows Mic 2 unavailable, and zero disables gain and shows both unavailable. Keep acquisition with its owner and keep stale, missing and failed observations explicit.
- Close restores the prior route and releases only this menu's acquisition request. Confirm Power off with the existing UI pattern and queue the power owner's transaction, preserving farewell, resource drain and visible errors.

### Pending validation gates

After integration and static review, obtain fresh authorization per exact command to cover zero/one/two inputs and native layout. With separate physical-device authorization, verify channel independence, gain/volume, audible playback, AFE ownership, stale/errors, Close, and shutdown/cancel.

## Cody layout refinement

- Validate the shared flat menu and screen presenters, single-line settings labels,
  data-qualified statistics and swipe navigation on the device and in Studio.
- Verify bounded IMU-driven parallax on Home, Cody and the BOOT menu; measure
  readability, power and memory at each setting before enabling it by default.
- Verify the eight Cody character themes and their saved selection at every
  supported rotation; preserve room for future personalization options.
- Verify weekly alarm thresholds, frame acknowledgement, sound policy, pairing
  changes and reset behavior. A same-PC account change with the same reset cannot
  be distinguished until an authoritative account identity is available.
- Complete direct device refresh of the public OpenRouter reference catalog.
  OpenRouter prices are indicative and distinct from official OpenAI prices and
  Codex credits; retain source time, freshness and errors and do not infer fields.
- Check Options cards, Behavior rows, saved-PC icon actions and full SSIDs at every rotation, including missing, stale and unsupported network names. Check Connections action admission, passive Wi-Fi animation and provisioning at all rotations, with a 32-byte SSID or lost IP.
- Check Account and Token activity empty states. Exercise Credits with zero, one, equal-date and multiple-date vouchers, local midnight and vertical swipes.
- With Sophia and double tap, verify dictation countdowns and start/send/close sounds. Exercise cancellation, destination changes, unsupported titles and repeated LAN loss before START, during PCM and temporarily after END. Confirm a real Windows WAV queue receipt.

## CLOUD-01 - Private relay acceptance

- Keep the independently versioned proprietary server outside the public Git index. Limit public docs to architecture and integration limits.
- Exercise failed publication and explicit recovery via `device.ps1 relay`, preserving the single writer and SMB state. At the next release, exercise direct Aspire CLI deployment against the existing app. The AppHost pipeline is listable from the solution root.
- With authorization, verify account/device isolation, pairing races, DPoP replay, token rotation, lost responses, revocation, expiry and bounded queues. Test the actual SMB/Samba mount separately on Windows and Linux for cross-host exclusion, interrupted writes, reconnect, restrictive permissions and power-loss durability limits.
- Verify the portal, supported remote MCP client authentication in Dot, notification calls and ACKs. Recheck HTTPS after the DNS failure from Dot's cloud computer and record the source host.
- After publication, verify the Aspire dashboard, HTTPS, operator-only console and authenticated OTLP. Keep service state and durable logs on SMB. Aspire may keep its local working files.

## CLOUD-02 - Device relay integration

- Obtain the deployed hostname and validate the opt-in `-RelayHost` build and Connections check on the board. Cover absent Wi-Fi, invalid time, certificate/HTTPS/upgrade errors, missing or malformed WebSocket frames, normal close and success. Measure TLS worker stack headroom and free internal heap across both requests.
- Before enrollment, select protected key/token storage and credential-safe ESP-IDF 6.0.2 HTTPS/WebSocket clients. Verify failed requests in both build modes because HTTP Debug and WebSocket write errors can expose headers. Use valid UTC from the canonical clock for proof and surface time errors.
- Enroll from Connections using a device-owned P-256 key, short-lived per-request proof, a portal claim code and physical owner confirmation. Persist the key-bound token before reporting success. Keep agent/admin keys off-device.
- Keep network and rotation policy with the relay owner and expose state through DeviceSnapshotService. Validate bounded polling, retries and credential recovery before admitting notifications. Acknowledge receipt after owner admission and display after frame retirement.
- Authenticate the WebSocket wake signal and reconcile the queue on connect and each hint. Exercise reconnect, lost hints, concurrent devices, revocation, rotation and expiry. Treat hints as prompts to reconcile.

## CLOUD-03 - Shared chat waiting state

Confirm Dot's supported surface and stable chat/reply IDs. Persist unread state per reply in the relay, independently of delivery receipts. Show a red dot, offer explicit Mark as read on authorized surfaces and schedule quiet, rate-limited reminders. Verify new reply, device reminder and exact-reply clearance from either surface. A later reply stays unread after an earlier read or a display receipt.

## Offline statistics

- With the PC off, verify saved statistics, UTC timestamp and countdown. On reconnect, check that fresh data supersedes saved data. Cover cold boot with and without valid time, expiry, pairing changes, factory reset and storage failures.
- Confirm Restart preserves pairing, preferences and statistics and Factory reset requires confirmation.

## Device information, effects and idle presentation

- Validate assistant-reply speech end to end: confirm the installed Codex App
  Server supports exact completed-turn and final-message reads, refresh the
  desktop plugin, and verify encrypted segmented delivery, opt-out, expiry,
  cancellation and actual speaker completion on the paired device. Unsupported,
  ambiguous and oversized replies remain explicit rather than truncated.
- Validate the ported bounded four-voice synth renderer and its PCM buffer
  ownership, codec volume and microphone gain across playback, voice and standby;
  measure latency, stack headroom and memory on the device when authorized.
- Run authorized focused host checks. On the round display, verify System, Memory, Firmware, terminal project QR, CPU baseline after standby and unavailable/failed readings.
- Verify event sounds with Sounds on/off, quiet hours, microphone failure, voice capture, standby, battery thresholds and concurrent notices. Measure stack headroom and check for duplicate or late playback after navigation/reconnect.
- Check the optional 15-second globe, brightness pulse and pause at minimum, first-touch consumption, interruptions and dim/standby deadlines. Check Options during held touches and settings writes against idle display work and CPU.
- Reload the plugin and verify context across tool working directories and consecutive 15-minute account counters. Verify the Ready-gated floppy cycle and boot audio on cold boot and software restart, including quiet hours and ordinary standby wake.

## Codex reset countdown

On the board, exercise PWR standby/wake, BOOT Options, Options to Codex stats, enabled pages/Info children, lateral controls at each rotation, countdown flips, quota ring, reconnect and stale data. Observe consecutive 15-minute live-account samples at all three zooms, including zero, missing, interrupted and account-change intervals.

## WIDGET-STATE-01 - Widget state acceptance

Verify statistics pages appear only with available data and retain Device when none are available. Resume the selected page after standby or remote attention, checking touch cancellation on rotation/wake. Shake returns to the first available statistics page or Device. Verify Codex reconnect updates freshness and page availability.

## Companion source selection, typography and desktop controls

- On the round display at all rotations, check Codex/local states, proportional Home typography, source changes, pending questions, finite footer pause/resume, touch and offline startup.
- Check both Setup rows, left icons, Options footer and Dictation persistence. Off blocks every trigger. Exercise double tap with Wi-Fi down, the paired PC disconnected and CCP on a LAN without Internet.
- Verify Windows and Linux widgets against the shared CCP broker for explicit pairing, source age, protected credentials, process lifetime and rejected/unconfirmed settings writes. macOS remains outside the current implementation scope.
- The 1.75C table reserves the upper 16 MiB for files/resources and preserves OTA. Installation requires the new table and fresh filesystem initialization. Repartitioning and formatting are pending.

## HARDWARE-PORT-01 - Waveshare 1.43C

- Integrate the 8 MB single-factory-slot board profile and validate application size. Use USB updates.
- Add CO5300 initialization, CST820 touch, 1.43C audio wiring, battery ADC/charging status and board power lifecycle. Expose IMU, GNSS, RTC, SD and expander as unavailable.
- Build from source and flash COM13 when authorized. Verify physical display, touch, power, audio and Wi-Fi.

## Waveshare 1.75C physical acceptance

- Verify prerequisite installation, reruns and first-failure reporting on Windows and Ubuntu. Build and flash from USB-booted Ubuntu and confirm Windows uses the same console and COM selection.
- Provisioning and Wi-Fi lease were confirmed. Verify the post-pairing Debug details and tap to continue.
- Verify display/touch at four rotations, speaker/mics, measured battery telemetry, PWR, standby and IMU INT1 wake. Check the 180-degree mounting correction upright and flat, including gravity/gyro transitions.
- Confirm the fitted RTC before enabling an external one. Keep SD and GNSS unavailable.

## Cody connectivity indicators and sounds

- Verify the terminal/disconnected plug for Codex absence and NoWifi/red crossed glyph for Wi-Fi loss. Exercise reconnect and unavailable data without stale artwork or repeated sounds.
- Check all three measured RSSI levels on the full snapshot cadence, plus unavailable, stale and failed reads. Connectivity validity stays independent of RSSI and reconnect errors.
- Listen for all four Wi-Fi/Codex transitions on the speaker. Check overlap, volume, Sounds, quiet hours, busy admission, navigation and standby cancellation.
- Final compilation, size, host contracts and board checks await authorization. Review compiled frames and stack headroom after Wi-Fi/status schema growth. The new bitmap and three PCM clips add 130,432 source payload bytes before descriptors/alignment, with existing bitmap planes and stack allocations retained.

## Standby wake on the SD-equipped prototype

Verify touch and PWR wake, including a touch interrupt with rejected coordinates. Check retained reset diagnostics and current-image SD session identity. Keep the accepted SD/PCB workaround. If the fault recurs, capture onset and decode against the running ELF.

## Native approval notices

After the updated plugin loads, use a genuine new prompt to verify one advisory notice per physical recipient and native turn. Follow Back/expiry, resumed activity and Cody routing. Approval remains on the PC. Capture recurrence at onset and exclude replayed hooks.

## Compiler failure on recurrence

If an authorized build reproduces the ESP-DSP GCC internal segmentation fault in `dspi_conv_f32_ansi.c`, retain the invocation and diagnostic. Establish a reason beyond reproduction and obtain fresh authorization before any rerun.
