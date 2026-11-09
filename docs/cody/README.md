# Cody

The `cody-companion` profile is a native `CODY2D` terminal for Codex. Its
device model projects account, network, battery and build data. The display
consumes those projections. The shared BSP and platform services own acquisition,
persistence, power and authenticated transport.

## Find the relevant contract

| Task | Guide |
| --- | --- |
| First boot, dashboard, Options, questions and recovery | [Navigation](navigation.md) |
| All device routes at a glance | [A4 navigation map](device-navigation-map-a4.pdf) |
| Display coordinates, action controls and QR | [Layout](layout.md) |
| Power and standby | [Power management](../bsp/services/power-management.md) |
| Native display transfers | [Display and touch runtime](../bsp/core/display-touch-runtime.md) |
| Muse reference and board/audio differences | [Hardware and audio comparison](muse-hardware-audio-comparison.md) |

Installation and PC operation belong to the [CCP guide](../../cody-desktop/plugins/cody-codex-plugin/README.md).
Packet formats and authentication belong to [CCP1](../../cody-desktop/plugins/cody-codex-plugin/protocol/CCP1.md).
Windows controls and synthetic scenarios belong to the [simulator guide](../../cody-device/simulator/windows/README.md).
Use the [A4 navigation map](device-navigation-map-a4.pdf) for the device-first
firmware. The [illustrated atlases](../README.md) also include character design
material used in Studio.

## Reference projects

- Use Agent Buddy as a reference for host event adapters, session priority and
  source badges. [Agent Buddy](https://github.com/DanWahlin/agent-buddy)
- Use Muse as the reference for procedural avatar drawing and motion with
  Cody's original artwork. Its board adapters and audio ownership are also
  useful references. [Muse Gadget SDK for ESP32](https://github.com/facebookincubator/muse-gadget-sdk/tree/main/esp32)

## Multi-agent direction

Keep one Cody avatar. Normalize events at the host bridge or relay, where each
integration can declare its supported events and actions. Identify events by
source, session and event identity. Show the source badge on the existing home
surface, with actionable attention ahead of work in progress and completed
work. Clear a request only when its own source reports the matching outcome or
expiry. Offer semantic replies only when an authorized route can deliver them
to the exact session. Native tool approvals stay on their originating host.

Include Dot as a planned connection alongside local Codex. The supported Dot
surface, authentication and delivery receipts are acceptance gates for the
existing [remote relay design](remote-integration.md).

## Native updates

The device renderer groups damage from the old and new content, controls, and
live overlays into a bounded set of even-aligned panel regions. Separate
regions stay separate when their bounding rectangle would redraw more pixels.
Every region is composed in the original layer order and the display owner
retires them as a single frame. A route change, rotation, or failed retirement
requires a full redraw.

## Profile and services

Select the physical board independently of the Cody profile. The Waveshare
1.75 GPS prototype uses 16 MB flash and supports removable microSD storage.
The aluminum-cased 1.75C uses 32 MB flash and has no removable-card slot;
its external RTC and removable-storage services are disabled. Cody on 1.75C
shows no SD status or free-space rows. Use native USB for live firmware logs.
Board wiring and available peripherals are listed in the
[hardware guide](../bsp/boards/purchased-amoled-comparison.md).
The [project introduction](../../README.md#the-little-round-thing) explains the choice of device.

Cody uses the shared power, battery, IMU, speaker, Wi-Fi and BLE provisioning
owners. The default image omits the character rig, microphone pipeline and
activity classifier. The optional `wake-word` feature enables the microphone,
audio front end and existing Sophia listener, including listening standby.

The saved-profile startup policy is authoritative; the Cody profile sets
`CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=n`. Development replay requires explicit
configuration. The compiled HTTP service defaults to disabled. The VibeWare
startup art, UI fonts and saved settings use internal flash. Removable-card
user data and mirrored logs apply only to SD-capable boards such as the GPS
prototype; they are not an internal-flash logging fallback on 1.75C.

Consumers read canonical projections with validity, original timestamps and real
errors. Only acquisition owners read hardware. Shared contracts are in
[BSP](../bsp/README.md); source ownership is in the
[component README](../../cody-device/components/CODY_companion/README.md).

## Independence and trademarks

I am not affiliated with, sponsored by, or employed by Espressif or Waveshare.
Cody Companion is an independent project and is not endorsed or certified by
either company. Third-party names, trademarks, logos and images belong to their
respective owners; their use identifies the relevant products and does not imply
a partnership. See the [license and attribution notices](../../THIRD_PARTY_NOTICES.md).

## Remaining work

Current release gates are in [TODO.md](../../TODO.md). Testing, generation,
simulation and physical operations use the public console and the authorization
rules in [AGENTS.md](../../AGENTS.md).
