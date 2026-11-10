# Cody Companion

Native, English-only interface for Codex. The device profile uses `CODY2D`
and platform snapshot and service APIs. `CODY_device_model` owns observations
and route state. `CODY_device_ui` owns input, lifecycle and frame retirement.
`CODY_device_view` prepares native presentation. `CODY_pixel_demo` prepares the
Muse startup view. `CODY_pixel_avatar` adapts Muse's procedural renderer to the
same logical coordinates and RGB565 display strips.

| Folder | Responsibility |
| --- | --- |
| [src/runtime](src/runtime/README.md) | Runtime ownership, lifecycle and settings |
| [src/scenes](src/scenes/README.md) | Scenes, onboarding, appearance and teletype audio |
| [src/model](src/model/README.md) | Rig, expressions and snapshot-driven response |
| [src/rendering](src/rendering/README.md) | Avatar raster and damage planning |
| [src/diagnostics](src/diagnostics/README.md) | HUD, status, screenshots and accounting |
| [src/demos](src/demos/README.md) | Authored animation bindings |

Assets stay under `assets/cody`; generated resources are versioned firmware
inputs. See [Cody architecture](../../../docs/cody/README.md) for behavior
and [the console](../../../scripts/README.md) for authorized workflows.

The avatar uses Cody's existing touch input and audio owner. Display state
advances on retired frames and sound follows the device's current audio policy.

Muse renders on its original 64 by 64 indexed grid. The serialized display
owner can supply `MuseVisualStyle` to `compose_muse_frame`: pose movement,
hand and body offsets, gaze and eye openness; particle count, size, speed,
radius, vertical scale, twinkle, brightness and color; symbol identity, offset,
scale, color and bobbing speed; and pose transition time. Invalid inputs are
rejected before rendering. Default particle settings preserve the original
state-tinted orbit in front of and behind Muse. These are native configuration
parameters, not settings exposed in the device UI.

While Muse is visible, current IMU acceleration from the device snapshot adds a
bounded, smoothed body and gaze response in the display's logical orientation.
The response returns toward its resting pose as motion settles and resets when
the sample is unavailable, stale or the mascot is hidden. It does not replace
the showcase poses or read the sensor directly.

The artistic showcase retains the original modes and includes question,
exclamation, idea, thought, heart, sweat, tension and sleep symbols. Its
interval defaults to ten seconds of visible mascot time. It does not represent
real device or Codex events, and it does not play symbol sounds. The host
preview uses the same showcase and renderer.

On first import of an external library or prebuilt binary, stage and scan the
archive and extracted imported files with an explicit targeted Microsoft
Defender scan before use; do not retain scan records.
