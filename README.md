# ESP32 Luna Shell / Cody Companion

![Platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-0078D4)
[![License: MIT](https://img.shields.io/badge/license-MIT-2EA44F)](LICENSE)

A small round screen, a little personality, and a surprising amount of C++.

**Cody Companion** is a **Codex plugin and a little device for your desk**.
The plugin brings Codex's hooks out of the app and into the physical world:
Cody shows what your coding assistant is doing, which model it's using and how
much context is left. Codex can send notifications and questions to Cody, and
you can tap an answer on Cody to send it back to the chat. It goes both ways.

Setup starts on your phone: Bluetooth helps you configure Cody's Wi-Fi, so
there's no password to peck out on a tiny round screen. Then you pair it with
the Codex plugin using the code shown on Cody. From there, device and plugin
communicate over your **local Wi-Fi network**. Remote connections are on the
wish list; for now, they share the same neighborhood.

Cody does more than sit there looking round:

- **Wake word and standby.** With wake-word listening enabled, Cody recognizes
  **Sophia** locally and can wake from listening standby. On battery it can take
  inactivity naps; powered over USB and connected to Codex, it stays awake for work.
- **A bit of personality.** Touch interaction, animations and sound cues bring
  activity and notifications to life. Yes, you can pet your coding sidekick.
- **Make it yours.** Pick a look, switch sounds on or off, and configure rotation,
  shake-to-wake and Desk/Away behavior.

**Luna Shell** is the other interface in this repository, built on Espressif's
ESP-Brookesia and sharing the hardware layer. Both are works in progress.
Cody already has opinions about your token budget; polishing the rest is my job.

## The little round thing

The default target is the **Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS** with
16 MB of flash. For the **1.75C** with 32 MB of flash and the aluminum case
pictured below, select `-Board waveshare-esp32s3-touch-amoled-1.75c`. Both
use a round 466 × 466 display. Their pin assignments and onboard peripherals
differ. The **1.43C** has a hardware reference only. [Board comparison](docs/bsp/boards/purchased-amoled-comparison.md) · [Open hardware checks](TODO.md)

<p align="center">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-2.jpg" alt="Waveshare 1.75C front view with aluminum case and wrist strap" width="300">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-6.jpg" alt="Waveshare 1.75C angled front view" width="300"><br>
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-4.jpg" alt="Waveshare 1.75C side view with USB cover and button" width="300">
  <img src="hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/product-photo-7.jpg" alt="Waveshare 1.75C aluminum case from the rear" width="300"><br>
  <em>Official Waveshare product photos. Image rights belong to their respective owners.</em>
</p>

Standby, wake and battery behavior are described in the
[power guide](docs/bsp/services/power-management.md).

## Pick a door

- **Use Cody:** [setup and navigation](docs/cody/navigation.md), then the
  [PC plugin](plugins/cody-codex-plugin/README.md).
- **Desktop widget:** [OpenAI Usage](widgets/codex-status/README.md), a standalone
  Windows/Linux widget for Codex allowance, tokens, credits and reset countdown.
- **Build something:** [Cody's native 2D interface](docs/cody/README.md),
  [Luna's Brookesia interface](docs/luna-shell/README.md), or the [shared hardware layer](docs/bsp/README.md).
- **Find the details:** [documentation and atlases](docs/README.md),
  [device console](scripts/README.md), and [open work](TODO.md).

Development uses **ESP-IDF 6.0.2** and, for Luna, **ESP-Brookesia 0.8** with LVGL.
Cody uses its own native 2D renderer. `scripts/device.ps1` is the command-line
entry point; [AGENTS.md](AGENTS.md) has the working agreements.

## The small print, kept small

I'm not affiliated with, sponsored by, or employed by **Espressif** or
**Waveshare**. This is my independent project, not an official product from
either company. All third-party names, trademarks, logos and images belong to
their respective owners.

My source and documentation use the [MIT License](LICENSE). Third-party code,
models and assets keep their [own licenses and notices](THIRD_PARTY_NOTICES.md);
the VibeWare identity has its own [reuse guidance](https://github.com/umbertotechnopreneur/VibeWare/blob/main/PRESS-KIT.md).

On first import, check the official source and version, then run a targeted
Microsoft Defender scan of the download and extracted dependencies. Use the
import after the scan completes without detections.

---

### This is VibeWare · [Manifesto](https://umbertogiacobbi.biz/vibeware/manifesto?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-manifesto)

ESP32 Luna Shell / Cody Companion participates in the **VibeWare initiative**,
an open movement for developers who build useful software with AI under human
direction. VibeWare describes participation; it is not the project's author
or publisher.

I am [Umberto Giacobbi](https://umbertogiacobbi.biz/?utm_source=github&utm_medium=referral&utm_campaign=esp32-luna-shell&utm_content=readme-author).
I guide this project's development and remain responsible for checking the
result. The project keeps its own identity, license and release status.

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
