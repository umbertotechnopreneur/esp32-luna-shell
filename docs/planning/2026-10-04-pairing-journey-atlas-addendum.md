# Pairing handoff and first-run atlas addendum A04

Date: 4 October 2026, Asia/Saigon.
Requested by: Umberto Giacobbi, project owner.
Repository: `E:\ESP32 Luna Shell`.

## Decision

Successful pairing writes and reads back `cody_firstboot=false` after the current
attempt has a healthy Wi-Fi association and IP. Debug builds then show the only
new atlas identity, `39 / Pairing debug`; Release skips it and enters 09 directly.
The Debug screen has a micro Cody cloud, six whitesmoke diagnostic lines, no
button, and one tap-anywhere dismissal.

The continued route uses existing IDs:
`09 -> 10 -> 36 -> 11 -> 12 -> 13 -> 14 -> 15`. Screen 12 is present when
`CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=y`, which is the current Cody default and
profile value; `n` skips from 11 to 13. Screen 15 requires a real current
Codex/PC session and is not inferred from Wi-Fi or BLE.

Screen 09 requests standby after 60 seconds and remains 09 after wake until the
user presses Continue. Screen 11 follows planning page 24 but has no Back in the
first-run journey. Pearl Mist remains visibly selected by default; Done is
disabled until the user explicitly taps one row. Screens 36 and 11 remain
reusable for later personalization.

## Documents

| Document | Previous pages retained | A04 page to print | Total pages |
| --- | ---: | ---: | ---: |
| `cody-planning-atlas-v1.pdf` | 1-25 | 26 | 26 |
| `cody-navigation-atlas-v1.pdf` | 1-15 | 16 | 16 |

The PDFs are byte-identical between `docs/cody/` and the Obsidian project atlas
folder. Both A04 pages were rendered with Poppler at 180 DPI and visually reviewed.
The external generation record is `source/addendum-a04-2026-10-04-review.json`.

No firmware configure/build/size, simulator, automated test, flash, monitor or
physical-device operation was performed for this increment.
