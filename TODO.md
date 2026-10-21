# Active work

Only the remaining follow-ups belong here. Last reconciled: **2026-10-21**.
Implementation and delivery history are in [done.md](done.md) and
[archive.md](archive.md); the wider backlog is in [planned-todo.md](planned-todo.md).
Listing an item does not authorize its execution; see [AGENTS.md](AGENTS.md).

## Standby wake: physical acceptance

Source `70678d9f` / `99b167b3` separates touch-IRQ wake from coordinate-read
success and retains early RTC breadcrumbs plus an SD session identity.
Release `cody-224-g99b167b3` was flashed on 17 October. Still needed:

- Observe touch/PWR wake, including a rejected coordinate report.
- Read retained diagnostics after reset and confirm the current-image SD identity.
- Capture the original failure before assigning a cause; the recovered log was
  from older firmware. Retain the accepted SD/PCB limitation.

[Delivery details](archive.md#native-touch-wake-and-retained-diagnostics---2026-10-17).
The 20 October Debug build was not flashed and does not close this acceptance.

## Approval notices: live activation

Plugin 0.6.10 was installed on 17 October; the correction admits one advisory
notice per physical recipient/native turn. Consolidated host evidence is 147/147,
including the separately authorized parser recovery after loading Visual Studio.
Still needed:

- Confirm plugin reload after the owner's Codex restart, then use a genuine new
  prompt to observe activation. Never replay hooks to manufacture activity.
- Observe notice admission, Back/expiry, resumed activity and routing on Cody.
- Keep historical locator/capability errors separate: the original repetition's
  exact cause/count was not captured.

[Investigation and delivery](archive.md#approval-notice-source-fix-host-checks-and-firmware-flash---2026-10-17).
Native approval decisions remain on the PC.

## Toolchain: retain the unexplained compiler failure

Two complete Release compilations on 17 October hit a GCC internal segmentation
fault in unchanged ESP-DSP `dspi_conv_f32_ansi.c`. Incremental retries passed at
four/one job without source or flag changes. The cause remains unknown. If it
recurs during an authorized build, retain the exact invocation and failure;
do not start a reproduction build solely because this item exists.

## Parked work

Physical voice, walking/classifier trials and speech-to-Codex follow-up remain
explicitly deferred. HAR is observational: no training or automatic shutdown.
The accepted SD/PCB workaround is not a reopened SD investigation. Other physical
acceptance and future features stay in the [deferred register](planned-todo.md).
