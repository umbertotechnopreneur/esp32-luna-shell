# Active work

## Device information, effects and idle presentation

- Compile the current firmware and execute the focused host checks when authorized.
- Verify System, Memory and Firmware pages, the terminal project QR, CPU baselines
  after standby, and unavailable/failed observations on the physical round display.
- Verify event sounds with Sounds off/on, quiet hours, microphone failure, voice
  capture, standby, battery thresholds and concurrent notifications. Measure stack
  headroom and confirm no duplicate or overdue playback after navigation/reconnect.
- Verify the optional 15-second globe screensaver, first-touch consumption, event
  interruption, eight-frame-per-second limit and unchanged dim/standby deadlines.
- Verify Options/Visible screens responsiveness during held touches and actual
  settings writes; compare display work and CPU with the screen resting.
- Install the updated plugin when authorized and verify context across tool working
  directories plus consecutive quarter-hour account-counter observations. Confirm
  the boot disk on a real restart; source inspection has not explained its absence.

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
