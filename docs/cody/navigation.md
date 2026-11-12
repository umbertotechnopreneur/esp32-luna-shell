<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Getting around Cody

**VibeWare · Cody Companion**

**Author:** Umberto Giacobbi · **Created:** 2026-10-11 · **Updated:** 2026-11-12 · **Revision:** 1.0

Cody is designed to feel at home on your desk. Here is a quick tour of the experience represented in the current source. Availability and exact screens depend on the build, device and connection.

## Start and connect

On startup, the VibeWare floppy animation welcomes you. On a fresh setup the birth introduction is followed by six tour pages: buttons, touch, Sofia, connections, the Cody project QR and a thank-you from the development team. Swipe or use the lateral controls to move between pages; Skip tour continues to setup. The current page is saved before moving, so the tour resumes there after a restart. Quick guide in Settings reopens the tour without changing the first-run decision. The project QR is also available in About. The tour and its artwork are source changes awaiting build and physical verification.

At first boot, a one-time 4.6-second melody accompanies the floppy animation. The last frame remains visible while an admitted melody finishes; muted, quiet-hour and unavailable-audio states do not hold the screen for an unplayed cue. The one-time choice is persisted before playback is requested. Subsequent cold starts retain the existing startup ding. Sound and quiet-hour preferences still apply, and physical playback remains unverified.

Home can show the saved Counter mode or Cody; Counter mode is enabled by default when there is no saved preference. With Counter mode off, Cody cycles through showcase poses. Those poses are a playful preview, not a claim about a real task or sensor.

A short **BOOT** press opens the menu for Settings, Events and Counters. Holding it for two seconds opens Options in builds without voice input, or requests voice input in builds that include it. **PWR** puts Cody in standby or wakes it; holding it for three seconds requests power off. Swipe across pages to explore, or swipe down from the top status area to return Home. Controls fade after inactivity and reappear on touch or detected motion.

Choose **Connections** to set up Wi-Fi and link Codex. The Wi-Fi guide offers a local network list and Bluetooth setup, with QR screens for the app and pairing. Scan the pairing QR inside the Espressif app. Follow the device prompts and keep the PC and Cody on the appropriate network. The [pairing quickstart](cody-ccp-pairing-quickstart-a5-v1.pdf) and [PC plugin guide](../../cody-desktop/plugins/cody-codex-plugin/README.md) have the details.

## Dashboard and Options

Explore connection settings, display and behavior preferences, device information and available Codex views. The screens depend on current data and settings. Missing or stale information is identified instead of being presented as live. The [navigation map](device-navigation-map-a4.pdf) gives you the bigger picture.

In **Behavior > Idle display**, choose whether inactivity returns to Cody or
Counters, set the delay, enable optional event reactions and select a parallax
level. The globe screensaver and power standby keep their own policies. Under
**Cody > Themes**, choose one of eight character palettes. The Device pages
include Third party and a small OpenRouter model-price selector; its prices are
indicative OpenRouter API rates, not Codex subscription credits.

The Codex hub offers views of chats, activity, events and statistics when the connected source provides them. Saving a chat on Cody is a local bookmark; it does not change the chat followed on the PC. When Cody offers a question, review the choices and explicitly send your answer. A request or pending send is not proof of delivery. Native PC approvals still happen on the computer.

Sounds follow the device settings and quiet hours. Voice features vary by build: the default lightweight profile does not acquire microphone input, and the wake-word option does not itself enable Codex dictation. The [voice notes](runtime.md#one-way-voice-input-source-increment) explain the distinction.

## Manage the connection

A saved Wi-Fi network and paired PC can reconnect when available. Wi-Fi and the authenticated Codex connection are separate: being online does not necessarily mean the PC is connected. For recovery, follow the action shown on screen. Replacing or forgetting a paired PC requires explicit confirmation; forgetting it on Cody does not revoke the PC's own record.

Restart and factory reset are different choices. Restart keeps saved settings; factory reset asks for confirmation and clears Cody's saved setup and preferences. Follow the prompts carefully.

## Native text entry

Where offered, local Wi-Fi entry lets you choose a network and enter its credentials on the device. Keep passwords private. Exact controls and admission rules live in the [onboarding source](../../cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp).

See the [screen catalog](screen-catalog.md) for original screen IDs and retained designs, and [open work](../../TODO.md) for outstanding verification.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
