# ESP32 Luna Shell / Cody Companion

Two interfaces share an Espressif hardware platform:

- **Cody Companion** is the default native 2D character/interface, using `CODY2D`.
- **Luna Shell** uses official ESP-Brookesia 0.8 System Super/Core and declarative
  GUI packages, with LVGL underneath.

The current adapter is the Waveshare ESP32-S3 Touch AMOLED 1.75: round 466 x 466
display, touch, 16 MB flash and 8 MB PSRAM. The supported SDK is **ESP-IDF 6.0.2**.
Board, interface, optional services and Debug/Release mode are separate choices.
Other Espressif boards require explicit integration; see [BSP](docs/bsp/README.md).

## Start with the task

| Task | Guide |
| --- | --- |
| Use Cody or recover setup/pairing | [Cody navigation](docs/cody/navigation.md) |
| Develop native character/screens | [Cody](docs/cody/README.md) |
| Develop LVGL/Brookesia screens | [Luna Shell](docs/luna-shell/README.md) |
| Work on display/touch, services or a board | [BSP](docs/bsp/README.md) |
| Install and operate CCP on Windows | [CCP](plugins/cody-codex-plugin/README.md) |
| Build, generate, capture or run an authorized check | [Public console](scripts/README.md) |

[Documentation](docs/README.md) is the complete guide and atlas index.
Repository documentation, device text and logs are English; Cody text is
bounded printable ASCII.

## Work and evidence

[AGENTS.md](AGENTS.md) owns repository rules and authorization.
[TODO.md](TODO.md) owns open work, [planned-todo.md](planned-todo.md) deferred
work, [done.md](done.md) dated checkpoints/evidence, and [lessons.md](lessons.md)
recurring engineering lessons. Build/host/flash results and physical behavior
retain separate evidence levels.

Use `scripts/device.ps1` as the public CLI/menu. Operations follow the explicit
authorization rules; help and list are read-only entry points. Builds, logs,
captures and temporary output belong in `artifacts/`.

## Source ownership

| Area | Owner |
| --- | --- |
| `main/` | Profile composition and startup orchestration |
| [CODY_companion](components/CODY_companion/README.md) | Character runtime, scenes, model and diagnostics |
| [CODY2D](components/CODY2D/README.md) | Native 2D primitives, text and controls |
| [LUNASHELL_platform](components/LUNASHELL_platform/README.md) | Shared hardware, snapshots and services |
| [LUNASHELL_app](components/LUNASHELL_app/README.md) | Luna documents, chrome and navigation |
| [LUNASHELL_boot](components/LUNASHELL_boot/README.md) | Boot UI and audio |
| [LUNASHELL_logger](components/LUNASHELL_logger/README.md) | Canonical logging and readback |
| [LUNASHELL_tof400f](components/LUNASHELL_tof400f/README.md) | Optional distance transport |
| `plugins/`, `assets/`, `config/`, `scripts/` | CCP, authored resources, firmware composition and tooling |

---

<p align="right">
  <a href="https://umbertogiacobbi.biz/vibeware/manifesto">
    <img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="160">
  </a><br>
  <strong>VibeWare</strong><br>
  <em>Human intent, AI execution, and plenty of tokens</em>
</p>

### What is VibeWare?

I coined the term [VibeWare](https://umbertogiacobbi.biz) and started an open initiative for developers who build with AI and care about the craft. I challenge the assumption that vibe coding is synonymous with low-quality code, while openly acknowledging the weaknesses and risks of AI-generated software: experienced developers must guide the process, test carefully, check security and take responsibility for the result. With this footer, I acknowledge that I developed this software with AI and ask you to judge the quality of the work.

[**Read the VibeWare manifesto**](https://umbertogiacobbi.biz/vibeware/manifesto)

*You are welcome to explore, adapt and reuse the open-source VibeWare materials under the project license — my contribution to the developer community.*
