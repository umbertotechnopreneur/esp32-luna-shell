<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Getting around Cody

**VibeWare · Cody Companion**

**Author:** Umberto Giacobbi · **Created:** 2026-10-11 · **Updated:** 2026-11-13 · **Revision:** 1.0

Cody is designed to feel at home on your desk. Here is a quick tour of the experience represented in the current source. Availability and exact screens depend on the build, device and connection.

## Start and connect

On startup, the VibeWare floppy animation welcomes you. On a fresh setup the birth introduction is followed by Welcome and Let's connect. Choose Wi-Fi setup or Later to continue offline. A successful Wi-Fi setup shows its result and Continue before the guide. The guide covers buttons, touch, Sofia when supported, connections, the optional Codex connection and a final All set page. Swipe or use the lateral controls to move between guide pages; Skip tour moves to the Codex choice. The current guide page is saved before moving, so the tour resumes there after a restart. Before a connection or offline choice is saved, a restart returns to Welcome. Quick guide in Settings reopens the tour without changing the first-run decision. The project QR is also available in About. The tour and its artwork are source changes awaiting build and physical verification.

At first boot, a one-time 4.6-second melody accompanies the floppy animation. The last frame remains visible while an admitted melody finishes; muted, quiet-hour and unavailable-audio states do not hold the screen for an unplayed cue. The one-time choice is persisted before playback is requested. Subsequent cold starts retain the existing startup ding. Sound and quiet-hour preferences still apply, and physical playback remains unverified.

Home can show the saved Counter mode or Cody; Counter mode is enabled by default when there is no saved preference. With Counter mode off, Cody cycles through showcase poses. Those poses are a playful preview, not a claim about a real task or sensor.

A short **BOOT** press opens the menu for Settings, Notifications, Counters and About. Holding it for two seconds opens Options in builds without voice input, or requests voice input in builds that include it. **PWR** puts Cody in standby or wakes it; holding it for three seconds requests power off. Swipe across pages to explore, or swipe down from the top status area to return Home. Controls fade after inactivity and reappear on touch or detected motion.

Choose **Connections** to set up Wi-Fi and link Codex. Offline recovery opens the same Wi-Fi choice with Cody. The guide offers a local network list and Bluetooth setup, with QR screens for the app and pairing. Scan the pairing QR inside the Espressif app. Follow the device prompts and keep the PC and Cody on the appropriate network. The [pairing quickstart](cody-ccp-pairing-quickstart-a5-v1.pdf) and [PC plugin guide](../../cody-desktop/plugins/cody-codex-plugin/README.md) have the details.

## Dashboard and Options

Explore connection settings, display and behavior preferences, device information and available Codex views. The screens depend on current data and settings. Missing or stale information is identified instead of being presented as live. The [navigation map](device-navigation-map-a4.pdf) gives you the bigger picture.

In **Behavior > Idle display**, choose whether inactivity returns to Cody or
Counters, set the delay, enable optional event reactions and select a parallax
level. The globe screensaver and power standby keep their own policies. Under
**Cody > Themes**, choose one of eight character palettes. The Device pages
include Third party and a small OpenRouter model-price selector; its prices are
indicative OpenRouter API rates, not Codex subscription credits.

The Codex hub offers Chats, Activity, Notifications and Statistics when the
connected source provides them. Saving a chat on Cody is a local bookmark; it
does not change the chat followed on the PC. Activity shows the observed current
operation as an icon and brief label; labels that exceed their space end in `...`.
Missing live data is shown on the selected page rather than moving you elsewhere.

Notifications keeps questions and unread replies, PC approval notices, checks,
pull requests, local messages and other notices together. Local records are
available without a PC; their 32-entry RAM history keeps read rows until
overflow or device restart and reports dropped records. PC notices retain their
own broker lifecycle. Each row separates the event type
from its originating chat. Tap a row for the same complete detail used by an
incoming notification. Swipe vertically or tap the text area to read its pages.
Going Back keeps the notice unread; use **Mark as read** for informational
notices, and wait for confirmation. An optional character reaction points to
the same retained event, so its short animation is not the only chance to find it.
Offline or saved information is identified, with remote actions unavailable.

For a question, choose **Choose a reply**, review the complete choice and
explicitly send your answer. A request or pending send is not proof of delivery.
Native PC approvals still happen on the computer; reading their notice on Cody
does not approve them. The Notifications and Activity refactor is implemented
in source and still needs separately authorized build and device acceptance.

Sounds follow the device settings and quiet hours. Voice features vary by build: the default lightweight profile does not acquire microphone input, and the wake-word option does not itself enable Codex dictation. The [voice notes](runtime.md#one-way-voice-input-source-increment) explain the distinction.

## Manage the connection

A saved Wi-Fi network and paired PC can reconnect when available. Wi-Fi and the authenticated Codex connection are separate: being online does not necessarily mean the PC is connected. For recovery, follow the action shown on screen. Replacing or forgetting a paired PC requires explicit confirmation; forgetting it on Cody does not revoke the PC's own record.

Restart and factory reset are different choices. Restart keeps saved settings; factory reset asks for confirmation and clears Cody's saved setup and preferences. Follow the prompts carefully.

## Native text entry

Where offered, local Wi-Fi entry lets you choose a network and enter its credentials on the device. Keep passwords private. Exact controls and admission rules live in the [onboarding source](../../cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp).

See the [screen catalog](screen-catalog.md) for original screen IDs and retained designs, and [open work](../../TODO.md) for outstanding verification.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
