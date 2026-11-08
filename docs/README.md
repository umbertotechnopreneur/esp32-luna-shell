# Documentation

Choose the task first. Each guide has one owner; commands, installation and wire
formats stay in their canonical guides.

| I want to... | Start here |
| --- | --- |
| Use Cody, finish setup or recover a connection | [Cody navigation](cody/navigation.md) |
| Develop the native character or its screens | [Cody](cody/README.md) |
| Develop the LVGL/Brookesia application | [Luna Shell](luna-shell/README.md) |
| Work on display/touch or integrate a board | [BSP](bsp/README.md) |
| Select a firmware profile, feature or build mode | [Toolchain and profiles](bsp/toolchain-and-profiles.md) |
| Build, generate assets, capture logs or run an authorized check | [Public console](../scripts/README.md) |
| Install CCP and review its hooks | [CCP installation and operation](../plugins/cody-codex-plugin/README.md) |
| Preview the interface on Windows | [Simulator](../simulator/windows/README.md) |

## Three documentation areas

- **Cody** owns the native 2D interface, character, routes and presentation.
- **Luna Shell** owns LVGL below Brookesia System Super/Core and declarative UI.
- **BSP** owns the display/touch baseline, shared service contracts and board
  details. Optional services do not become requirements of the minimal baseline.

Select the physical board independently of the interface, services and build
mode. The shared BSP supports the Waveshare 1.75 GPS (16 MB) and 1.75C
(32 MB). The 1.43C entry documents hardware compatibility only.
[Board comparison](bsp/boards/purchased-amoled-comparison.md)

## Source map

| Area | Responsibility |
| --- | --- |
| [main](../main/README.md) | Profile composition and startup |
| [CODY_companion](../components/CODY_companion/README.md) | Character, scenes, model and diagnostics |
| [CODY2D](../components/CODY2D/README.md) | Native graphics, text and controls |
| [LUNASHELL_platform](../components/LUNASHELL_platform/README.md) | Hardware, snapshots and services |
| [LUNASHELL_app](../components/LUNASHELL_app/README.md) | Luna documents and navigation |
| [LUNASHELL_boot](../components/LUNASHELL_boot/README.md) | Boot UI and audio |
| [LUNASHELL_logger](../components/LUNASHELL_logger/README.md) | Logging and readback |
| [LUNASHELL_tof400f](../components/LUNASHELL_tof400f/README.md) | Optional distance transport |
| `plugins/`, `assets/`, `config/`, `scripts/` | PC integration, resources, composition and tools |

## Illustrated Cody references

| Current document | Purpose |
| --- | --- |
| [Navigation Atlas](cody/cody-navigation-atlas-v1.pdf) | Routes, returns, questions and read-only PR updates |
| [Planning Atlas](cody/cody-planning-atlas-v1.pdf) | Screen inventory, layout and product boundaries |
| [Pairing Quickstart](cody/cody-ccp-pairing-quickstart-a5-v1.pdf) | Exactly four A5 instructional pages for local pairing; no added cover, contents or appendix |
| [CCP Communication Atlas](cody/cody-ccp-connection-atlas-v1.pdf) | Current local pairing and events, private relay with mounted SMB/Samba storage, and explicit integration limits |
| [Manga Status Atlas](cody/cody-manga-status-atlas-v1.pdf) | Preserved expressive artwork and current Studio/design boundaries |
| [A4 navigation map](cody/device-navigation-map-a4.pdf) | One-page device-first dashboard, Options and event routes |

Use screen IDs to match atlas pages to the firmware. The console guide covers
PDF generation, front matter and the Obsidian mirror. [Console guide](../scripts/README.md)

## Current work

| Record | Purpose |
| --- | --- |
| [AGENTS.md](../AGENTS.md) | Repository rules and authorization |
| [TODO.md](../TODO.md) | Open work and remaining validation |
| [planned-todo.md](../planned-todo.md) | Deferred work and activation conditions |
| [done.md](../done.md) | Latest validation round only |
| [lessons.md](../lessons.md) | Causes and practical lessons from recurring errors |

