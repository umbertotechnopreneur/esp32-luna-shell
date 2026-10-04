# Cody / Codex Connection Atlas

Date: 4 October 2026, Asia/Saigon.
Requested by: Umberto Giacobbi, project owner.

Created a separate 14-page English A4 landscape visual workbook:
[`cody-codex-connection-atlas-v1.pdf`](../Cody/cody-codex-connection-atlas-v1.pdf).
The earlier planning and navigation atlases are retained unchanged.

The visual sequence is `12 -> 13 -> 16 (10 active seconds) -> 14 -> 15`.
The document explains display-code pairing from the chosen Codex PC, two-sided
confirmation, the brief Idle interval, finding the retained PC, connection
recovery, code retry and the 100-PC LAN scenario. Project/chat/task transport
is described as the next product stage. The workbook is illustrative and does
not claim observed device, plugin or LAN behavior.

## Design

- Same white paper, navy type, blue accents, JetBrains Mono/Segoe UI fonts,
  circular previews, original front-facing cloud and screen IDs as the atlases.
- Three new built-in ImageGen scenes: device with Codex PC, entering the code,
  and one selected PC among many. Original generated PNGs are retained.
- Vector arrows and text remain sharp in print.
- Handwritten-note areas on every page, plus three large sketch boxes and
  decision/open-question areas on page 14.
- No protocol internals or installation commands in the user journey.

## Authoring files and review

The primary PDF and source files are in the Obsidian project folder:
`C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.

- `source/build_codex_connection_atlas.py`: ReportLab authoring source; reuses
  the existing atlas cloud-placement and font helpers.
- `source/codex-connection-image-prompts.json`: exact built-in ImageGen prompts.
- `source/codex-connection-page-manifest.json`: page index.
- `source/codex-connection-document-review.json`: review and output hash.
- `codex-connection-assets/`: the three original generated PNGs.

All 14 PDF pages were rendered with Poppler at a 1500-pixel long edge and
visually inspected. The corrected spacing and revised workflow/retry text
were rendered and inspected again. The final document is A4 landscape and
contains 14 pages. The repository PDF mirrors the Obsidian PDF.

This task performed document authoring and visual review only. It did not
run firmware tests, configure/build, simulator, plugin, LAN or hardware work.
