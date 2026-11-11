# Active work

This file tracks open implementation and acceptance. See [planned-todo.md](planned-todo.md) for deferred work and development history. Apply [AGENTS.md](AGENTS.md) to each operation. Activating work does not authorize builds, tests, simulator runs, asset generation or device operations.

## CODY-NEXT - Multi-chat domain and Muse

Work is tracked on `feat/cody-vNext`. Keep the status below aligned with the [phase plan](planned-todo.md#cody-next---domain-model-and-muse-implementation-plan), its task markers and progress summary. Retain reusable screen sources and IDs. The vNext contracts may change without compatibility layers or automatic migration.

| Phase | State and remaining work |
| --- | --- |
| CODY-NEXT-00 | [Screen inventory](docs/cody/screen-catalog.md) accepted at G01/G02. Route and hardware acceptance remain in 09/11. |
| CODY-NEXT-01 | [Domain contract](docs/cody/domain-model.md) reviewed at G01/G02. Compile headers and verify memory layout and runtime behavior. |
| CODY-NEXT-02 | [Protocol, schema and examples](cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md) reviewed at G01/G02. Execute the schema, compile helpers and verify the live protocol. |
| CODY-NEXT-14 | Portable store, firmware/host composition, Muse frame boundary and Waveshare descriptor wired. Review current-target capacity, OS boundaries and scenario integration before G01. |
| CODY-NEXT-03 | Broker restores stable records and history under a new epoch and serves bounded state-v1 pages. Complete question correlation and executed acceptance. |
| CODY-NEXT-04 | Broker preserves exact question, answer and confirmation evidence. Signed CCP1 owns delivery. Complete native turn correlation, state-v1 question publication and restart/race acceptance. |
| CODY-NEXT-05 | DeviceSnapshotService serves generation-bound queries and the paired device restores counts, a bookmark and unresolved references. Verify storage and recovery. |
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

## Offline statistics and local timer

- With the PC off, verify saved statistics, UTC timestamp and countdown. On reconnect, check that fresh data supersedes saved data. Cover cold boot with and without valid time, expiry, pairing changes, factory reset and storage failures.
- Verify Timer controls, background countdown, software standby wake, both alarms, quiet hours, Sounds Off and overlapping voice/audio. Confirm Restart preserves pairing, preferences and statistics and Factory reset requires confirmation.

## Device information, effects and idle presentation

- Run authorized focused host checks. On the round display, verify System, Memory, Firmware, terminal project QR, CPU baseline after standby and unavailable/failed readings.
- Verify event sounds with Sounds on/off, quiet hours, microphone failure, voice capture, standby, battery thresholds and concurrent notices. Measure stack headroom and check for duplicate or late playback after navigation/reconnect.
- Check the optional 15-second globe, brightness pulse and pause at minimum, first-touch consumption, interruptions and dim/standby deadlines. Check Options/Visible screens during held touches and settings writes against idle display work and CPU.
- Reload the plugin and verify context across tool working directories and consecutive 15-minute account counters. Verify the Ready-gated floppy cycle and boot audio on cold boot and software restart, including quiet hours and ordinary standby wake.

## Codex reset countdown

On the board, exercise PWR standby/wake, BOOT Options, Options to Codex stats, enabled pages/Info children, lateral controls at each rotation, countdown flips, quota ring, reconnect and stale data. Observe consecutive 15-minute live-account samples at all three zooms, including zero, missing, interrupted and account-change intervals.

## WIDGET-STATE-01 - Widget state acceptance

Verify Visible screens saves, reloads and recovers from read/write failures. Skip hidden statistics pages and retain Device when all are hidden. Resume the selected page after standby or remote attention, checking arrow fade and touch cancellation on rotation/wake. Shake returns to Reset, the first enabled statistics page or Device when none are enabled. Verify Codex reconnect changes freshness independently of visibility.

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
