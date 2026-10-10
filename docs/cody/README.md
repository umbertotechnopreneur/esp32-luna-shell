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

The current physical adapter is the [Waveshare board](../bsp/boards/waveshare-esp32s3-touch-amoled-1.75/hardware.md).
Cody admits power/battery, IMU, RTC, speaker, microphones, Wi-Fi, BLE provisioning
and removable SD according to its profile. GNSS and distance are excluded.
Firmware exclusion does not establish an electrically unpowered peripheral.
Wake word and TTS are explicit optional features.

The saved-profile startup policy is authoritative; the Cody profile sets
`CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=n`. Development replay requires explicit
configuration. The compiled HTTP service defaults to disabled. Artwork/fonts
belong in internal flash; SD has its separate user-data and logging purpose.

Consumers read canonical projections with validity, original timestamps and real
errors. Only acquisition owners read hardware. Shared contracts are in
[BSP](../bsp/README.md); source ownership is in the
[component README](../../components/CODY_companion/README.md).

## Evidence

Current release gates are in [TODO.md](../../TODO.md). Dated source, host, build,
flash and device observations are in [done.md](../../done.md). Old benchmark or
boot observations do not certify a later source change. Testing, generation,
simulation and physical operations use the public console and the authorization
rules in [AGENTS.md](../../AGENTS.md).
