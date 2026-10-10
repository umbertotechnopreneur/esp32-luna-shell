# Cody cloud/title scene assets

`scenes.json` is the English-only authoring source for reusable standalone
cloud/title/content scenes. The initial greeting is based on planning-atlas
page 05, screen 04. It shares the existing pearl cloud and snapshot response.

Generate using `scripts/device.ps1 generate -Generator cody-scenes` with an
explicit `--font-path` forwarded through `-ToolArguments`. Layout-only changes
may use `--definitions-only` with the existing committed font. The source must be
JetBrains Mono; no substitute font is selected. The full source font is not
packaged. Derived glyph coverage is renamed **Cody Title Mono** under SIL
OFL 1.1; keep `OFL.txt` with the asset distribution.

`generated/scene_data.hpp` owns immutable scene definitions.
`generated/scene_font.cpp` embeds 19,000 bytes of two-bit printable-ASCII
coverage in firmware flash. `generated/scene_font.2bpp.bin` and `manifest.json`
are for the Windows preview; that binary is not a second firmware embed.

The generator now emits an explicit `cody.scene-font.v2` package with 4bpp
coverage, baked at 4x resolution and downsampled while preserving the committed
title/body metrics. V2 names both font binaries and records their format and
hashes; the native renderer and preview validate the same packing. The existing
v1 assets above remain 2bpp until a separately authorized full bake supplies the
licensed font source. Definition-only generation preserves the validated format.

Layout, setting behavior, content lifetime, costs and deferred validation are
documented in `docs/architecture/cody/runtime.md`.

## Product voice - 2026-10-10

Cody speaks as a calm work companion: short invitations, useful next steps and
clear outcomes. Fixed copy is printable English ASCII. Friendly wording never
claims a selected chat, active work, successful checks or delivered reply before
the canonical owner confirms that state. Clear existing labels such as Cancel,
Send, Done, Checks failed and UNAVAILABLE remain explicit.

The scene catalog was reviewed across setup, personalization, activity, checks,
questions and connection errors. These title revisions keep the same scene IDs,
geometry, font metrics and reveal/touch rules:

| Scene | Revised title |
| --- | --- |
| pair-invitation | Let's connect |
| pairing | Connecting... |
| pairing-failed | Pairing failed |
| online | You're online |
| looking-for-pc | Finding your PC |
| network-wait | Wi-Fi needed |
| project-active | Let's get to work |
| working | On it |
| response-ready | Reply ready |
| session-paused | Taking a pause |
| session-ended | Chat closed |
| connection-lost | Link unavailable |
| companion-idle | Ready for more |
| pr-pending | Review pending |
| checks-running | Checking the work |
| check-failures | Check details |
| decision-notice | Your call |
| decision-choices | Pick your reply |
| decision-confirm | Ready to send? |
| decision-waiting | Reply on its way |

Connected screen 15 retains the factual Connected title and shows Ready to team
up / Start a chat in Codex. With the committed 12-pixel body glyph and 11-pixel
advance, these lines occupy 177 and 232 pixels in the existing 232-pixel slot.
Activity details distinguish Making it happen, Shaping your code and Running a
command. Missing or stale activity remains UNAVAILABLE / Activity unavailable.

Question delivery uses Sending your reply, then Reply reached your PC for the
broker receipt, with Confirming delivery until the recipient acknowledges it.
Reply sent remains reserved for that final confirmation. Saved links, incomplete
pairing, unavailable updates and real error codes remain distinguishable.

Regenerate scene definitions through the public cody-scenes generator after
explicit authorization. Firmware compilation, simulator execution and physical
display/touch validation require separate authorization.
