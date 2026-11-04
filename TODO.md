# Active work

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
