# Active work

## Cody optional synthesized voice

- Select and integrate an English speech engine and voice data in an optional
  Cody build. Evaluate Ito's main int8 voice, its terms, IDF 6.0.2 compatibility,
  dependency import scan, and measured resource budget. Keep disabled builds
  free of the engine and its data.
- Support the optional wake-word and speech features together. Review shared
  codec ownership, microphone capture, AEC reference timing, power transitions,
  and memory and partition budgets before admitting the combined composition.
- Connect the prepared Synth Voice page to persisted Enable Synth Voice, volume
  and voice-selection settings supported by the chosen integration; enforce
  the enable preference on speech requests. Provide a deliberate playback path.
- With separate authorization, validate the selected builds and measure real
  playback, pronunciation, microphone/wake behavior, CPU and memory headroom on
  each supported board; compilation alone cannot establish audible behavior.

## Cody layout refinement

- Validate the Options destination cards, Behavior rows, saved-PC icon actions and full SSID
  display at all rotations, with missing/stale and unsupported network names.
- Verify Account and Token activity empty states. Exercise Credits with no vouchers,
  one voucher, equal dates and multiple dates, including local midnight and vertical swipes.
- Verify the dictation countdowns and start, send and close sounds with Sophia and
  a double tap. Exercise cancellation, destination changes, unsupported titles,
  repeated LAN loss before START and during PCM, and a real WAV queue receipt on
  Windows, including a temporary loss after END.
- Verify Connections action admission, passive Wi-Fi animation and the
  provisioning result at all rotations, including a 32-byte SSID and a lost IP.

## CLOUD-01 - Private relay acceptance

- Keep the independently versioned proprietary server outside the public Git
  index. Public documentation contains only architecture and integration limits.
- Exercise failed publication and explicit recovery through the unified
  `device.ps1 relay` workflow while preserving the single writer and SMB state.
- Exercise the direct Aspire CLI deployment against the existing app during
  the next release. The AppHost pipeline can be listed from the solution root.
- After explicit authorization, exercise account/device isolation, pairing races, DPoP
  replay, token rotation, lost responses, revocation, expiry and bounded queues.
- Verify Windows and Linux separately against the actual SMB/Samba mount,
  including cross-host exclusion, interrupted writes, disconnect/reconnect,
  restrictive permissions and power-loss durability limits.
- Verify the operator portal and connect the remote MCP plugin to Dot through a
  supported client authentication route. Verify actual notification calls and
  acknowledgements.
- Confirm HTTPS liveness from Dot's cloud computer after the DNS failure.
  Record the source host with the result.
- Verify the normal Aspire dashboard, HTTPS, operator-only console access and
  authenticated OTLP after relay publication. Aspire's own local working files
  are allowed. Service state and durable logs remain on SMB.

## CLOUD-02 - Device relay integration

- Obtain the deployed relay hostname. Validate the opt-in `-RelayHost` build
  and Connections relay check on the board. Cover absent Wi-Fi, invalid time,
  certificate rejection, HTTPS errors, upgrade rejection, a missing or malformed
  WebSocket frame, a normal close and success. Measure the separate TLS worker's
  stack headroom and free internal heap across both requests.
- Reuse the canonical clock observation for device proof time. Wait for valid
  UTC and surface time errors. Select protected device key and token storage
  for the target board before enrollment.
- Run enrollment as an explicit Connections action. Use a device-owned P-256
  key, short-lived proof per request, a code for portal claim, and physical
  confirmation of the claimed owner. Commit the key-bound token durably before
  showing a completed link. Agent and administrator keys remain off-device.
- Keep networking and token rotation with the relay owner and project its
  state through DeviceSnapshotService. Validate bounded polling, retries and
  credential recovery before admitting notifications. Acknowledge reception
  after owner admission and display after the corresponding frame retires.
- Connect the enrolled firmware to the relay's authenticated WebSocket wake
  signal. Reconcile the queue after connecting and after each hint. Exercise
  reconnect, lost hints, concurrent devices, revocation, rotation and expiry
  without treating the signal as delivery evidence.
- Select credential-safe HTTPS and WebSocket clients for ESP-IDF 6.0.2 before
  attaching proof or token headers. The built-in HTTP client can log serialized
  headers at Debug level, and the built-in WebSocket transport logs the upgrade
  request on a write error. Check credential handling on failure in both build modes.

## CLOUD-03 - Shared chat waiting state

- Confirm the supported Dot surface and its stable chat and reply identities.
  Persist unread state per reply in the relay, independently of delivery receipts.
- Show a red dot and offer explicit Mark as read on each authorized surface.
  Apply a read to the exact reply so it cannot clear a newer reply in the same
  chat. Keep an unread reply eligible for quiet, rate-limited reminders.
- Verify the two-surface flow: new reply, red dot, reminder on the associated
  device, read on either surface, and clearance everywhere. Repeat with a later
  reply in the same chat and verify that a display receipt alone keeps it unread.

## Offline statistics and local timer

- Verify the saved snapshot, UTC timestamp and countdown with the paired PC off,
  then reconnect and confirm fresh data supersedes the saved observation.
- Verify cold startup with and without valid device time, saved-window expiry,
  pairing changes, factory reset and storage failures.
- Exercise Timer controls, background countdown, software standby wake and both
  alarm sounds. Check quiet hours, Sounds Off and overlapping voice/audio.
- Verify local Restart preserves pairing, preferences and saved statistics, and
  that Factory reset still requires its own confirmation.

## Device information, effects and idle presentation

- Execute the focused host checks when authorized.
- Verify System, Memory and Firmware pages, the terminal project QR, CPU baselines
  after standby, and unavailable/failed observations on the physical round display.
- Verify event sounds with Sounds off/on, quiet hours, microphone failure, voice
  capture, standby, battery thresholds and concurrent notifications. Measure stack
  headroom and confirm no duplicate or overdue playback after navigation/reconnect.
- Verify the optional 15-second globe screensaver, the brightness pulse and its pause
  at minimum intensity, first-touch consumption, event interruption and dim/standby deadlines.
- Verify Options/Visible screens responsiveness during held touches and actual
  settings writes; compare display work and CPU with the screen resting.
- Reload the updated plugin and verify context across tool working directories
  plus consecutive quarter-hour account-counter observations.
- Verify the Ready-gated floppy cycle and boot audio on a cold boot and software
  restart. Check quiet hours and ordinary standby wake.

## Codex reset countdown

- Verify PWR standby and wake, BOOT Options, and Options to Codex stats on
  the physical board. Exercise the enabled pages and Info children, lateral controls at all
  rotations, countdown flips and quota ring, reconnect, and stale data.
- Observe consecutive 15-minute samples with a live account. Verify zero,
  missing, interrupted, and account-change intervals at all three zooms.
## WIDGET-STATE-01 - Widget state acceptance

- Verify Visible screens saves and reloads on the board. Exercise skipping hidden
  statistics pages, keeping Device available when all statistics pages are hidden,
  and recovery from storage read and write failures.
- Verify the selected statistics page resumes after standby and remote attention
  events. Check arrow fade and touch cancellation across rotation and wake.
- Shake to return to Reset or the first enabled statistics page. Verify the Device
  fallback when all statistics pages are hidden.
- Disconnect and reconnect Codex while a statistics page is selected. Verify that
  canonical freshness changes independently of the saved visibility policy.

## Companion source selection, typography and desktop controls

- Review the Codex and local companion states and native proportional
  Home typography on the round display at all rotations. Exercise source changes,
  pending questions, finite footer pause/resume, local touch and offline startup.
- Verify the two Setup rows, their left icons and the Options footer on the
  display. Verify that Dictation persists after a restart and that Off prevents
  every recording trigger. Exercise double-tap admission with Wi-Fi down,
  with the paired PC disconnected, and with CCP connected over a LAN that has
  no Internet access.
- Validate Windows and Linux widget controls against the shared CCP broker,
  including explicit pairing, source age, protected credentials, process lifetime
  and rejected or unconfirmed settings writes. No macOS implementation is claimed.
- The 1.75C source tables assign the upper 16 MiB to files/resources, preserving
  OTA. The owner selected a clean layout without old-filesystem compatibility;
  later installation needs the new table and filesystem initialization. No device
  repartitioning or formatting has occurred.

## HARDWARE-PORT-01 - Waveshare 1.43C

- Integrate the single-factory-slot 8 MB layout in the board selector and
  validate the actual application size. USB is the firmware update path.
- Add the CO5300 panel initialization, CST820 touch, 1.43C audio wiring,
  battery ADC/charger status and board-specific power lifecycle. Exclude the
  absent IMU, GNSS, RTC, SD and expander without synthesizing readings.
- Build from source and flash the connected board on COM13. Physical display,
  touch, power, audio and Wi-Fi behavior remain acceptance gates.

The operating and authorization rules are in [AGENTS.md](AGENTS.md).

## Waveshare 1.75C physical acceptance

- Verify prerequisite installation, rerun and first-failure reporting on Windows
  and Ubuntu, then build and flash Cody from Ubuntu booted from USB. Verify that
  Windows still builds and flashes through the same console and `COM` selection.
- Confirm that the post-pairing Debug screen displays the connection details and
  accepts a tap to continue. The phone has confirmed provisioning and Cody has
  obtained a Wi-Fi lease.
- Check alignment and touch at all four rotations, speaker and microphones,
  measured battery telemetry, PWR controls, standby and IMU INT1 wake.
- Verify the 1.75C's 180-degree IMU-to-display mounting correction at all four
  upright orientations and during flat-table turns. Confirm that automatic
  rotation keeps content upright and touch aligned, including transitions
  between gravity-based and gyro-based orientation.
- Confirm the fitted RTC before enabling an external one. Keep SD and GNSS
  unavailable on this board.

## Cody connectivity indicators and sounds

- Verify that Codex absence uses the terminal/disconnected-plug icon while a real
  Wi-Fi loss uses NoWifi and a red crossed status glyph. Check reconnects and
  unavailable observations without stale artwork or repeated sounds.
- Verify measured RSSI updates on the canonical full-snapshot cadence, all three
  signal levels, and unavailable/stale/read-error handling. Connectivity validity
  must remain independent of signal measurement and reconnect-operation errors.
- Listen to the four dedicated Wi-Fi/Codex transition effects on the speaker.
  Check overlapping edges, volume, Sounds, quiet hours, busy admission, route
  changes and standby cancellation. Audio dispatch is not audible completion.
- Final compilation, firmware size, host contracts and physical checks are
  pending authorization. Review compiled frames and device stack headroom after
  the bounded Wi-Fi/status schema growth; no task stack was enlarged. The new
  bitmap and three additional PCM clips add 130,432 source payload bytes before
  descriptors/alignment; existing bitmap planes remain unchanged.

## Standby wake on the SD-equipped prototype

- Observe touch and PWR wake, including a touch interrupt with rejected
  coordinates. Check retained diagnostics after reset and the current-image SD
  session identity. Preserve the accepted SD/PCB workaround.
- If the wake fault recurs, capture its onset and decode against the running ELF.

## Native approval notices

- After the updated plugin loads, use a genuine new prompt to observe one
  advisory notice per physical recipient and native turn. Follow Back or expiry,
  resumed activity and routing on Cody. Approval decisions stay on the PC.
- Capture any recurrence at its onset. Keep replayed hooks out of this observation.

## Compiler failure on recurrence

If an authorized build reproduces the GCC internal segmentation fault in ESP-DSP
`dspi_conv_f32_ansi.c`, retain the failing invocation and compiler diagnostic.
Reproduction alone does not justify another build.

Deferred hardware ports, voice and walking trials, speech-to-Codex and other
acceptance work are in [planned-todo.md](planned-todo.md).
