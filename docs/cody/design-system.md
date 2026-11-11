# Cody device design system

This is the shared visual and interaction language for the native Cody device
UI and its Studio preview. Apply it to new screens and to revisions of existing
ones. The physical display uses a 466 x 466 circular logical canvas; see
[display geometry](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md)
and [Cody layout](layout.md) for admission and touch bounds.

## Palette and typography

The semantic source is [theme.json](../../cody-device/assets/cody/palette/theme.json).
It defines device RGB565 colors, authored Studio RGB colors, status roles,
surfaces, controls, text gradients and the independent assistant-quota and
battery arcs. Use semantic roles for new UI rather than introducing local color
constants. Allowance windows, technical panels and device information reuse the
shared dashboard roles for secondary metrics, positive states, tracks,
separators, attention and errors. The indexed artwork palette remains an
asset-format concern.

Screen titles share the pixel-arcade glyphs and violet treatment of the
**SET UP WI-FI** screen. When a title exceeds its viewport, keep its font and
size and scroll it horizontally, pausing at each end. Center titles that fit.
Pass the complete title and its admitted viewport to the renderer. Reserve
room for adjacent icons and QR quiet zones when defining that viewport.
Subtitles use a consistent, smaller glyph treatment.
General body text uses the shared system font; measurements, codes and values
that require aligned columns use the shared monospace font. These four roles
must be chosen by meaning rather than by a screen's identity. Reserve status
colors for actual state and errors. Keep dynamic labels readable; never truncate
an identity without a way to inspect the full value.

Some existing dynamic or space-constrained labels retain their former font when
neither system size fits their admitted bounds. Their full conversion remains
open until their scrolling, status and layout behavior can be verified.

## Menus and navigation

Settings and other menus use flat rows with a semantic icon at the left, a
label, an optional value beneath it, and a thin separator. The whole row is the
touch target. Selected and unavailable states remain distinguishable without
changing the row's meaning. Settings actions, retries and confirmation choices
also use icon-and-separator rows in the content area. Destructive actions retain
a separate confirmation screen and require an explicit released touch.

Swipe horizontally to navigate pages. Multipage views show the shared position
marks where the total is known; an incomplete cursor must not invent a total.
Swipe down from the top status area to return Home, subject to the existing
pairing and pending-operation exit rules. Keep a Back control only for a real
parent route and preserve its full touch bounds.

Wi-Fi networks follow the same row language. Display real scan results, allow
horizontal paging, and preserve access to the full selected SSID. Do not invent
network or signal observations in Studio.

## Setup, QR and device information

During initial setup the violet perimeter animation indicates that setup has
not yet completed. It clears when both Wi-Fi and the Codex connection are
observed connected; later disconnects do not turn it back into a first-run
indicator. Factory reset restores first-run behavior.

QR screens prioritize a title and a readable code with its complete quiet zone.
A tap temporarily reveals status and perimeter chrome; a right swipe returns
through the actual prior route, including pairing cleanup when applicable.

**Settings > Device > About** opens the existing System, Memory,
Firmware, VibeWare and project QR pages. Hardware data must come from the
canonical snapshot and must retain unavailable, stale and error states.
