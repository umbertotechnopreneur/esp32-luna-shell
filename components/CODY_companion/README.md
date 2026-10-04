# Cody Companion

Standalone, English-only character runtime. Uses `CODY2D` and platform snapshot
and service APIs; does not admit LVGL. Public contracts remain in `include/`.

| Folder | Responsibility |
| --- | --- |
| [src/runtime](src/runtime/README.md) | Runtime ownership, lifecycle and settings |
| [src/scenes](src/scenes/README.md) | Scenes, onboarding, appearance and teletype audio |
| [src/model](src/model/README.md) | Rig, expressions and snapshot-driven response |
| [src/rendering](src/rendering/README.md) | Pearl compositing and damage planning |
| [src/diagnostics](src/diagnostics/README.md) | HUD, status, screenshots and accounting |
| [src/demos](src/demos/README.md) | Authored animation and PoC bindings |

Assets stay under `assets/cody`; generated resources are versioned firmware
inputs. See [Cody architecture](../../docs/architecture/cody/README.md) for behavior
and [the console](../../scripts/README.md) for authorized workflows.
