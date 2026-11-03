# Active work

Keep current follow-ups here and deferred work in [planned-todo.md](planned-todo.md).
Listing an item does not authorize execution; see [AGENTS.md](AGENTS.md).

## Waveshare 1.75C: physical acceptance

The 32 MB board has an independent physical selector and OTA layout. Remaining:

- Complete the requested source build and flash on the confirmed COM8 unit.
- Observe display/touch alignment and all rotations, speaker/microphones,
  pairing, measured battery telemetry, PWR controls, standby and IMU INT1 wake.
- Confirm the delivered PCB's RTC population before enabling an external RTC;
  the supplied schematic has none. Retain explicit unavailability for SD/GNSS.
- Run the physical-board regression suite when separately authorized; preserve
  the 16 MB prototype profile and its existing peripheral owners.

## Standby wake: physical acceptance

Touch-IRQ wake is independent of coordinate-read success. Early RTC breadcrumbs
and SD session identity are implemented. Still needed:

- Observe touch/PWR wake, including a rejected coordinate report.
- Read retained diagnostics after reset and confirm the current-image SD identity.
- Capture the original failure before assigning a cause, using matching firmware
  and logs. Retain the accepted SD/PCB limitation.

## Approval notices: live activation

The plugin admits one advisory notice per physical recipient/native turn.
Still needed:

- Confirm the current plugin is loaded after Codex restart, then use a genuine new
  prompt to observe activation. Never replay hooks to manufacture activity.
- Observe notice admission, Back/expiry, resumed activity and routing on Cody.
- Capture a recurrence before assigning its cause or count.

Native approval decisions remain on the PC.

## Toolchain: unresolved compiler failure

A GCC internal segmentation fault in unchanged ESP-DSP `dspi_conv_f32_ansi.c`
remains unexplained. If it recurs during an authorized build, retain the exact
invocation and failure; do not start a reproduction build solely for this item.

## Parked work

Physical voice, walking/classifier trials and speech-to-Codex follow-up remain
explicitly deferred. HAR is observational: no training or automatic shutdown.
The accepted SD/PCB workaround is not a reopened SD investigation. Other physical
acceptance and future features stay in the [deferred register](planned-todo.md).
