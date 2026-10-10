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

Physical board, interface, optional services and Debug/Release mode are separate
selections. The supported physical adapter is currently the Waveshare ESP32-S3
Touch AMOLED 1.75. Other Espressif devices are a future integration direction;
the folder structure does not claim additional firmware support.

## Illustrated Cody references

| Current document | Purpose |
| --- | --- |
| [Navigation Atlas](cody/cody-navigation-atlas-v1.pdf) | Routes, returns, questions and read-only PR updates |
| [Planning Atlas](cody/cody-planning-atlas-v1.pdf) | Screen inventory, layout and product boundaries |
| [Pairing Quickstart](cody/cody-ccp-pairing-quickstart-a5-v1.pdf) | Four-page A5 flyer for nontechnical users |
| [CCP Connection Atlas](cody/cody-ccp-connection-atlas-v1.pdf) | Combined connection, broker, persistence and security reference |
| [Manga Status Atlas](cody/cody-manga-status-atlas-v1.pdf) | Expressive cues, artwork and boot identity |

The former [service atlas](cody/cody-ccp-service-atlas-v1.pdf) and
[Codex connection atlas](cody/cody-codex-connection-atlas-v1.pdf) are historical
sources. Screen IDs remain stable independently of page numbers. PDF authoring
commands and translation provenance are in the [console guide](../scripts/README.md).
Document illustrations and simulator frames are synthetic evidence.

## Work and evidence

| Record | Purpose |
| --- | --- |
| [AGENTS.md](../AGENTS.md) | Repository rules and authorization |
| [TODO.md](../TODO.md) | Open work and remaining validation |
| [planned-todo.md](../planned-todo.md) | Deferred work and activation conditions |
| [done.md](../done.md) | Dated implementation and observed evidence |
| [lessons.md](../lessons.md) | Causes and practical lessons from recurring errors |

Guides describe source contracts. Build, host execution, flash and physical
behavior retain their own evidence levels and dates in the root records.
