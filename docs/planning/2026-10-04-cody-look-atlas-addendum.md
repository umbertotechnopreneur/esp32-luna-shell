# Cody look atlas addendum A02

Date: 4 October 2026, Asia/Saigon.
Requested by: Umberto Giacobbi, project owner.
Repository: `E:\ESP32 Luna Shell`.

## Decision

Screen 11 keeps its unique screen identity and changes its user-facing title
from `Cody's color` to `Cody's look`. The old Blue/Mint chips are replaced by
one vertically scrollable, single-choice list below a live Cody preview.

The list exposes Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic
and Dreamwave. Pearl Mist remains the default. A tap persists the stable
internal ID and updates the preview immediately. A vertical drag only scrolls
the bounded list. Back or Done returns to the caller. There is no color editor,
texture mixing or extra parameter surface.

The same selector can be entered from first setup and, later, from Settings.
The current firmware increment implements the screen and its preference; the
remaining screen 10 preferences stay separate work.

## Documents

The append-only PDFs are stored both in the repository under `docs/Cody/` and
in the Obsidian project atlas folder.

| Document | Previous pages retained | A02 page to print | Total pages |
| --- | ---: | ---: | ---: |
| `cody-planning-atlas-v1.pdf` | 1-23 | 24 | 24 |
| `cody-navigation-atlas-v1.pdf` | 1-13 | 14 | 14 |

The planning addendum shows the revised screen, all five display names and
stable IDs. The navigation addendum preserves screen identity 11 and records
first-setup and future Settings entry/return behavior.

All 23 previous planning pages and 13 previous navigation pages render
pixel-identically to the pre-A02 PDFs at 96 DPI. Both new pages were rendered
at 144 DPI and visually reviewed. Evidence and final hashes are stored in the
external atlas source file `addendum-a02-2026-10-04-review.json`.

No firmware build, simulator, automated test, flash, monitor or physical-device
operation was performed for this increment.
