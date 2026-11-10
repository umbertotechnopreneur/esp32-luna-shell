# Cody state protocol 1

This is the implementation contract for `CODY-NEXT-02`. It replaces the selected
chat ACTIVITY/TECH model with source-qualified domain transactions. It is not
yet enabled in the broker or firmware. Update both peers together; there is no
old-activity parser, downgrade or legacy-state migration in this contract.
Commissioning and the retained-key security guarantees are reused from
[CCP1](CCP1.md), not its activity payloads. Audio, account/settings and power
commands remain separate owners; a state transaction cannot invoke them.

The [JSON schema](cody.state.v1.schema.json) specifies shape and enum validation;
the rules below additionally specify identity, semantics and resource admission.
[Examples](cody.state.v1.examples.json) are synthetic authoring fixtures, not
captures or executed tests. Portable constants and preliminary admission helpers
are in [CODY_state_protocol.hpp](../../../../cody-device/components/CODY_domain/include/CODY_state_protocol.hpp).

## JSON values and entity mapping

All messages use `schema: "cody.state/1"`. Reject duplicate object keys, unknown
properties, unknown enums, trailing input, NaN/infinity, invalid encodings and
nesting deeper than eight containers. JSON bytes are UTF-8 with printable ASCII
string values only; no BOM or NUL. Whitespace outside strings is normal JSON
whitespace. No canonical JSON algorithm is required: a transfer hashes its exact
frozen bytes and retransmits those bytes. Do not reserialize received JSON for
authentication or integrity checks.

IDs and unsigned 64-bit revisions/timestamps are exactly 16 lowercase hexadecimal
digits, avoiding floating-point rounding in JSON clients. Entity/source/epoch/
observation/request IDs must be nonzero. Revision zero means no committed
baseline and is permitted only where explicitly stated. Counts and durations
are JSON integers bounded to uint32, never booleans/fractions. Original signed
error codes are canonical decimal strings in the int64 range: no plus, leading
zeroes or negative zero. Every decoded integer is range-checked before casting.

A transaction contains one `source_id` and `epoch`; entity IDs/references inside
it are source-local tokens. Expand them to typed `(source_id, token)` values
before insertion. `source` record ID must equal the envelope's source ID.
All other IDs retain their entity-kind namespace. Source registration preserves
the exact native namespace; tokens are never inferred from names, ordering or
native app focus. Epoch adoption is negotiated below, not triggered by a packet.

`upserts` contain whole records, not ambiguous field patches. The `type` property
selects the matching domain type. Reference-bearing fields are typed by that
record: `turn.chat`, `tool_call.turn/definition`, `agent_task.owning_turn`,
`external_operation.owning_turn`, `choice.question`, `answer_attempt.question/choice`
and `wait.turn`. Facts carry `{state, value, evidence}`. Known facts have a typed
value and observation ID; non-known values are null. Only `unknown` may have
null evidence; absent/unsupported/failed/conflict require actual evidence.
The selected chat and return route are deliberately absent from the wire.

Text maxima are bytes after JSON unescaping: provider 64, native namespace 128,
chat title 255, original tool name 128, display label 96, native operation identity
128, question prompt 512, choice label 96, choice detail 1,024 and error detail
512. Zero-length titles/optional labels can be known empty; provider/namespace,
tool names, question prompt and choice label/detail cannot be empty. Aggregate
text capacity is separately negotiated. No truncation or arbitrary argument
inspection supplies a missing label/category. Native approval notices admit no
choices/answers. Answerable questions support one through three explicit choices
in this version; semantic answer admission and durability belong to phase 04.
Enum spellings in JSON are the schema's lowercase snake_case names. Its uint32
pool maxima are hard protocol bounds, not default allocations or promises of
simultaneous admission; actual limits are negotiated downward to fit memory.

Every fact in an upsert must resolve against that transaction's `evidence` list.
Each evidence ID is a stable broker observation token within the source, retained
across transfers and broker epochs. Intern it as `Evidence.observation`; allocate
a separate receiver-local `EvidenceId`. The same observation ID cannot acquire
different origin epoch/revision, original timestamp, provenance, lease or error.
Age/freshness may become older; a refreshed observation gets a new ID. Repeated
rows must agree. Evidence not referenced by any upsert is rejected. Unchanged
records keep their existing evidence without requiring retransmission.

`observed_at` is null or `{basis, clock_id, microseconds}`. Basis is Unix or
monotonic microseconds; a clock ID changes when that clock discontinuously resets.
Never compare remote monotonic time with device uptime. Receiver time and local
admission sequence are computed locally and never accepted from JSON. Failed
facts require evidence with an original error. Restored evidence is historical;
synthetic evidence is admitted only by the explicitly synthetic host adapter,
never by production firmware. Semantic capabilities are independent of OS,
board, microphone, transport liveness and current record counts.

## Envelope kinds

| Kind | Required content / meaning |
| --- | --- |
| offer | `request_id` of negotiation; stable source bindings with epochs/provider/namespaces/capabilities, broker capacity/transport limits |
| select | Same request ID, exact offer SHA-256 and receiver-selected limits no greater than either peer's bounds |
| accepted | Same request ID, exact offer/selection digests and the selected limits; no silent renegotiation |
| resync | New request ID, source/epoch, receiver's last revision (zero if absent), concrete recovery reason |
| next | New request ID, source/epoch and exact current `base_revision`; asks for the next coherent delta |
| no_change | Echo request/source/epoch/base as `revision`; source has no new transaction, not fresh source evidence |
| snapshot | Echo request/source/epoch, nonzero revision, base zero, coverage, whole records/evidence, explicit removals and cue watermark; cues empty |
| delta | Echo request/source/epoch, nonzero revision greater than base, coverage, whole changed records/evidence, explicit removals, bounded semantic cues |
| error | Echo request, exact reject code/pool/detail and current revision when known; cannot mutate entities |

No remote `answer`, `reset`, renderer expression or generic command kind is
admitted by this protocol. Later answer intents must use the phase-04 contract
and its own authentication/durability rules.

Every snapshot/delta is a response to one locally pending request in the current
session. A receiver makes at most one outstanding state request and admits at
most one incoming transfer. `next` requests are no faster than every 500 ms;
the producer freezes its response after receiving the request, and responds
without waiting for a future event. It may coalesce journal revisions into one
delta, retaining the exact requested base and a newer target revision. If no
change exists, return `no_change`. Unsolicited updates cannot replace state.

## Freshness without shared clocks

At response freeze, the sender calculates each observation's original age in
milliseconds using its actual clock evidence. Unknown age/lease is null, not
zero. A lease, when meaningful, is 1..120,000 ms; a lease is a freshness limit,
not the lifetime of a question or answer. Current evidence requires known age
and lease. An expired source observation cannot be marked current.

The receiver records the original request-send time and clock ID. On commit,
use `source_age_ms + elapsed_since_original_request_ms` as a conservative age
bound: the producer froze its answer after that request. Round elapsed time up,
never down. Retries do not move the request's timestamp. Expire a request after
120 seconds; reject responses after receiver clock reset or time reversal.
For an already-known observation, retain the greater of its locally aged bound
and the new bound. This may age facts early, but never renews old evidence.
Missing age/lease remains unknown; a current link alone cannot make it current.

Transfers, PONGs, no-change replies and restored text do not refresh observations.
An unresolved question remains retained even when its evidence or reply ticket
expires. Reboot restores historical facts; authenticated reconciliation must
establish new valid bounds before a live claim. Artistic Muse time and cue dwell
never alter source age, semantic resolution or answer deadlines.

## Authentication, negotiation and epoch binding

Keep the existing explicit commissioning, protected retained link key, separate
pending/active association and persistence/readback requirements. Use the existing
UDP owners/ports (device 42117, broker 42118); enlarge their bounded state packet
path only during implementation. Bootstrap text packets are at most 512 ASCII
bytes, exact ordered fields, lowercase hex and canonical decimal port:

```text
CCP2|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|state=1|proof=<64hex>
CCP2|READY|device=<12hex>|pc=<32hex>|nonce=<32hex>|challenge=<32hex>|address=<IPv4>|port=42117|state=1|proof=<64hex>
CCP2|COMMITTED|device=<12hex>|pc=<32hex>|nonce=<32hex>|challenge=<32hex>|address=<IPv4>|port=42117|state=1|proof=<64hex>
```

HMAC-SHA256 uses the active retained key and ASCII values joined by `|` in the
shown order, excluding names/proof. Prefix the joined values with respectively
`find-state-v1|`, `ready-state-v1|`, `committed-state-v1|`. These domains include
the final `state=1` value. No user-provided address determines a return endpoint.
FIND is only a challenge request: it discloses no current domain data. READY
binds the current client nonce and broker-generated 128-bit challenge to the
socket-observed device IPv4/port. Cody verifies that against canonical network
observations (or the explicitly synthetic loopback adapter). The broker's source
port is fixed. Repeated FIND retains its original two-second challenge deadline.

COMMITTED must arrive from that exact endpoint before expiry with the matching
key, nonce and challenge; consume it before outgoing effects. A duplicate cannot
create a second session. The 128-bit challenge becomes `session_id`. The device
must have persisted/read back its association before COMMITTED. A lost exchange
starts a new FIND nonce; it does not reuse old negotiation state. Bound outstanding
challenges to 128, live broker sessions to 16 and device sessions to one.

Only then may the broker send `offer` through the binary transport below.
The offer's nonzero request ID is unique within the session; the same ID is used
for select/accepted. The offer's source manifest contains at most four bindings.
The receiver selects source IDs from that manifest and capacities within both
local memory budgets and offered limits, sends select, and admits accepted only
if the selected source IDs, both exact document digests
and every selected limit match. Negotiation controls use a one-chunk window and
the fixed 4 KiB control cap until accepted commits. A selected source subset must
fit `limits.sources`; source IDs/provider namespaces must be unique and stable.
All pool limits are aggregate per receiver across selected sources, not multiplied
by the number of sources. Zero optional capacity means unsupported; it is
not successful empty coverage. Mandatory source/chat/turn/evidence/text support
must be nonzero; negotiate 64 admitted chats for the initial Cody profile or
reject the composition explicitly. Choices need capacity for admitted questions;
all simultaneous pool limits still apply. No behavior is selected by OS/board name.

Lock this manifest for the session. Source epoch changes require a fresh endpoint
handshake/offer/select/accepted and then a requested snapshot for each bound source.
An epoch in an ordinary snapshot or delta cannot authorize its own adoption.
The receiver accepts only the current session and its exact bound epochs; old
session packets cannot trigger resync or swap epochs back. Random epoch tokens
are compared for equality, never ordering. Source/entity tokens survive epochs.
Unchanged facts retain their observation identity and original provenance.

## Binary datagrams

All integers use unsigned network byte order. IDs shown as hex in JSON are raw
bytes in headers. Never serialize a C++ struct, padding, pointer or enum ordinal
implicitly. The fixed 144-byte header is followed by payload and a 32-byte tag:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `CDS1` |
| 4 | 1 | Wire version 1 |
| 5 | 1 | Packet kind: DATA=1, ACK=2, PING=3, PONG=4 |
| 6 | 2 | Flags, must be zero |
| 8 | 6 | Device ID |
| 14 | 16 | PC ID |
| 30 | 16 | Current session ID |
| 46 | 8 | Nonzero per-direction packet sequence |
| 54 | 16 | Transfer ID; probe nonce for PING/PONG |
| 70 | 8 | Source ID; zero for control documents/probes |
| 78 | 8 | Source epoch; zero for control documents/probes |
| 86 | 8 | Target revision; zero for control documents/probes |
| 94 | 4 | Zero-based DATA chunk index; ACK next missing chunk |
| 98 | 4 | Total DATA chunk count |
| 102 | 4 | Total exact JSON document bytes |
| 106 | 2 | This packet's payload length |
| 108 | 32 | SHA-256 of the entire frozen JSON byte stream |
| 140 | 4 | Reserved, must be zero |

The datagram tag is HMAC-SHA256(active link key,
`ASCII("cody.state.datagram/1") || 0x00 || role || header || payload`). Role is one
byte: 0 for device sender, 1 for broker sender. Both IDs, session, exact endpoint,
role, packet kind, lengths and tag must match before allocations or staging.
No new encryption primitive or derived-key persistence is needed. Verify tags
in constant time; unauthenticated failures are silently dropped/rate-limited,
not reflected as signed errors to arbitrary endpoints. Reuse the ingress budget
owner before parsing/HMAC work. HMAC authenticates but **does not encrypt** chat
titles, question text or other metadata; same-LAN confidentiality is not claimed.

DATA payloads are exactly 1,024 bytes except the final remainder; maximum datagram
is 1,200 bytes including header/tag. Maximum state document is 8 MiB (8,192 chunks),
maximum control document 4 KiB. Negotiation may reduce document/window bounds,
not change chunk size/header layout. Reject zero totals, inconsistent counts,
wrong last length, unknown kinds/flags/reserved bytes and trailing datagram bytes.
The 1,200-byte UDP payload fits a 1,280-byte IPv6 path MTU including ordinary
headers; transport must still report an actual send/MTU error, not use IP
fragmentation as snapshot assembly.

All chunks repeat the same transfer ID, source, epoch, target revision, byte count,
chunk count and document hash. Conflicting repeats reject the transfer. Only
snapshot/delta use nonzero source/epoch/revision in the header; these must match
the parsed envelope. Controls use zero for all three, even source-specific
requests. Transfer IDs are fresh nonzero 128-bit random values per frozen response;
all retries within that transfer keep its bytes, ID and digest unchanged.

## Streaming, acknowledgements and bounded work

Use a selective-repeat window of at most 16 chunks, negotiated downward if needed.
The receiver buffers at most that window, feeds contiguous bytes to an incremental
JSON decoder and SHA-256, and writes only to staging. No complete encoded snapshot
or unbounded JSON DOM is buffered. The decoder uses at most a 4 KiB decoded record workspace,
1,024-byte decoded string, eight-container stack and negotiated pool/index space.
The 64 KiB baseline decoder scratch must cover packet, window, parser, record,
hash and bookkeeping buffers together. Actual allocations/frame sizes require
later compilation and measurement; exceeding scratch is a capacity error.

ACK repeats the transfer metadata/hash; header chunk index is the first missing
chunk, or chunk count after all bytes are consumed. Its 12-byte payload is:
`u16 received_bitmap`, `u8 status`, `u8 error`, `u64 committed_revision`.
Bitmap bit i acknowledges `next_missing+i`, within the negotiated window/count;
bits beyond either bound must be zero. Status 1=Receiving, 2=Committed, 3=Rejected.
Receiving/Committed require error zero; Rejected requires a defined nonzero code.
Committed requires next_missing=count and bitmap zero. A full receive ACK is not
a committed-generation ACK. Source-less control ACKs use committed_revision zero.
For state, a commit ACK names the exact target revision; earlier receive ACKs
carry the still-current revision (zero if none).

The sender retains/reconstructs only the unacknowledged window from an immutable
producer generation. Before sending, a bounded first serialization pass computes
byte count and SHA-256; a second pass emits exactly those bytes with the same
frozen ages, field order and encoder settings. No full raw document is required.
Retry after 500 ms, at most five retries per chunk. A retry
uses a new packet sequence but identical logical bytes; it never refreshes facts.
Reject authenticated chunks outside the advertised window without allocating
more storage; reply with the current receive ACK. An exact duplicate inside the
window is harmless. A consumed chunk outside retained bytes cannot modify staging
and may only receive the existing cumulative ACK. Final whole-document hash and
semantic admission must pass before commit. Retain the final ACK/digest until a
new transfer or session replaces it so a lost commit ACK can be repeated.
An authenticated new `next` with base equal to the in-flight target revision,
same source and epoch is also evidence that the device committed that transfer;
the broker may release its old window before answering the new request. Otherwise
Busy/retries preserve the original pending transfer, without moving deadlines.

Transfer idle timeout is five seconds without contiguous progress; absolute
timeout is 120 seconds from first admission and is never extended. Bound a
request/response lifetime to its original 120-second request deadline as well.
On timeout, allocation failure, cancellation, digest failure or decoder error,
discard only staging, report the original cause and keep the committed view.
One incoming and one outgoing transfer per peer are permitted; extra work gets
Busy. On endpoint/key/session change, cancel in-flight transfers and renegotiate.

Packet sequences increase independently per sender/session. Maintain a 64-packet
anti-replay window. A duplicate sequence never updates liveness or state; a cached
ACK may be repeated for a duplicate request. A sequence outside the window drops.
Sequence exhaustion requires a new authenticated session; never wrap silently.
Every two seconds each peer can issue one PING with a new 128-bit nonce. PONG
echoes that nonce within two seconds. Probe source/epoch/revision/chunk fields,
document size, payload length and digest are all zero. Only a matching outstanding
PONG renews that peer's eight-second link lease; captured data/ACKs do not.
Interleave probes and ACKs with the bounded DATA window so bulk transfer cannot
starve liveness. Link expiry cancels transfer admission, retains stale coherent state and requires
fresh endpoint authentication before new metadata is sent.

## Atomic synchronization, coverage and removals

Snapshots begin with base zero and a matching resync request. A fresh authenticated
binding initially has no live baseline; all new deltas wait for its snapshot.
Within the same bound epoch, reject snapshots older than the committed revision.
A solicited snapshot at the same revision may reconcile coverage/ages but cannot
change semantic facts for an existing immutable observation ID. Unsolicited
snapshots are rejected. A committed snapshot clears needs-snapshot only after
full integrity, reference, capacity and lifecycle validation.

A delta requires its exact base equal to the committed revision and a greater
target revision. Revisions can skip numeric values when the producer coalesces
its journal; the explicit base is the continuity proof. A missing/overlapping base
requires resync. A lower target is stale. An equal target is a duplicate candidate
only: ACK it only if its digest and base equal the last committed delta. Conflicting
content for the same revision is RevisionConflict. A state hash alone does not
authorize unknown source epochs. No stale/duplicate transaction emits a new cue.

Coverage contains exactly one row for each of the eleven entity kinds, naming
admitted count, state and omitted count (null when unknown). Complete requires
omitted=0; Unsupported requires admitted=0 and the corresponding unsupported
capability. Counts must match the resulting generation. Completeness concerns
the negotiated live registry and bounded retained history, not all historical
messages or transcripts. Omitted is outside that admitted projection, never a
license to evict something already pinned locally.

An explicit `remove` names kind/id and the transaction's target revision. It must
not also be upserted in that transaction. Apply additions, replacements and
removals as one staged graph. A complete snapshot may remove absent unpinned
terminal/history rows; partial/unknown coverage never infers deletion. Pin live
calls/agents/operations, unresolved questions/answers and all their required
turns/definitions/choices. Even an explicit removal cannot erase a pinned row
without corresponding authoritative terminal resolution in the same transaction.
If a snapshot omits such a row, reject it and request reconciliation; do not lose
the unresolved record or fabricate a new relationship to current_turn.

Retain tombstones through the receiver's replay window. If tombstone capacity or
producer history is exhausted, force full resync; older deltas cannot revive a
removed identity. A removed token is never assigned to a different object. Every
mandatory reference must resolve; optional unknown references remain explicit.
Check parent cycles, exact original turns, question/choice associations, native
approval restrictions and terminal conflicts before the single generation swap.

The producer retains at most 256 journal transactions and 512 KiB of encoded
delta history per source. Exceeding either bound requires a snapshot for peers
whose base is no longer available. Freeze one coherent source generation while
sending; newer changes enter this bounded journal. After commit, the device asks
for next from the snapshot revision. Overflow during transfer does not corrupt
the snapshot: the next request gets NeedSnapshot. There is no unbounded device
delta backlog. Live plus staged domain storage remains bounded by the capacity
helper; do not allocate a third generation while a reader still borrows the old one.

CapacityExceeded rejects the whole transaction and leaves the last revision
unchanged. Error/coverage diagnostics must reveal the exhausted pool and partial
source coverage independently of the retained coherent data. Negotiate a smaller
projection or pause ingress explicitly; do not drop active records silently.

## Cues, outcomes and errors

The source persists a monotonic `cue_watermark` independently of registry revision
and epoch. Each delta cue has a nonzero counter ID, target entity kind/token and
semantic event: question_opened, reply_available or turn_outcome. Up to 32 cues
are allowed; cue IDs must be consecutive and ordered from the receiver's prior
watermark plus one through the envelope watermark. Empty cues require an unchanged
watermark in a delta. Snapshots may advance it silently but cannot decrease a
previously retained watermark for the same source namespace. Targets must resolve in the committed
result. A cue requests no expression, sound name or screen coordinate.

Snapshots carry an empty cue list. Initial sync, reboot and reconnect establish
the current watermark silently and do not replay prior celebrations or sounds.
If a delta would exceed the cue bound or lose continuity, send NeedSnapshot and
resynchronize quietly. Commit entities before advancing the watermark/dispatching
new cue candidates. Consumers still apply their own visibility/audio/quiet-hours
policy. A delivered state/cue ACK says nothing about a question answer reaching
its recipient. That acknowledgement remains an explicit AnswerAttempt fact.

Reject codes are stable: 1 Malformed, 2 UnsupportedVersion, 3 UnsupportedCapability,
4 WrongBinding, 5 RevisionConflict, 6 NeedSnapshot, 7 MissingReference,
8 CapacityExceeded, 9 TransferTimeout, 10 Integrity, 11 Busy, 12 Persistence,
13 InvalidTransition. Optional error detail/pool is bounded printable text and
contains no credentials. Untrusted failures get no reflected diagnostic.
Errors never manufacture successful empty results or advance admitted revision.

## Source-review and execution gates

Phase 02 supplies the schema, fixtures, wire constants and revision/chunk guards.
Actual HMAC parsing, incremental decoding, replay windows, producer journal,
negotiation owner and device store are integration work in phases 03/05/14.
The guards are preliminary decisions, not an authentication or entity validator.
Phase 11 must exercise malformed lengths/JSON, foreign bindings, replay, reversed
clocks, hash failure, missing chunks, lost commit ACK, epoch changes, partial
snapshots, pool exhaustion, journal overflow and unresolved retention. The
authored examples include expected outcomes; they have not been executed.
