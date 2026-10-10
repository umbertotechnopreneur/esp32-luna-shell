# Luna Shell

Luna Shell is the LVGL/Brookesia interface. System Super owns system lifecycle
and services; System Core owns native application lifecycle; GUI LVGL renders
the declarative application documents. The supported framework is official
ESP-Brookesia 0.8 on ESP-IDF 6.0.2.

| Task | Guide |
| --- | --- |
| Configure the minimal shell or optional hardware services | [Configuration](configuration.md) |
| Understand boot and the Brookesia handoff | [Startup](startup.md) |
| Edit layout, chrome, Settings or local actions | [UI guidelines](ui-guidelines.md) |
| Select firmware composition or Debug/Release | [Toolchain and profiles](../bsp/toolchain-and-profiles.md) |

## Declarative application

Edit `components/LUNASHELL_app/gui/root.json`. The document generator emits the
resident `res/root.json` plus one lazy `res/screen_*.json` per non-Welcome route.
The resident document owns background, shared chrome, Welcome and the empty
content target. Only one lazy route is mounted at a time; leaving it releases
its document, subscriptions and callbacks.

C++ selects documents, routes actions and binds published values through the
official GUI interface. Shared styles and chrome remain in the JSON contract.
Search uses the System Super keyboard; destructive storage confirmation uses
its dialog service and the existing storage owner.

Current full-profile screens include Welcome, Logs, Hardware/detail,
Audio Scope/FFT/detail, IMU, Settings, About and Pairing. The `ui-minimal` shell
has its separate single-screen package and service lifecycle.

## Shared hardware and storage

Physical selection belongs to [BSP](../bsp/README.md). Luna consumes canonical
snapshots; a route never reads sensors itself. Log presentation consumes the
canonical logger ring. Missing or stale measurements expose their state.

Internal `/littlefs` holds application/framework resources; removable `/sdcard`
holds its separately owned user data and log mirror. Formatting and retries
follow [storage ownership](../bsp/services/storage-and-logging.md).
The current round-screen coordinates belong to the
[Waveshare geometry](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/display-geometry.md).

Source ownership is in the [application README](../../components/LUNASHELL_app/README.md).
Use the [console](../../scripts/README.md) for operations and the root
[work records](../README.md#work-and-evidence) for current gates and evidence.
