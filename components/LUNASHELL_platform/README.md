# LUNASHELL_platform

Hardware acquisition and shared device services. Consumers read measured state
through `DeviceSnapshotService`; they do not open their own sensor paths.

| Source folder | Responsibility |
| --- | --- |
| [snapshot](src/snapshot/README.md) | Hardware acquisition, canonical reports, cadence, fusion and measured performance |
| [runtime](src/runtime/README.md) | Settings, clock/routine policy, sleep coordination and liveness |
| [display](src/display/README.md) | Standalone display/touch ownership and saved display settings |
| [power](src/power/README.md) | Power lifecycle, real inactivity and transient scene timeout policy |
| [audio](src/audio/README.md) | Shared codec/transport owner, playback/capture and voice AFE |
| [network](src/network/README.md) | Wi-Fi, Bluetooth, provisioning and pairing lifecycle |
| [http](src/http/README.md) | HTTP endpoints and canonical snapshot JSON |
| [storage](src/storage/README.md) | Removable-storage acquisition, lifecycle and explicit format requests |
| [distance](src/distance/README.md) | Optional distance acquisition and snapshot publication |

Public headers stay in `include/`. Root CMake, Kconfig and the component manifest
retain the existing feature gates and component identity. Disabled-feature
implementations live beside their owners; BSP, managed dependencies and generated
assets are outside this source tree.
