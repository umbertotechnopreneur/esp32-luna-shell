# Cody multi-chat domain contract

`CODY-NEXT-01` defines a presentation-independent model. Its C++17 value types
are in [CODY_domain_types.hpp](../../cody-device/components/CODY_domain/include/CODY_domain_types.hpp);
its capacity calculation is in [CODY_domain_capacity.hpp](../../cody-device/components/CODY_domain/include/CODY_domain_capacity.hpp).
The initial source, chat, turn and tool records are integrated in the
authenticated broker and device store.
The [phase-02 protocol](../../cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md)
defines serialization and synchronization; 14 owns platform/build boundaries; 03-05 own live
adapters, durable answers and the device store. No compiler, simulator or board
result is implied by source review.

vNext requires matching broker/device versions. Old ACTIVITY/TECH projections,
old internal APIs and old domain persistence formats need no compatibility
adapter or automatic migration. Unsupported formats fail explicitly; any
destructive reset still requires its own explicit action. Retaining screen
source in the [catalog](screen-catalog.md) does not retain its old data contract.

## Identity and relationships

`SourceId` is a nonzero 64-bit namespace token registered in the broker's durable
source directory. Registration binds it to an exact provider/account/installation
namespace, not a display name. A conflicting registration fails. Pairing scopes
which directory is trusted; independent brokers cannot inject their local source
numbers into another directory without explicit registration.

Each typed entity ID is `(SourceId, uint64_t token)`. The broker persists the
mapping from `(source, entity kind, full native identity)` before publication.
Allocation never truncates/hashes a native ID into a token and never reuses a
token for a different object, including after deletion. Zero is unassigned.
Duplicate native mappings or token collisions fail admission. Native tool-use
IDs, chat IDs and turn IDs remain intact on the broker for correlation.
The device receives their stable tokens, not array indices. Loss of the mapping
is an explicit recovery fault; it must not silently regenerate different IDs
under the same source namespace. This directory is new vNext data, not a migration
of the old broker cache.

`EpochId` identifies an authenticated producer lifetime. It is not ordered and
is not part of an entity's identity. Reordering, reconnecting or restarting the
broker does not rename a chat, call or unresolved question. Evidence IDs are
separate store-local tokens: they are not provider IDs or wire addresses.
`ObservationId` names the source-qualified original broker observation on the
wire; `Evidence.observation` preserves it across transfers, independently of
the local handle. Retransmission cannot turn it into a new observation.

| Object | Mandatory association | Independently observed facts / lifetime |
| --- | --- | --- |
| Source | Registered provider/native namespace | Current epoch, admitted revision, semantic capabilities and link observation; retained while any entity refers to it |
| Chat | Source in its typed ID | Title, lifecycle, current turn; all admitted chats retained regardless of selection |
| Turn | Exact ChatId | Execution, outcome and reply availability; retained while any call, agent, operation, wait, question or answer chain depends on it |
| ToolDefinition | Source in its typed ID | Exact original name, label, semantic category; immutable identity retained while calls use it |
| ToolCall | Exact TurnId and ToolDefinitionId | Executing agent when known, invocation state and outcome; one record per invocation, including overlapping uses of the same tool |
| AgentTask | Original owning TurnId | Optional parent agent, state and outcome; parent graph must be acyclic |
| ExternalOperation | Original owning TurnId | Originating call/native process or job identity when observed, independent state/outcome |
| Question | Source-qualified request ID | Explicit known/unknown chat and turn associations, kind, semantic state, prompt, ticket availability; unresolved records remain pinned |
| Choice | Exact QuestionId | Immutable choice identity, display order, label and full detail; reordering does not select a different answer |
| AnswerAttempt | Exact QuestionId and ChoiceId | Immutable attempt/delivery correlation plus delivery state; retain until semantically reconciled and durably recorded |
| WaitDependency | Exact TurnId | Tagged user, tool, agent, service or unknown target; independently observed blocking status |

Mandatory references must resolve in the same committed generation and source.
An executing agent, its call and their owning turn must agree; a parent agent
must belong to the same chat, while retaining its actual original turn.
Known `Chat.current_turn` must point back to that chat. Known question chat/turn
references must agree. Unknown association is represented explicitly and cannot
borrow the selected chat or become an answerable device action. A native approval
notice is not an answerable question and never admits a Choice or AnswerAttempt.

## Facts, errors and clocks

`Fact<T>` contains a typed value, `Availability` and an `EvidenceId`:

- `Unknown`: no authoritative value is known; the default payload is not a fact.
- `Known`: the value is meaningful, including zero, false or an empty string.
- `Absent`: the source explicitly observed no value/reference.
- `Unsupported`: the source explicitly lacks this capability.
- `Failed`: the observation failed; evidence retains the original error.
- `Conflict`: trustworthy observations disagree and reconciliation is required.

Known does not mean fresh. Each evidence record separately carries freshness,
provenance, source epoch/revision, original observation time and clock identity,
reported source age, explicit validity lease, receiver clock/time and error
kind/original code/detail. Unknown lease is not zero; wire leases and original
observation identities are retained for subsequent local aging.
The local store assigns `admitted_sequence` for retention ordering across
sources/epochs; source revision numbers cannot provide that global ordering.
Unknown age is not zero. Monotonic values are comparable only within their exact
clock identity; a restarted receiver or producer gets a new clock identity.
Do not subtract a remote monotonic value from local uptime. Unsupported, invalid
or missing source time stays explicit. Phase 02 defines cross-peer age transfer.

Facts can share evidence only when they came from the same actual observation.
An updated title must keep the turn's original evidence unchanged. Heartbeats,
packet retries, navigation, animation and persistence reload do not refresh
facts. Restore marks evidence Historical/Restored until authoritative live
reconciliation. Synthetic host evidence stays tagged Synthetic and cannot be
published into production observations.

The owner must retain each referenced evidence row and text span until no live,
staged or borrowed generation uses it. Text spans are internal offsets/lengths,
not pointer values, identities or wire representations. The state protocol caps each
field; all displayed text is printable ASCII. Reject invalid/oversized text
with a specific cause instead of silently truncating identities or answer meaning.

## Mutation and publication

The PC adapter translates genuine provider observations into source-qualified
facts. One broker registry owner serializes admission; no UI selection filters
ingestion. The device's single mutation owner admits authenticated transactions
from the existing acquisition boundary and publishes through DeviceSnapshotService.
The portable types have no hardware, OS, network, mutex, rendering or file I/O.

The store admits each transaction atomically:

1. Check source/epoch authorization, expected revision and negotiated capacities.
2. Stage entities, facts, evidence and text without modifying the committed view.
3. Reject duplicate IDs, dangling mandatory references, cross-source confusion,
   cycles, contradictory terminal facts, invalid values and forbidden eviction.
4. Publish one generation only after all checks succeed. Preserve the original
   error and previous coherent generation on any failure.

`DeviceSnapshotService::readCodexDomainObservation` exposes bounded queries
for the selected chat, chat pages, turns, calls, coverage, current attention
and health. The initial profile reports pending questions as unsupported.
Do not put the complete registry into `DeviceSnapshot`, `DeviceModel` or a
display-worker stack frame.
Network buffers and callbacks never escape their owner. Queries copy a bounded
page under owner synchronization, then release the lock before raster, audio,
storage or network work. Text resolves into that page's bounded buffer, so a
retired generation cannot leave a dangling `TextRef` in a display frame.

Summaries (running-call counts, waiting chats, attention priority) are queries
over this registry. Cached presentation is generation-qualified and never a
second mutable source of chat truth. A page cursor names its source, epoch,
revision, local generation and exact query scope. A generation change requires
the UI to restart pagination. When a page contains historical evidence, the
interface can show the last known value without counting it as current work.

## Lifecycle rules

These are reducer requirements for phase 05; the headers do not yet enforce them.
Unknown observations can enter any state supported by trustworthy evidence.
An unchanged repeated fact is idempotent. Out-of-order transport delivery never
overrides a newer admitted revision; a contradictory new observation requires
explicit reconciliation, not a guessed transition.

| Object | Admitted transitions / evidence | Forbidden inference |
| --- | --- | --- |
| Chat | Open -> Closed/Archived; reopen only with explicit source evidence | Interrupt, UI Back or missing data does not close it |
| Turn execution | Queued -> Running; Running <-> Waiting; observed stop -> Stopped; explicit interrupt -> Interrupted | Tool completion or reply availability does not complete the turn |
| Continued turn | Stopped -> Running only with provider evidence for the same native turn and no contradictory terminal outcome | Do not rename a late event to the chat's newest turn |
| Turn outcome | Unknown until an explicit final outcome is observed; terminal result cannot silently flip | Stop is not Succeeded; Interrupted is not a completed successful task |
| ToolCall | Queued -> Running -> Returned/Failed/Cancelled; terminal evidence can arrive without a start if association is known | Returned is not task success or child-process exit |
| AgentTask | Queued/Running/Waiting -> observed Stopped/Failed/Cancelled; waits may resume | Child stop does not close parent turn; contradictory reuse needs a new invocation identity |
| ExternalOperation | Pending -> Running -> Finished/Failed/Cancelled | A process-start tool returning does not mean the process finished |
| Question | Open -> Answered/Cancelled/Superseded only from semantic source resolution | Ticket expiry, offline, stale, navigation or local dismissal never closes it |
| AnswerAttempt | Admitted -> Persisted -> Sending -> BrokerReceived -> RecipientDelivered; explicit Rejected/Cancelled or Unconfirmed on uncertain delivery | UDP receipt is not recipient delivery; selecting a choice is not Send |

A terminal call/agent/operation cannot return to Running under the same invocation
ID. A corrected outcome needs a specific authoritative reconciliation event and
its evidence; ordinary updates cannot silently overwrite it. Phase 03 must map
documented native semantics rather than assuming providers share these signals.

`Unconfirmed` is unresolved delivery. A later authoritative reconciliation of
the exact persisted attempt may establish recipient delivery; a delayed packet
from an obsolete ticket cannot resurrect a closed presentation or answer a new
question. Once delivered/rejected/cancelled, competing terminal observations
become conflicts for reconciliation. Phase 04 serializes answer-versus-cancel
and competing answers across devices and persists the immutable selection.

Question content/choice meaning cannot change underneath an admitted answer.
A material replacement creates a new request/choice identity and explicitly
supersedes the old request. Keep the original question, choice and turn pinned
until all attempts against them have a resolved durable result.

Multiple wait edges may coexist. A background question can be Open while tools
are Running. Only known blocking evidence changes the parent to Waiting;
unknown targets remain UnknownWait/UserWait/ServiceWait with unknown association.
Do not infer a wait merely from the existence of a child object.

## Bounded storage and admission

`CapacityLimits` is an initial negotiation proposal, not a reservation of board
memory. It contains 4 sources, 64 chats, 192 turns, 128 tool definitions,
256 active calls plus 128 terminal calls, 64 agents, 64 external operations,
64 open plus 32 resolved questions, 288 choices, 192 answer attempts, 256 wait
edges, 4,096 evidence records, 512 tombstones and 128 KiB text. Decoder scratch
is 64 KiB; persistence scratch is 32 KiB. Source count is capacity, not activation
of additional providers. State protocol 1 admits one through three explicit
choices per answerable question; native approval notices have none.

All limits apply together. A transaction may reach the evidence/text/turn limit
before its call limit; reject it explicitly instead of promising that every
record can simultaneously contain every maximum-length field. The broker keeps
longer history. Optional capabilities and device memory are explicit inputs,
not tests for board or OS names. A device without a microphone still represents
chats, tools and questions.

The budget helper uses `sizeof` of the actual C++ records and sorted index
entries, with 64-bit arithmetic on 32-bit targets. It includes per-kind coverage,
both evidence indexes (local handle and original observation), pooled text and
tombstones, then reserves another complete
generation for atomic replacement. Both scratch areas are additional. No array
or heap allocation occurs in these headers. Adapters must also reserve allocator
alignment/bookkeeping, retained-reader lifetime, existing firmware, Muse, audio,
network/DMA buffers, stacks and internal-memory requirements. At most the live
and staged generation may coexist: if the old one remains borrowed, defer a new
transaction or return backpressure rather than allocating a third bank.

For the requested **4,096 active-call** evaluation,
`extended_call_candidate()` sets active calls to 4,096, evidence to 16,384,
waits to 512 and text to 256 KiB. Call storage alone is
`(4096 + 128) * sizeof(ToolCall)` per generation; evidence adds
`16384 * sizeof(Evidence)`, plus indexes, other tables and two generations.
This is a multi-megabyte candidate, not an automatically enabled default.
The source baseline does not establish currently available PSRAM or largest
usable blocks. Phase 11 must obtain compiler layout/stack evidence and measured
device headroom before advertising this capacity. If it does not fit, negotiate
a smaller call allowance with explicit partial coverage/backpressure. Never
discard admitted active calls to make a high capacity claim.

Eviction/admission rules:

- Keep all admitted chats until explicit source removal/closure retention policy
  permits deletion. Never evict a chat because another is selected.
- Pin active calls, unresolved questions/answers, live children, their original
  turns, required definitions, choices, evidence and text. An answered question
  stays pinned while an unresolved attempt references it.
- Evict only unreferenced terminal history, oldest terminal admission sequence first with
  typed ID as the deterministic tie break. Reclaim dependencies only when no
  remaining entity or reader refers to them. Keep tombstones through the accepted
  replay window; if their budget fills, require full resynchronization.
- Capacity failure rejects the atomic transaction with `CapacityExceeded` and
  the exhausted pool. Preserve the last coherent view; expose partial coverage
  and exact omitted counts when known. Unknown omitted counts are not zero.
- Do not advance the admitted revision over a rejected transaction. The broker
  must pause/retry or negotiate a fresh coherent projection; newer deltas cannot
  skip the missing base. No truncated snapshot is labeled Complete.
- Persistence scratch is a streaming workspace, not the total durable ledger
  capacity. Phase 04/05 must bound ledger size, atomic records and recovery.

## Selection and deterministic examples

`SelectionState` lives outside the source registry. FollowPrompt may select a
chat on a genuine new prompt. Pinned preserves manual inspection until explicitly
released. `return_chat` keeps semantic context for returning from detail; UI route
IDs remain with the presentation owner. Optional typed IDs represent a selected
chat or no selection; they do not borrow evidence from a replaceable source
generation. Selection is neither native app focus
nor a change to the existing frozen voice destination.

These examples define expected results for the later scenario driver. Symbols
stand for stable typed IDs, not array positions; each line uses a new admitted
revision unless explicitly described as a duplicate/late event.

| Scenario | Input sequence | Expected domain / selection result |
| --- | --- | --- |
| Two chats | A/turn a1 starts; pin A; B/turn b1 starts | Both chats/turns retained; selection stays A; B contributes to derived global counts |
| Overlapping tools | a1 calls shell as c1 and c2; c2 returns first | Two ToolCalls reference one ToolDefinition; c1 remains Running; a1 outcome remains Unknown |
| Background question | B asks q1 while A is pinned and B/c3 is Running | q1 remains Open and reachable; c3 remains Running; no invented blocking wait or selection change |
| Old turn child | A starts a2 while a1/c1 is running; c1 later returns | c1 still references a1; retain a1; current_turn stays a2; no outcome inferred for either turn |
| Reorder / duplicate | Admit revision 12; receive identical 12, then old 11 | Duplicate has no effects or new audio; old revision cannot overwrite; source ages unchanged |
| Gap | Committed 12; delta requires base 13 | Retain generation 12, report resync required; do not apply a partial transaction |
| Broker restart | Authenticate new epoch, reconcile same A/a1/q1 identities | Keep identities and unresolved q1; old-epoch packets cannot roll back state; no replayed celebration |
| Ticket expires | q1 Open; ticket expires while answer attempt is Unconfirmed | q1 remains Open, attempt remains unresolved; reconciliation is required before another action |
| Field freshness | A title refreshed; turn state has no new observation | Title evidence changes; old turn evidence stays stale/unknown; heartbeat does not refresh it |
| Capacity | c257 arrives with a full 256-active-call pool and no evictable slot | Atomic rejection, old registry intact, CapacityExceeded/partial coverage visible; no silent loss |
| Approval | Native approval arrives with unknown chat relation | Advisory-only record; no choice, answer, guessed chat or provider decision |
| Process | Spawn call returns a running process identity; process exits later | Call Returned and operation Running coexist; only later explicit exit ends the operation |

The full scenario matrix remains `CODY-SC-01` through `CODY-SC-21` in the plan.
Compilation, executable reducer checks, protocol scenarios, persistence failures
and physical measurements remain explicit phase-11 gates after implementation.

## Hardware pet and assistant status models

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
