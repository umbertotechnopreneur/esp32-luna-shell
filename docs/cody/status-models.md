# Cody status models

Cody separates hardware observations, pet state and assistant observations by
ownership. The first integration is intentionally partial; these names do not
imply that all existing state has already moved.

| Model | Current source | Boundary |
| --- | --- | --- |
| `HardwareStatus` | `DeviceSnapshotService::readHardwareStatus` | A bounded, coherent battery, clock, Wi-Fi and BLE projection. Original validity, age and errors stay with the measurements; it performs no hardware acquisition. |
| `PetStatus` | `PetModel` behind `record_pet_tap` and `read_pet_status` | Session-only count and original monotonic time of recognized mascot taps. The model has no renderer, driver or assistant dependency. `PetObservation` includes the latest delivery error and original attempt time. |
| `AssistantStatus` | `readCodexAssistantStatus` over the committed domain store | One independently identified source with provider, native namespace, epoch, revision, local generation, negotiated capabilities and evidence-qualified link. Source metadata is copied from the committed generation, not from an uncommitted offer. |

The Wi-Fi setup debug path consumes the hardware-only view. A recognized tap on
the mascot publishes one pet interaction before starting its visual reaction;
the view keeps its own showcase and animation clock. Readers and writers serialize
the small scalar update/copy under one mutex, with no I/O, callbacks or logging
inside the critical section. Reader contention waits instead of dropping a tap.
A rejected event leaves accepted tap facts unchanged, is logged and remains in
the service observation and dashboard projection. Pet delivery is optional for
display operation: rejection suppresses that reaction and cue, not the display
worker. Hiding, re-creating or changing the view does not clear the pet
model. The model is memory-only and starts empty on firmware restart. It does not
infer mood, needs or a physical interaction from a showcase pose.

`AssistantStatusPage` assembles up to three independently obtained source views
and rejects duplicate source IDs or overflow. It is caller-owned scratch, not a
second registry, and does not claim that a page is a complete source list or a
cross-source atomic snapshot. Each source retains its own generation, health and
link evidence. The current CCP owner supplies one source; additional owners,
discovery, pagination and adapters are not implemented by this type.

The legacy `DeviceSnapshot` and `DeviceStatusObservation` still contain mixed hardware and
Codex/application data, and most consumers have not migrated. No persistent pet
lifecycle, multi-assistant registry or Claude, Groq or Copilot adapter is
implemented here.

Further separation must retain existing acquisition owners, source identities,
per-field freshness and errors. Do not copy a second mutable registry into the
status models or infer real work from an artistic pose. Queries for different
owners are independent; they do not constitute an atomic combined snapshot.
