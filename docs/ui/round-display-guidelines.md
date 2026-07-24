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

## Cardinal action caps

The four curved margins outside the safe square are interaction caps. Each cap
hosts at most one visible action.

| Cap | Default role | Visual slot |
| --- | --- | --- |
| Left | Back, or Cancel in a modal flow | `x=12, y=169, w=56, h=128` |
| Right | Next, or OK on a final step | `x=398, y=169, w=56, h=128` |
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

The boot screen contains no buttons. It should remain visible only as long as
required to initialize the system, with the SFX started once audio output is
ready.

## ESP-Brookesia integration boundary

These coordinates and roles are framework-independent design tokens. When the
ESP-Brookesia application shell is introduced, expose them through the shared
theme/layout layer and keep app documents inside the 320 x 320 safe-content
container. Cardinal controls belong to the shell, not to each app's private
layout.
