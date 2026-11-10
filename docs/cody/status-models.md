# Cody status models

Cody separates hardware observations, pet state and assistant observations by
ownership. The first integration is intentionally partial; these names do not
imply that all existing state has already moved.

| Model | Current source | Boundary |
| --- | --- | --- |
| `HardwareStatus` | `DeviceSnapshotService::readHardwareStatus` | A bounded, coherent battery, clock, Wi-Fi and BLE projection. Original validity, age and errors stay with the measurements; it performs no hardware acquisition. |
| `PetStatus` | Display-owned Muse showcase | The latest prepared artistic pose and its playback state for this session. A hidden view invalidates that pose; preparation does not prove a frame reached the display. This is not yet a persistent pet, a needs model or evidence of assistant work. |
| `AssistantStatus` | The existing domain query result | One bounded, source-qualified query including health, freshness, coverage and a page where requested. The existing authenticated CCP owner remains the sole store owner; the current profile admits one source. |

The Wi-Fi setup debug path consumes the hardware-only view and the Codex
browser consumes the assistant query under its new model name. The legacy
`DeviceSnapshot` and `DeviceStatusObservation` still contain mixed hardware and
Codex/application data, and most consumers have not migrated. Pet playback is
still owned by the display worker. No independent pet lifecycle, persistence,
multi-assistant registry or Claude, Groq or Copilot adapter is implemented here.

Further separation must retain existing acquisition owners, source identities,
per-field freshness and errors. Do not copy a second mutable registry into the
status models or infer real work from an artistic pose. Queries for different
owners are independent; they do not constitute an atomic combined snapshot.
