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

Installation and PC operation belong to the [CCP guide](../../cody-desktop/plugins/cody-codex-plugin/README.md).
Packet formats and authentication belong to [CCP1](../../cody-desktop/plugins/cody-codex-plugin/protocol/CCP1.md).
Windows controls and synthetic scenarios belong to the [simulator guide](../../cody-device/simulator/windows/README.md).
Use the [A4 navigation map](device-navigation-map-a4.pdf) for the dashboard and
Options. The startup screen shows the Muse avatar after the VibeWare animation.

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
owners. The default image includes the Muse avatar. The microphone pipeline and
activity classifier are optional. The optional `wake-word` feature enables the microphone,
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
