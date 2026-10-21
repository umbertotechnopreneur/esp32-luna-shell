# ESP32 Luna Shell / Cody Companion

Two interfaces share an Espressif hardware platform:

- **Cody Companion** is the default native 2D character/interface, using `CODY2D`.
- **Luna Shell** uses official ESP-Brookesia 0.8 System Super/Core and declarative
  GUI packages, with LVGL underneath.

The current adapter is the Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS: round 466 x 466
display, touch, 16 MB flash and 8 MB PSRAM. The supported SDK is **ESP-IDF 6.0.2**.
Board, interface, optional services and Debug/Release mode are separate choices.
Other Espressif boards require explicit integration; see [BSP](docs/bsp/README.md).

## Why I chose Waveshare for Cody

I chose and bought the **Waveshare ESP32-S3-Touch-AMOLED-1.75C**, with its CNC-machined
aluminum-alloy case, for Cody Companion. Its compact
round form factor suits a small desktop character, and the ESP32-S3, 8 MB PSRAM
and 32 MB flash make it an attractive platform for this project. Waveshare's
hardware quality and the balance of features and price were the reasons I chose
this direction; that is my personal assessment as a paying customer.

<p align="center">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-2.jpg" alt="Waveshare ESP32-S3-Touch-AMOLED-1.75C front view with silver aluminum case and wrist strap" width="300">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-6.jpg" alt="Waveshare ESP32-S3-Touch-AMOLED-1.75C angled front view showing the round display and side controls" width="300"><br>
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-4.jpg" alt="Waveshare ESP32-S3-Touch-AMOLED-1.75C side view showing the USB port cover and button" width="300">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-7.jpg" alt="Waveshare ESP32-S3-Touch-AMOLED-1.75C angled rear view showing the aluminum case" width="300"><br>
  <em>Waveshare ESP32-S3-Touch-AMOLED-1.75C. Manufacturer product photos;
  image rights remain with their respective owners.</em>
</p>

Cody was developed and tested on the **Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS**;
the **1.75C pictured above** is the model I chose for its aluminum case.
Technical board details are in the [hardware guide](docs/bsp/boards/purchased-amoled-comparison.md).

Cody and Luna Shell keep the interface separate from the hardware, allowing
adaptation to other Espressif devices. Waveshare is my chosen starting point.

## Independence and trademarks

I am not affiliated with, sponsored by, or employed by **Espressif** or
**Waveshare**. ESP32 Luna Shell and Cody Companion are independent projects;
neither company endorses or certifies them.

All third-party product names, trademarks, logos and images belong to their
respective owners. They are used here to identify the hardware, software and
reference material involved. Their inclusion does not imply a partnership or
endorsement. Third-party materials retain their applicable licenses and notices;
see [third-party notices](THIRD_PARTY_NOTICES.md).

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

## Observed battery runtime

**About 7 hours with a 1,000 mAh battery** in the tested Cody Debug configuration,
with wake-word listening and the pretrained activity observer enabled. The
12-13 October 2026 run retained a last-alive checkpoint at **6 h 55 min 42 s**,
including standby. This is one observed run; incomplete power-source observations
and the unknown final interval limit its precision. See the
[test configuration and saved evidence](docs/bsp/services/power-management.md#observed-1000-mah-run---12-13-october-2026).

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

## License and attribution

Project-owned source and documentation are available under the [MIT License](LICENSE).
Keep the copyright and permission notice when reusing covered material.
Third-party components, models and assets retain their own licenses and notices;
see [third-party notices](THIRD_PARTY_NOTICES.md). Firmware distributions must
respect the licenses of the components included in their selected profile.

The VibeWare name and logo follow the initiative's
[identity and reuse guidance](https://github.com/umbertotechnopreneur/VibeWare/blob/main/PRESS-KIT.md).
The shared Terms and Privacy pages below accompany this project; they do not
replace its license or the CCP documentation about audio and external services.

## A note on security

I care about what goes into this project. Before I add a third-party library or prebuilt binary, including files supplied without source code, I scan the downloaded package, its extracted files and any dependencies with Microsoft Windows Defender. I keep new imports aside until the scan finishes without detections.

I keep a record of where each new import came from and the scan result. If a scan cannot finish or flags something, I leave it out.

---

### This is VibeWare · [Manifesto](https://umbertogiacobbi.biz/vibeware/manifesto?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-manifesto)

ESP32 Luna Shell / Cody Companion participates in the **VibeWare initiative**,
an open movement for developers who build useful software with AI under human
direction. VibeWare describes participation; it is not the project's author
or publisher.

I am [Umberto Giacobbi](https://umbertogiacobbi.biz/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-author).
I guide this project's development and remain responsible for checking the
result. The project keeps its own identity, license, release status and
validation evidence.

To learn what VibeWare means and why I started it, read the manifesto above.

[ESP32 Luna Shell on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) ·
[VibeWare on GitHub](https://github.com/umbertotechnopreneur/VibeWare) ·
[umbertogiacobbi.biz](https://umbertogiacobbi.biz/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-website)

*Explore, adapt and reuse the project materials under their applicable licenses.*

[Terms](https://umbertogiacobbi.biz/terms/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-terms) ·
[Privacy](https://umbertogiacobbi.biz/privacy/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-privacy)

<p align="right">
  <a href="https://umbertogiacobbi.biz/vibeware/manifesto?utm_source=github&amp;utm_medium=referral&amp;utm_campaign=esp32-luna-shell&amp;utm_content=readme-logo">
    <img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="160">
  </a><br>
  <em>Human Intent, AI Execution, Plenty of Token</em>
</p>
