# Cody

Cody is the English ASCII character and interface rendered by `CODY2D`.
Its `cody-companion` profile uses the shared BSP and platform services. Display,
touch, sensor acquisition, persistence, power and network retain their owners.

## Find the relevant contract

| Task | Guide |
| --- | --- |
| First boot, Setup, settings, pairing, questions or recovery | [Navigation](navigation.md) |
| Screen placement, action cards, QR and native chrome | [Layout](layout.md) |
| Startup, scene clocks, pause, standby and audio admission | [Runtime](runtime.md) |
| Gaze, caress, happiness and idle gestures | [Behavior](behavior.md) |
| Single manga cue, priority, icon resources and petting | [Personality channel](personality-icons.md) |
| Rig, jelly skins, frame damage, DMA and resource budgets | [Rendering](rendering.md) |
| Greetings, routines and quiet presentation | [Clock presentation](clock.md) |

Installation and PC operation belong to the [CCP guide](../../plugins/cody-codex-plugin/README.md).
Packet formats and authentication belong to [CCP1](../../plugins/cody-codex-plugin/protocol/CCP1.md).
Windows controls and synthetic scenarios belong to the [simulator guide](../../simulator/windows/README.md).
The [documentation index](../README.md) lists the current illustrated atlases.

## Profile and services

Select the physical board independently of the Cody profile. The Waveshare
1.75 GPS prototype uses 16 MB flash. The aluminum-cased 1.75C uses 32 MB flash,
with the external RTC and removable SD services disabled. Board wiring and
available peripherals are listed in the [hardware guide](../bsp/boards/purchased-amoled-comparison.md).
The [project introduction](../../README.md#the-little-round-thing) explains the choice of device.

Cody uses the shared power, battery, motion, audio, Wi-Fi and BLE provisioning
owners. The profile excludes GNSS and distance. Wake word and TTS are optional.

The saved-profile startup policy is authoritative; the Cody profile sets
`CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=n`. Development replay requires explicit
configuration. The compiled HTTP service defaults to disabled. Artwork/fonts
belong in internal flash; SD has its separate user-data and logging purpose.

Consumers read canonical projections with validity, original timestamps and real
errors. Only acquisition owners read hardware. Shared contracts are in
[BSP](../bsp/README.md); source ownership is in the
[component README](../../components/CODY_companion/README.md).

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
