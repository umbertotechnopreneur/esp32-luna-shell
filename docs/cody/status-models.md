# Cody status models

Cody separates hardware observations, pet state and assistant observations by
ownership. The first integration is intentionally partial; these names do not
imply that all existing state has already moved.

| Model | Current source | Boundary |
| --- | --- | --- |
| `HardwareStatus` | `DeviceSnapshotService::readHardwareStatus` | A bounded, coherent battery, clock, Wi-Fi and BLE projection. Original validity, age and errors stay with the measurements; it performs no hardware acquisition. |
| `PetStatus` | `PetModel` behind `record_pet_tap` and `read_pet_status` | Session-only count and original monotonic time of recognized mascot taps. The model has no renderer, driver or assistant dependency. `PetObservation` includes the latest delivery error and original attempt time. |
| `AssistantStatus` | `DeviceSnapshotService::readAssistantStatuses` over registered source owners | One independently identified source with provider, native namespace, epoch, revision, local generation, negotiated capabilities and evidence-qualified link. The built-in CCP adapter reads its committed domain store. |

The native dashboard, battery notices, status overlay and Wi-Fi cues consume the
hardware-only view. Legacy Codex presence is read separately through
`readCodexPresenceStatus`, with its own read error; it is not a generic assistant
presence signal. Neither read claims an atomic snapshot across those owners.
A recognized tap on
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

`AssistantCatalog` contains observations for up to four registered owners,
including unavailable owners and their original errors. Registration rejects
duplicate owner IDs, duplicate callbacks and capacity overflow. Owner IDs are
local adapter identities, not source IDs; value one is reserved for CCP.
Two successful readers claiming the same source ID are both marked conflicting,
even if their providers or epochs differ. No source is selected by brand or index.
`copy_source` selects an unambiguous source and preserves the destination on failure.

The registry contains callbacks only, not copied mutable source state. A read
captures the registry revision under its lock, releases it, then reads each owner
under that owner's synchronization. Callbacks and their state must have process
lifetime, including after unregister, because a captured read may still finish.
Callbacks copy observations without network I/O. `enumerated` means the complete
registered set at that revision was visited; it does not mean every read succeeded,
that all possible providers were discovered, or that the sources are atomic together.

The dashboard retains the catalog in its existing display-owned PSRAM storage,
separately from hardware and pet observations, and logs per-owner error changes.
The catalog has a 2304-byte compile-time ceiling and is filled in place. The service
keeps only a small callback-list copy on its stack, plus bounded row temporaries;
compiled stack frames and physical headroom still require validation.

The legacy `DeviceSnapshot` and compatibility `DeviceStatusObservation` still
contain mixed hardware and Codex/application data for remaining consumers.
The native dashboard no longer reads that combined projection. The registry and
status selection support independent assistants, but only the CCP owner is
registered by default. Claude, Groq and Copilot adapters, automatic discovery,
multi-assistant command/question routing and a persistent pet lifecycle are not
implemented here. Existing Codex account panels and actions remain Codex-specific;
an assistant's domain link does not substitute for legacy Codex app presence.

Further separation must retain existing acquisition owners, source identities,
per-field freshness and errors. Do not copy a second mutable registry into the
status models or infer real work from an artistic pose. Queries for different
owners are independent; they do not constitute an atomic combined snapshot.
