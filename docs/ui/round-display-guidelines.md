# QSee.ai round display guidelines

## Device profile

The target is a circular 466 x 466 pixel AMOLED display. Its center is
`(233, 233)` and its nominal radius is 233 pixels.

The largest mathematical square inside that circle is about 329.5 pixels per
side. QSee.ai uses a deliberately smaller 320 x 320 pixel safe-content square,
positioned at `(73, 73)`. This leaves about 6.7 pixels of radial clearance at
each corner.

Everything required to understand or complete a screen must fit in that safe
square. Content may decorate the circular perimeter, but it must not depend on
the perimeter to remain usable.

## Runtime background cache

The full-screen radial atmosphere appears immediately as an LVGL gradient. If
the optional SD card is ready, a low-priority worker validates or creates a
versioned 466 x 466 RGB565LE cache with deterministic ordered dithering. The
validated pixels live in PSRAM for the process lifetime; LVGL never reads the
SD card during repaint. Missing media, invalid data, write failures, or an
unavailable PSRAM block leave the original gradient active, and this flow never
formats the card. One unrotated asset serves every hardware cardinal rotation.

## In-screen command groups

When a screen needs two or more local commands, place them on the lower arc
whenever the content permits it. Commands in the same group use equal
dimensions, symmetric spacing, the same gradient and bright border as the
Welcome action arc, and no pressed-state movement. Keep destructive actions
visually distinct and do not let the group replace the shell-owned Back action.

Lower-arc groups are adaptive. They are fully visible on route entry and fade
only through opacity after the configured inactivity timeout. Any touch or
meaningful IMU movement fades them back in and restarts the timer. Once fully
transparent, their click targets are disabled until fade-in begins. The
perimeter Back action is always excluded. The persisted timeout uses a real
dropdown with common values from 10 seconds through 10 minutes and defaults to
10 seconds.

## Compact diagnostic screens

Live diagnostics should favor one non-scrolling overview over vertically
stacked telemetry. Use a stable graphic for spatial values, keep each numeric
value beside the part of the graphic it describes, and retain the physical unit
on screen. The IMU view uses a color-matched X/Y/Z acceleration triad, a compact
three-segment gyroscope ring, and a thermometer icon while the numeric values
remain authoritative.

## Settings information architecture

Settings is one spacious vertical surface because the complete device
configuration no longer fits in a useful hub of tiny buttons. It uses native
dropdowns and switches, clear section spacing, and exposes only implemented
controls:

- Diagnostics: sampling period and IMU smoothing;
- Storage: SD state and logger mirroring;
- Wi-Fi and Bluetooth: connection state, credentials, scans, and provisioning;
- GPS: receiver state;
- Display and motion: brightness profiles, cardinal auto-rotation, and command visibility;
- Power: display dim and panel-off timing.

Each section keeps live status next to the relevant control. Password entry
uses a dedicated full-height editor: the password starts blank and masked, is
cleared whenever the editor closes, and is never copied into a diagnostic
snapshot. Provisioning steps use the same bottom command geometry and keep the
shell-owned Back control available.

## Cardinal action caps

The four curved margins outside the safe square are interaction caps. Each cap
hosts at most one visible action.

| Cap | Default role | Visual slot |
| --- | --- | --- |
| Left | Back, or Cancel in a modal flow | `x=10, y=175, w=60, h=116` |
| Right | Next, or OK on a final step | `x=396, y=175, w=60, h=116` |
| Top | Close, menu, status, or another non-destructive contextual action | `x=169, y=12, w=128, h=56` |
| Bottom | Contextual primary action such as capture, retry, or details | `x=169, y=398, w=128, h=56` |

Navigation must remain consistent across flows:

- left means go back or abandon;
- right means advance or confirm;
- destructive confirmation never appears in the top cap;
- missing actions disappear instead of leaving disabled placeholders;
- icon and short label are preferred when both remain legible;
- the visual control may be 48 pixels on its short side, but its touch target
  should reach at least 64 pixels and may overlap up to 8 pixels of the
  non-interactive safe-area gutter.

## Boot screen

The QSee.ai boot screen is the first device-visible frame. It uses the supplied
QSee.ai artwork without reinterpreting the logo, keeps the complete mark inside
the safe square, and plays one short boot SFX.

Production files:

- `assets/branding/qsee/generated/qsee-boot-466x466.png`
- `assets/branding/qsee/generated/qsee-boot-466x466.rgb565le.bin`
- `assets/sfx/generated/qsee-boot.wav`

The PNG is the canonical visual derivative. The raw file contains exactly
466 x 466 little-endian RGB565 pixels with no header. It is suitable for direct
frame-buffer loading when the firmware uses the same byte order. If the panel
transport swaps RGB565 bytes, perform that swap in the display pipeline rather
than silently changing the asset format.

The boot screen contains no buttons, live log, RichText panel, or shadows. Its
only dynamic text moves from `INITIALIZING` to `OPENING WELCOME`. It remains
visible only as long as required to initialize the system, with the SFX started
once audio output is ready. Immediately before System Super takes ownership,
the complete 466 x 466 frame is cleared to black and refreshed so no boot-art
strip survives around the application surface.

## ESP-Brookesia integration boundary

System Super is the device shell and service layer. System Core owns the
QSee.ai native application lifecycle, while GUI LVGL loads and renders
`components/qsee_app/package/res/root.json` as the resident background, chrome,
Welcome, and empty content target. Non-Welcome content is loaded from one
generated `res/screen_*.json` document at a time and unloaded when navigation
leaves it. LVGL remains an implementation detail beneath that JSON interface.

All current content screens center a 320 x 320 root surface on the 466 x 466
display. Screen structure, layout, styles, and actions belong in the editable
`components/qsee_app/gui/root.json`; the build generator splits that source
into the bootstrap and lazy runtime documents. C++ lifecycle code selects and
mounts those documents and may bind text and progress values through the GUI
interface, but it must not construct parallel screen-specific widgets.

These coordinates and cardinal roles remain shared design tokens. Any future
cardinal controls or system chrome must be provided through System Super or a
shared JSON theme/layout contract; individual screens must not redefine them.

Hardware-derived text and progress values come only from
`DeviceSnapshotService`. The Logs screen may consume the canonical logger ring.
System interactions such as keyboard input and destructive SD confirmation use
System Super services so every screen receives the same behavior.

Internal `/littlefs` stores the staged UI resources and the Brookesia HAL
adaptor provides its LittleFS/NVS storage interfaces without automatic
formatting. It is not the removable SD card, is not displayed as SD capacity,
and must never be substituted for `/sdcard`. QSee alone owns the removable SD
lifecycle and its card/transport-unconfirmed, ready, and format-required
states. The board has no card-detect pin, so probe failure is never rendered as
physical absence. Formatting requires exact `FR_NO_FILESYSTEM`, an explicit
destructive confirmation, and a one-shot permission bound to the observed CID
fields plus capacity. A poisoned or incomplete cleanup always takes display
priority over format-required, ready, or unconfirmed state and shows the exact
cleanup stage and error.
