# Active work

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
