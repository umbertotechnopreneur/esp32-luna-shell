# Home token proposals - 2026-10-14

These are illustrative ImageGen concepts for the native circular Home screen.
They use the existing pearl-cloud character as a reference and synthetic token
values. They are not firmware screenshots, approved layout replacements or
physical-display evidence. The current implementation change only shortens
Home token captions with `K`, period grouping and the `TT` unit.

| Proposal | Intended treatment |
| --- | --- |
| [01 - Compact card](01-compact-card.png) | One readable Free token capsule, small context bar and secondary Window/timer row. Preferred direction for a restrained Home. |
| [02 - Context arc](02-context-arc.png) | Large Free value with a circular availability accent and secondary Last/Window values. |
| [03 - Dual metrics](03-dual-metrics.png) | Two equal Free/Window tiles with a secondary timer. |

Generated with the built-in ImageGen tool. Each image is retained byte for byte
from its generated output. [prompts.json](prompts.json) contains the full prompt
set and the native-reference path. No CLI/API fallback was used.

The token numbers are illustrative: Last 155,173, Window 475,000 and Free
319,827 become Last 155K TT, Window 475K TT and Free 319K TT. `K` uses decimal
thousands truncated for the compact Home. Exact counters remain available in
the technical panels. Context graphics represent the same coherent source
pair; they must not suggest cumulative spending or a guaranteed next-turn budget.

Any selected concept needs native-font metrics, chord-aware geometry, actual
damage/animation integration and orange compaction priority. The mockups are
presentation proposals; no new UI task, polling service or bitmap/video playback
has been added to firmware.
