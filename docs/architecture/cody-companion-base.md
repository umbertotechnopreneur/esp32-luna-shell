# Cody companion base

This branch starts at remote `main` (`86c7ace`) and carries the standalone
display/touch checkpoint from local Luna `main` (`38decc3`, cherry-picked as
`2912cd9`) and the readiness synchronization fix (`3283a19`, cherry-picked as
`369e5026`). The worktree is `E:\ESP32 Cody Companion`; the branch is
`esp32-cody-companion`. The earlier Luna UI remains available in its explicit
profiles; the default here is `cody-companion`.

## Current implementation

The local character PoC now extends this base. See
[`cody-rig-poc.md`](cody-rig-poc.md) for the YAML rig, 256-slot palette, debug
overlay, elapsed-time animation and Windows preview. The historical boot-only
description below records the earlier checkpoint; Cody now also allocates a
bounded PSRAM rig scene. HTTP diagnostics are disabled in the PoC profile.

| Owner | Selected behavior |
| --- | --- |
| Waveshare BSP | Mandatory CO5300 display and CST9217 touch, shared board buses and pins |
| Luna platform | Saved brightness, manual 0/90/180/270 rotation, measured IMU auto-rotation, power/battery, Wi-Fi/BLE, codecs, existing opt-in HTTP diagnostics |
| CODY2D | Caller-owned RGB565 surfaces, bounded fills, 32-level vertical ramps, indexed RLE strip decoding |
| CODY companion | Sleeping, waking and ready blue cloud phases, real touch sampling, complete redraw after rotation or panel wake |

The new consumer owns no LVGL objects, Brookesia GUI worker, UI assets, fonts,
or full-screen framebuffer. Its profile does not admit those component roots.
Only the small surface/fill ideas from `esp32_biagio_animatro` are retained in
the local CODY2D implementation. Robot joints, rig/state machinery, host tools,
and the entire Biagio component are not copied.

Display, touch, Wi-Fi and BLE are required in this profile. The selected IMU,
PMU/battery and codec routes remain available for subsequent companion work.
RTC is now enabled in the Cody profile through the existing canonical UTC/time
and snapshot owners. See [RTC capabilities and scheduling](cody-rtc.md) for
alarm limits, actual interrupt wiring and deferred product uses.
GNSS, SD, distance and speech AFE are disabled. This excludes the GPS
firmware path; the board's fixed GPS power rail is not physically disconnected
by a Kconfig flag. No physical operation was performed.

Manual orientation is available with automatic rotation disabled. Display and
touch use one cardinal transformation, including 180 degrees without reflecting
text or artwork. A content revision invalidates the whole static frame after
rotation or wake. The base does not yet provide an orientation settings screen.

The layout baseline is the source-verified 320 x 320 safe square at (73, 73)
within the 466 x 466 circle. Its rounded margins are usable space, including
the 60 x 60 icon-only Back slot at (10, 203). See
[`circular-display-layout.md`](circular-display-layout.md) for the geometry;
OOBE navigation remains a subsequent implementation step.

## Boot resource

The unchanged Imagegen PNG sources live under `assets/cody/boot/`. Each has its
final prompt and tool mode stored next to it. Blue is the initial accent;
user-selected color will be an OOBE preference. All three frames deliberately
contain only the same cloud on black, overriding the handoff's earlier boot
greeting text.

| Frame | Actual startup checkpoint |
| --- | --- |
| `cody-sleep-blue.png` | Display/touch and saved orientation ready; first frame before the remaining services |
| `cody-waking-blue.png` | Local time/power/storage stages finished, before Wi-Fi/BLE initialization |
| `cody-ready-blue.png` | Remaining startup stages and startup-watchdog cleanup finished |

The middle phase identifies a real stage boundary, not a measured 50 percent
of elapsed boot time. No timer delays startup to display an image. The ready
face reports boot completion, not a Wi-Fi association or Codex handshake.
Each phase survives later orientation and panel-wake redraws.

`scripts/generators/package_cody_boot.py` converts that source to 466 x 466 indexed RGB565
rows with a dedicated black index, no dithering and at most 64 palette entries.
It writes checked-in immutable C++ arrays plus encoding metadata under
`assets/cody/boot/generated/`. It is an asset conversion tool, not an IDF
configure/build/test workflow. Run it explicitly after replacing the source:

```powershell
.\scripts\device.ps1 generate -Generator package-cody-boot
```

The three resources contain 91,264 bytes of array data in total, excluding
their descriptors/alignment and any compiled code. A strip of
466 x 8 RGB565 pixels uses 7,456 bytes of PSRAM; the BSP has separate bounded
internal DMA staging. There is no firmware PNG decoder or full 434,312-byte
RGB565 framebuffer for the boot image. Actual linked size, heap usage, current
and frame/transfer timings have not been measured.

The original high-resolution PNGs are design sources, not runtime allocations.
The assets are in internal flash and require neither SD nor LittleFS. Later
parametric faces and accent changes belong to the 2D renderer; this static
boot resource is not a completed skin engine or OOBE implementation.

## Services and deferred work

Wi-Fi and BLE initialize from the standalone startup sequence, without a Luna
Settings application. Provisioning support is compiled, but this checkpoint
does not automatically enter provisioning or provide an OOBE QR screen.
The local rig PoC disables the inherited HTTP diagnostics at build selection.
No MCP server, Codex registration, session handshake,
question transport, transcription, or PC bridge is implemented here.

Touch samples remain real and current, at 50 ms while active and 200 ms while
rendering is parked. Activity feeds the existing power policy: it can restore
an inactivity-driven display-off state while the device is active. PWR-driven
Device Standby still exits through the existing PWR/IMU policy; this checkpoint
does not add touch wake from that state. Runtime failures stop the consumer and
remain in snapshots. Ambiguous cleanup retains the strip until explicit reset.

No configure, dependency resolution, build, size, simulator, test, validation
scan or device access was requested or run. Both the new `display-touch` and
`cody-companion` dependency locks must be resolved and reviewed in an explicitly
requested validation session through `scripts/device.ps1`. Missing locks
intentionally stop builds instead of silently using a GUI profile's closure.
Existing UI profiles also need their changed manifest gates reviewed.

When authorized, use `scripts/device.ps1 update-lock -Profile cody-companion`
before the requested final build workflow. All firmware output remains under
`artifacts/`. Device validation needs separate explicit authorization.
