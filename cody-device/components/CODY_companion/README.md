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
