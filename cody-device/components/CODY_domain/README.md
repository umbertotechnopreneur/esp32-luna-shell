# Portable Cody domain

The C++17 headers define typed identities, field evidence, relationships,
capacity budgets and preliminary revision decisions. The [domain contract](../../../docs/cody/domain-model.md)
covers ownership and admission. The [state protocol](../../../cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md)
defines the wire format and security boundary.

| Layer | Current owner and boundary |
| --- | --- |
| Domain values and limits | `CODY_domain_types.hpp` and `CODY_domain_capacity.hpp` use standard C++ and allocate no store. The future single mutation owner admits staged generations and publishes bounded copies through DeviceSnapshotService. |
| Wire and ingress | `CODY_state_receive.hpp` checks binary geometry, ACK fields, replay and chunk ordering. The authenticated link owner supplies endpoint/role/session checks, cryptography and receiver monotonic time. The staging sink consumes bytes and reports its original error. |
| Provider and desktop | The CCP broker translates native evidence. OS owners select user paths, protect credentials, discover processes and own IPC. Native IDs stay provider-qualified. |
| Muse | The shared indexed frame composer accepts semantic pose, active time and optional artistic audio energy. Firmware and Windows preview use the same Muse C core. The artistic seed only affects animation. |
| Device | The Waveshare adapter describes configured capabilities and logical geometry. The display owner submits aligned RGB565 strips and holds frame storage until retirement. DeviceSnapshotService supplies health, input, and sensor evidence from the existing owners. |

The desktop preview and firmware source lists include the same receiver
implementation. A receiver holds 16 fixed 1,024-byte payload slots plus
metadata. Allocate it in owner storage, off callback and device task stacks.
The negotiated width may be lower. The decoder and digest sink borrow each
ordered slice only for the synchronous call. A full receive window confirms
that the sink consumed the bytes. Hash and semantic admission must pass before
the owner sends a commit ACK. An active transfer rejects a different transfer ID
as busy. Conflicting metadata on the same transfer, decoder errors and deadline
expiry release the window and require the owner to discard staging.

## Conformance cases

These are inputs and expected outcomes for the phase-06 scenario driver.

| Case | Input | Expected outcome |
| --- | --- | --- |
| Reorder within window | Width 4, chunks 2, 0, 1, then final chunk 3 | First ACK is missing 0 with bitmap 0x0004. After 0 and 1 the sink has chunks 0 through 2 in order. The last chunk completes receipt but does not commit. |
| Conflicting retry | Buffer chunk 2, then send different bytes for chunk 2 with the same transfer identity | Integrity error. Drop staging and preserve the last committed view. |
| Concurrent transfer | A is receiving, then a packet for transfer B arrives | Busy for B. A retains its buffered chunks and original deadline. |
| Duplicate replay | Admit sequence 70, then 69, then 69 again, then 6 | Accepted, accepted, duplicate, too old. Neither rejected packet renews liveness. |
| Transfer clock | Fill a gap just under the idle timeout, then retry without progress or move the clock backwards | The first contiguous progress updates only the idle timer. The absolute deadline is unchanged. Duplicate traffic cannot extend either timer and clock regression cancels staging. |
| Bounded viewport | Synthetic rectangular 240 x 320 panel, safe rectangle 192 x 224, 64-cell grid and preferred scale 3 | Admit a centered 192 x 192 viewport and use its same half-open bounds for touch. The Waveshare fixture retains 192 x 192 at (137,137). |
| Smaller device | Synthetic target without external RAM or a microphone | Voice remains unavailable. Capacity admission still requires measured memory and must report failure if the generation banks do not fit. |

## Adding a target

- **Desktop OS:** add the OS adapter for state paths, credential protection,
  executable discovery, process and IPC behavior. Use the same provider admission
  and source IDs. Keep the shared host driver in the existing console flow and
  verify the new adapter on that OS before enabling it in the product.
- **Device:** supply a capability descriptor and admitted content geometry from
  the board configuration. Register sources through the board/profile catalog.
  Reuse the acquisition owner and apply the same logical coordinates to drawing
  and touch. Report missing capabilities explicitly. Confirm the target's actual
  memory, DMA alignment/retirement, cache access and persistence guarantees
  before admitting a capacity or reporting durable state.
- **Store integration:** keep exactly one mutation owner, stage before commit,
  and keep storage/clock/error inputs explicit. Confirm the available memory
  before allocating generation banks, and keep the previous coherent generation
  on failure.

Both existing Waveshare configurations keep their authored circular geometry.
Other layouts, OS ports, live negotiation, digest/JSON integration, atomic
generation publication and persistence still need their respective owners and
acceptance. No execution or device validation is recorded here.
