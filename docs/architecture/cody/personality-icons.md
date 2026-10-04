# Cody personality symbols and delayed follow

The native 2D renderer adds one fixed-palette manga symbol above Cody. All skins
share the same fourteen masters. The default is `PersonalityCue::None`; a
caller explicitly selects a single symbol through the session API. Selection
does not alter expression presets, settings, sensor acquisition or persistence.

```cpp
#include "CODY_personality.hpp"

// Handle each returned esp_err_t in the caller's existing error path.
const esp_err_t narrative_result = CODY::set_personality_cue(CODY::PersonalityCue::Joy);
const esp_err_t device_result = CODY::set_personality_device_cue(CODY::PersonalityCue::LowBattery);
```

The second request replaces the first; there is no stack or priority arbitration.
The getter reports the requested cue, not proof that a display frame retired.
Narrative selection rejects device identities. Device selection admits only
None, LowBattery, Charging or NoWifi and never manufactures their truth.

Device requests read only a coherent `DeviceSnapshotService` projection, not
sensors/BSP/I2C. Their component must be Ready, valid, non-stale, error-free and
have an original successful publication timestamp. LowBattery also requires a
valid percentage in [0,100], and reuses the existing status-overlay low threshold
of 15 percent. Charging follows the published charging state independently of
percentage validity; unknown current states return their real contract error.
NoWifi requires a confirmed started/configured Wi-Fi radio with no connected
link; unknown, stale, failed and unconfigured state is not a disconnection claim.
Unavailable or inapplicable device observations suppress the device symbol;
the canonical status layer retains its existing truthful unavailable/error text.
Narrative sleep, fear, hunger, thirst, heat and cold are artistic story cues,
not measured physiological or environmental facts.

## Typed existing sizes

`CodySize` represents every current authored nominal presentation. Breathing,
root motion, depth and entrance interpolation still use continuous geometry;
they cannot accidentally turn an ordinary character into Micro.

| Stable enum | Nominal scale | Existing presentation |
| --- | ---: | --- |
| Micro | 22 | Scan-to-pair QR header; symbol intentionally suppressed |
| Tiny | 30 | Debug-text header |
| Preferences | 42 | Make-it-yours/settings header |
| Look | 44 | Cody-look header |
| Pairing | 46 | Pairing header |
| Small | 58 | Install-app header |
| Invitation | 65 | Pair-invitation header |
| Medium | 105 | Pairing-failed/online and connection scenes |
| Greeting | 130 | Welcome greeting |
| Full | 145 | Ordinary rig/lifecycle placement |

Every non-Micro size supports the selected symbol. The runtime classifies the
unanimated authored target scale; the existing free-scale rig preview selects
the nearest named size, with stable enum-ID tie breaking. Only nominal scales
at or below 22 are Micro. Invalid/nonfinite/out-of-contract scales are errors.
None and Micro cause no sprite pixel work.

## Motion, layout and frame ownership

Cody's already evaluated crown provides the horizontal follow anchor. The
existing upper-left/right lobes provide a small bounded squash ratio (0.9-1.1).
Vertical clearance uses the actual prepared jelly silhouette top, or the
evaluated vector-body extent, rather than assuming a lobe anchor is the contour
edge. The single symbol follows position and width/height through an analytical
first-order response with an 85-ms time constant. This creates a soft delayed
parallax response without another body mesh, texture warp or full-canvas effect.

Requested support is proportional to character scale, bounded to a 64 x 64
maximum and 12-pixel minimum. Where visible chrome narrows the gap above a large
deformed Cody, the renderer reduces the support into that actual gap, preserving
the filtered bottom anchor and keeping it above the body. A bounded set of
upward/lateral candidates preserves the scene title, all content/QR quiet-zone
bounds, existing content controls, status strip and complete lateral Back target.
For a constant-width rectangle in a convex circle, the farther endpoint row is
the narrowest chord; one square root proves every occupied row fits the circle.
Invalid geometry or physically impossible requested placement logs the real
context and returns an error. It is not silently hidden as a successful frame.
Extreme motion/layout admission and the minimum-size readability need validation.

Lag re-primes on cue, nominal size, route or cardinal orientation change, wake,
invalidated frame ownership, reversed time or a gap above 250 ms. Paused frames
do not advance it. Core graphics and coordinates stay in the shared logical
orientation; text/touch are not rotated separately. There are no hit targets on
the decorative symbol.

The immutable `PersonalityFrame` travels with `RigScene`. Changed identity or
bounds invalidate both old and new footprints through the existing paired-row
dirty planner. Fade invalidates symbol footprints with the other artwork;
complete display retirement alone commits the previous frame. Rendering uses
integer nearest sampling of the index and coverage planes and blends directly
into the current compact RGB565 DMA tile with its declared byte order.

## Static resource estimates and limits

- Master index/coverage planes: 14 x 64 x 64 x 2 = 114,688 flash bytes.
- Exact RGB565 palette: 512 flash bytes. Total payload: 115,200 bytes, plus
  descriptors (224 bytes under the normal 32-bit ESP ABI), alignment and code.
- Artistic filter: one fixed state (approximately 48 bytes under the ESP ABI).
  Each current/previous scene adds a small cue descriptor. These are source
  estimates, not compiled structure/heap measurements.
- One active cue visits at most 4,096 destination pixels per complete frame,
  independent of the number of catalog symbols or skins. At the existing 25-Hz
  jelly target this bounds cue sampling to 102,400 pixels per second; it does
  not predict CPU time, blend cost, achieved FPS or power consumption.
- Common placement uses one filter `expm1` and one circle `sqrt`; the bounded
  admission search has at most 24 candidates, independent of display area.
  Vector-only clearance also visits the already evaluated body vertices once.
- No new full framebuffer, render task, native per-frame allocation, sensor
  acquisition or per-pixel blob deformation is introduced.

The existing paired-row planner combines nearby/distant sprite and body
footprints into conservative tile rectangles. Old/new bounds and intervening
gaps can increase transferred pixels beyond the cue's own 4,096-pixel support.
The display-wide upper bound remains the 466 x 466 canvas (217,156 pixels per
complete redraw); a 1.89-percent cue area is not a CPU or transfer-overhead
percentage. Target profiling must compare complete frames, transfer spans,
stack headroom and actual artwork/personality substages. `RigRasterProfile`
records personality work separately from vector work while including it in
total artwork time.

The Windows simulator source loads the same index/coverage planes and stable
identities, uses the same follow/admission equations and native integer sampling,
and offers an explicitly synthetic host-symbol selector. Host image allocation,
Pillow compositing and timing do not establish firmware performance. It has not
been run for this change.

The branch started from main `290b6da3` in the isolated worktree
`E:\ESP32 Luna Cody Emotions` (`feat/personality-icons`). Another renderer
optimization worktree is independent; integrating later main/renderer changes
may require reconciling `RigScene`, dirty planning and draw-stage profiling.
No merge, push or physical operation belongs to this implementation checkpoint.
