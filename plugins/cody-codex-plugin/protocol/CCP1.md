# CCP1 local pairing protocol

CCP1 connects one physical Cody device to one local Cody's Codex Plugin (CCP)
runtime. It provides bounded UDP commissioning, liveness, selected chat
activity and semantic question/answer exchange. It does not transport full chat
transcripts or audio.

## Current activity=5 / events=3 contract (14 October 2026)

This section supersedes the historical activity=4, events=2 and EVENT v1 wire
examples below. Current source uses the [host encoder/decoder](../mcp/ccp_interactions.py),
[firmware decoder](../../../components/LUNASHELL_platform/src/network/LUNASHELL_codex_events.cpp)
and [authenticated envelope owner](../mcp/ccp_server.py). Pairing and the fresh
READY/COMMITTED handshake retain their existing contract.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=5|events=3|current=<32hex>|state=<device-state>|event_select=<32hex>|proof=<64hex>
HMAC input: find-events-activity-v6|<device>|<pc>|<nonce>|<current>|<state>|<event_select>
```

`event_select` is all zeroes unless explicitly selecting another pending question.
Selection is allowed only from `state=pending`; it neither changes the followed
chat nor answers a question. A sending/received reply and an unacknowledged
terminal result retain ownership. Multiple independently owned questions may
queue, with at most two pending summaries displayed at once. Questions take
priority over informational notices; different native chats keep separate notices.
An authenticated `EVENTACK state=dismissed` retires only the published
informational notice, never a question or an answer.

ACTIVITY appends `title`, `titlestate`, `titleage` after `branch`, before `proof`.
Its HMAC domain is `activity-v3`, covering device, pc, nonce, chat, revision,
phase, project, description, branch and those three title fields in that order.
The exact native title is at most 255 printable ASCII bytes, hex encoded; its
state is `current`, `unavailable`, `unsupported` or `error`. Invalid title/age
fields are `-`; a current title retains its source age and 45-second lease.
TECH remains `technical-v2`. EVENT, REPLY, REPLYACK and EVENTACK retain the
`event`, `reply`, `reply-ack` and `event-ack` domains and envelope field order
documented below; only the negotiated FIND domain changes.

EVENT binary **schema 2** starts with `u8 version=2`. It keeps the historical
field order through `total`, with the following current limits and appended fields:

| Field | Current schema 2 contract |
| --- | --- |
| kind | Existing IDs unchanged: 1 Notice, 2 Question, 3 ApprovalOnPc, 4 PrPending, 5 ChecksRunning, 6 ChecksFailed, 7 ChecksPassed; appended 8 ReplyReady |
| context / title / body | Length-prefixed printable ASCII: 0..24 / 1..24 / 1..63 bytes |
| choices | Question only, exactly 2 or 3; each ID 1..12, label 1..63, detail 1..255 ASCII bytes, followed by the unchanged u8 tone |
| selected_id / failures / progress | Existing identity, at-most-six 48-byte failure names and big-endian u16 count rules remain unchanged |
| pending_question_count | Appended after `total`: u8, 0..128 outstanding questions; zero for nonquestions |
| pending_entry_count | Next u8, 0..2 and no greater than the question count; zero for nonquestions |
| pending entries | Each has length-prefixed nonzero 32-lowercase-hex request ID, context 0..24 and title 1..24 ASCII bytes; IDs are unique |

Schema 2 admits at most **1,536 binary bytes / 3,072 lowercase hexadecimal
characters**. Datagram caps are **EVENT 4,096**, **ACTIVITY 2,048**, **TECH 1,024**
and **512 bytes for other packets**. There is no fragmentation or silent text
truncation; schema 1, unknown kinds and contradictory/trailing fields are rejected.

`ReplyReady=8` is native-lifecycle-only: the first genuine background-chat `Stop`
may enqueue it once per turn; public notification tools cannot manufacture it.
Its title is `Reply ready`, and its body is the complete current native chat title
when it fits 63 ASCII bytes, otherwise `Open the other chat in Codex`. The existing
title owner performs one exact read-only index lookup for an eligible physical
recipient, with session/turn/checkout revalidation and no transcript read or focus
change. This event carries no choices or check progress and never implies task
success. Native approvals continue on the PC.

## Authenticated Codex client presence (15 October 2026)

App presence is separate from retained pairing, READY/LAN liveness and the followed
chat's ACTIVITY phase. Its source is the existing MCP transport lease owner,
`mcp/ccp_presence.py`: a real transport start/renewal establishes `open`; normal
EOF releasing the last known live transport establishes `closed`. Hook-only
broker startup establishes neither. `SessionEnd` cannot establish app closure:
the [official hooks documentation](https://learn.chatgpt.com/docs/hooks#sessionend)
also defines it for archive/delete and abandoned conversations.

After READY and COMMITTED, capable firmware sends:

```text
CCP1|APPCAP|device=<12hex>|pc=<32hex>|nonce=<32hex>|app=1|proof=<64hex>
HMAC input: app-cap-v1|<device>|<pc>|<nonce>|1

CCP1|APPSTATE|device=<12hex>|pc=<32hex>|nonce=<32hex>|sequence=<16hex>|state=<state>|reason=<reason>|age=<milliseconds-or-dash>|proof=<64hex>
HMAC input: app-state-v1|<device>|<pc>|<nonce>|<sequence>|<state>|<reason>|<age>
```

Both envelopes retain the **512-byte** cap. APPCAP must match the current committed
active key, nonce and observed UDP endpoint; an incapable peer receives no
APPSTATE. APPSTATE validates the active key, current READY nonce, exact endpoint
and strictly increasing nonzero unsigned 64-bit sequence. Duplicate/out-of-order
packets cannot refresh evidence. A repeated READY with the same nonce does not
reset the receiver's sequence; only a newly authenticated nonce does.

`state` is `open`, `closed` or `unavailable`. Open/Closed require `reason=none`
and canonical unsigned decimal `age` in milliseconds, bounded to uint32.
Unavailable requires `age=-` and reason `unobserved`, `expired` or `transport`.
The firmware maps these to NOT_FOUND, TIMEOUT and FAIL respectively. Source age
is a host duration, never a comparison of clocks on two machines. Open requires
a genuine lease less than 60 seconds old; every accepted state also depends on
an authenticated packet less than eight seconds old. Expiry becomes Unavailable,
never Closed, and retains the original receipt/source timestamps as evidence.

Lease transitions push through the existing UDP owner; each fresh APPCAP also
returns current state. The existing one-second broker loop detects expiry and
retries optional publication failures; no new task, timer or process scan is
introduced. Up to 64 active transports and 128 capable peers are retained. An
ambiguous expired/failed transport prevents a later normal release of another
client from claiming all clients closed for that broker lifetime. Shutdown grace
remains 60 seconds after the last genuine lease/release, allowing authenticated
polls to recover a lost final packet. Session hooks can keep the broker alive but
cannot create an Open lease or a Closed transition.

Normal EOF proves the plugin's MCP client connection ended; plugin reload or
disable can also do that. It is not an independent OS/window observation. A
killed bridge, failed release or lost connection remains Unavailable when its
lease expires. `ccp_connection_status.app_presence` identifies
`source=mcp_transport`, source age and any original optional publication error.
Presence never switches the followed chat, answers a question, confirms native
audio availability or erases a pairing. The bounded canonical firmware owner is
[`CodexAppPresenceSnapshot`](../../../components/LUNASHELL_platform/include/LUNASHELL_codex_presence.hpp),
appended as `CodexLinkSnapshot.app_presence`.

The development plugin remains version 0.6.8 for this incremental source update.
New clients send local `release` with `id` and explicit boolean `normal`.
An already-loaded client's earlier `release` containing only `id` retires that
lease as abnormal and logs the missing EOF evidence; it can never establish
Closed. Other missing/unknown fields and nonboolean `normal` values are rejected.
An already-running broker does not hot-reload installed Python source. A real
Codex reopen that starts the updated broker/client is required for the new
presence and EOF reporting; cache installation alone does not prove activation.

## Identity and ports

- Cody listens on UDP port `42117` while pairing.
- CCP listens on UDP port `42118` while Codex has the plugin running.
- A device ID is the 12 lowercase hexadecimal digits of the station MAC.
- A PC ID is 32 random lowercase hexadecimal digits retained by CCP.
- The displayed code contains six characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`.

The simulator uses a separate 12-hex-digit identity, always labelled SIMULATED.
Discovery requires explicit inclusion; this identity is never represented as a
measured physical-device MAC.

Every datagram is printable ASCII and uses `|` separators. Ordinary packets
admit 512 bytes; ACTIVITY admits 2048, TECH 1024 and EVENT 4096.
Unknown, duplicated or missing fields are rejected. Senders use the canonical
field order shown below for reproducible proofs and diagnostics.

## Pairing

CCP broadcasts:

```text
CCP1|DISCOVER|request=<16 hex>
```

A Cody with an active pairing window replies:

```text
CCP1|OFFER|request=<16 hex>|device=<12 hex>|session=<8 hex>|salt=<32 hex>|tag=<64 hex>
```

The verifier is PBKDF2-HMAC-SHA256 over the displayed ASCII code and the
16-byte salt, with 50,000 iterations and a 32-byte output. `tag` is
HMAC-SHA256 using that verifier over:

```text
offer|<request>|<device>|<session>|<salt>
```

Derive each offer's verifier from the user-provided code and require exactly
one match. Before the first PAIR, generate the nonce, derive the link key below
and persist the DPAPI-protected PC candidate in `pending-pairings.json`, separate
from active `state.json`. This permits recovery even if every PAIRED reply is lost.
Then send:

```text
CCP1|PAIR|device=<12 hex>|session=<8 hex>|pc=<32 hex>|port=42118|nonce=<32 hex>|proof=<64 hex>
```

`proof` is HMAC-SHA256 using the verifier over:

```text
pair|<device>|<session>|<pc>|42118|<nonce>
```

Both peers derive the link key as HMAC-SHA256 using the verifier over:

```text
link|<device>|<session>|<pc>|<nonce>
```

Cody persists the authenticated candidate in `ccp_pending`, separate from active
`ccp_pairing`. The active v1 schema and pairing messages are retained, with the
post-commit COMMITTED confirmation below. The previous link remains active until
proof that the PC persisted the candidate. Only a successful pending write permits:

```text
CCP1|PAIRED|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The confirmation proof uses the link key over:

```text
paired|<device>|<session>|<pc>|<nonce>
```

PAIRED confirms Cody retained the candidate, without claiming an active new link.
The PC candidate is already persisted without replacing its old active record.
After validating PAIRED, the PC may send:

```text
CCP1|CONFIRM|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The proof uses the persisted link key over:

```text
confirm|<device>|<session>|<pc>|<nonce>
```

Keep the pairing screen until valid PC proof. Authenticated CONFIRM promotes
the candidate to active storage and removes pending; only successful completion
of both operations permits:

```text
CCP1|ACTIVE|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The activation proof covers:

```text
active|<device>|<session>|<pc>|<nonce>
```

Commissioning completes the tool and promotes the PC candidate only after
authenticated ACTIVE. Cody shows All set after successfully sending ACTIVE.
For a short window, retain the nonsecret activation tuple so repeated valid
CONFIRM can recover a lost final reply without retaining code/derivation material.

After an interrupted transaction, recover `ccp_pending` and discover the PC with
that candidate. Fresh authenticated READY tied to the current request proves PC
retention and permits Cody to promote it and close its journal without a new code.
The matching COMMITTED then proves device commit so the PC can promote its candidate.
Recovery never fabricates success for an old tool. Missing proof retains real wait/error.

Reject a different candidate while the journal holds one, until Cancel or explicit
replacement. Cancel closes the attempt and removes Cody pending while retaining
active storage. Failed reads, writes, validation or deletion remain repair-required;
Retry alone does not turn them into success.

Confirmed Forget removes only `ccp_pairing` and `ccp_pending` on Cody. Wi-Fi,
skin, preferences and logs remain, as does the independent PC DPAPI record.
There is no remote revocation. Explicit Forget may remove a damaged record;
there is no automatic migration or reset.

The two stores do not promise atomic distributed rollback. Cancel may restore the
previous operational link while leaving the PC candidate unresolved. COMMITTED
under the old key cannot delete that candidate because it may be replayed.
After Cancel, Forget or interrupted first pairing, PC replacement still requires
a new visible code and explicit `replace_pending:true`. Old packets and elapsed
time never authorize automatic deletion.

The displayed code, verifier and pairing salt are wiped when the attempt ends.
Neither side logs them.

## Authenticated rediscovery (CCP 0.6.4)

After commissioning, a retained boot or reconnection, Cody broadcasts a new
128-bit client nonce every two seconds. The authenticated FIND capabilities
below retain their existing HMAC domains. FIND alone establishes neither
current presence nor permission to return activity, questions or technical data.

```text
CCP1|FIND|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
HMAC input: find|<device>|<pc>|<nonce>
```

The matching broker verifies the retained key and returns a fresh, signed,
128-bit server challenge. Its `address` and `port` are the device endpoint
observed by the receiving socket, not fields chosen by the requester.

```text
CCP1|READY|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|challenge=<32 hex>|address=<IPv4>|port=42117|proof=<64 hex>
HMAC input: ready-v2|<device>|<pc>|<nonce>|<challenge>|<address>|<port>
```

Cody verifies the HMAC, current client nonce, fixed broker source port and its
own endpoint: `address` must match current canonical Wi-Fi IPv4 and `port` must
be 42117. The simulator requires its explicit loopback endpoint. A relay cannot
ask Cody to sign a challenge addressed to the relay's socket. No consumer reads
radio hardware or guesses an unavailable address.

READY can finish a retained candidate transaction after reboot. Only after the
active device key is durably written and read back does Cody acknowledge it:

```text
CCP1|COMMITTED|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|challenge=<32 hex>|address=<IPv4>|port=42117|proof=<64 hex>
HMAC input: committed-v2|<device>|<pc>|<nonce>|<challenge>|<address>|<port>
```

The broker retains at most 128 endpoint-scoped challenges for two monotonic
seconds. A duplicate FIND for the same endpoint and nonce does not renew that
challenge's deadline. COMMITTED must match its original socket endpoint,
nonce, fresh challenge and proof. The challenge is consumed before persistence
or outgoing effects; duplicate, expired and previously captured acknowledgements
cannot renew liveness or disclose another snapshot.

Only this completed exchange marks the device seen, admits the event peer and
returns ACTIVITY, TECH or EVENT. An old captured FIND can obtain a fresh
challenge but cannot obtain current metadata without proving the retained key
at the signed device endpoint. A failed or lost exchange waits for Cody's next
fresh FIND. The ten-second PC presence lease uses monotonic time; UTC timestamps
are retained only for status presentation.

The broker promotes a matching pending candidate after the fresh COMMITTED.
Proof with the previous active key preserves that association without retiring
an unrelated pending candidate. `pairing_pending` and `retained_pairing` remain
separate. The exclusive local listener is acquired before protected identity
access, and a stopped responder remains an actual MCP tool error.

CCP 0.6.4, firmware and the explicit simulator must be updated together.
Earlier READY/COMMITTED layouts and HMAC domains are rejected; there is no
compatibility fallback. The saved pairing record format remains unchanged.

## Historical activity=4 contract and presentation

The examples and presentation description in this section preserve the earlier
activity contract; the current activity=5/events=3 contract is specified above.
That firmware negotiated `activity=4`, with or without the separate events
capability. Each capability is bound to its own HMAC domain; stripping or
changing a capability never reuses a presence proof. Version three adds the
separate TECH projection below; its ACTIVITY payload keeps version-two fields.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=4|proof=<64hex>
HMAC input: find-activity-v4|<device>|<pc>|<nonce>

CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=4|events=2|current=<32hex>|state=<device-state>|proof=<64hex>
HMAC input: find-events-activity-v4|<device>|<pc>|<nonce>|<current>|<state>

CCP1|ACTIVITY|device=<12hex>|pc=<32hex>|nonce=<32hex>|chat=<64hex>|revision=<16hex>|phase=<token>|project=<hex or ->|description=<hex or ->|branch=<hex or ->|proof=<64hex>
HMAC input: activity-v2|<device>|<pc>|<nonce>|<chat>|<revision>|<phase>|<project>|<description>|<branch>
```

The PC sends this complete activity snapshot only after the fresh COMMITTED. The link
key authenticates every field, in exactly the documented order. `chat` is the
SHA-256 of the followed opaque Codex session ID; all zeroes denote no followed
prompt. `revision` is a volatile diagnostic counter, not a persisted ordering
guarantee. The current random FIND nonce admits at most one ACTIVITY snapshot;
old polls, duplicate replies and altered metadata are rejected.

ACTIVITY and TECH use separate 1024-byte extension envelopes. EVENT retains its
1280-byte envelope; every other datagram retains its original 512-byte bound.
There is no fragmentation. Printable ASCII text is hex encoded: `project` and
`branch` each admit at most 64 characters, `description` at most 192. `-` means
no description or unavailable branch. A no-chat snapshot requires `phase=none`,
zero chat, `project=-`, `description=-` and `branch=-`. A description is admitted
only for actual `working`, `editing` or `running` phases.

The hook ingestion owner obtains the actual branch from the nearest checkout's
`.git/HEAD`, including an explicit worktree `gitdir` pointer. Detached HEAD is
reported as `DETACHED`; an absent checkout or failed/unrepresentable read makes
the branch unavailable and preserves the real diagnostic. No Git command,
transcript, raw tool input or guessed branch is used. Project metadata uses the
actual workspace leaf within its 64-character ASCII contract; unrepresentable
labels explicitly report `UNAVAILABLE`. Measured overflow scrolling belongs to
the native display; authenticated metadata retains the accepted full label.

Version-one peers continue to negotiate `activity=1`, using `find-activity` or
`find-events-v2`. Their ACTIVITY fields remain device, pc, nonce, chat, revision,
phase, project and proof, authenticated with `activity`. Their 24-byte project
slot receives a separately logged display projection when a newer owner holds
a longer name. Version-one responses remain below 512 bytes. Version-two peers
use `find-activity-v2` or `find-events-activity-v2` and receive ACTIVITY without
TECH. Current firmware requires the version-three contract it requests; it
does not silently downgrade.

Phases are `none`, `active`, `working`, `editing`, `running`, `ready`, `paused`,
`interrupted`, `ended` and `unavailable`. A genuine `apply_patch` tool, including
native namespace qualification, maps to editing. `Bash`, `exec_command`,
`write_stdin` and `shell_command` map to running. Other tools, including opaque
code orchestration, remain generic working; arguments are never mined for an
invented command or purpose. Latest-state polling can miss brief tool bursts.

The canonical DeviceSnapshotService receives activity validity, health, original
observation times and real errors. UI consumers never read host sockets. Missing
PC presence after eight seconds uses the connection-lost view; expired activity
remains unavailable. A retained label identifies context while disconnected.

Hooks retain only event/session/turn/tool identities, cwd, bounded checkout
metadata, reported model and genuine compaction trigger through the
DPAPI-authenticated loopback owner. No prompts, tool
arguments, responses or transcript paths are retained. Only the latest genuinely
emitted UserPromptSubmit follows a chat; background events cannot switch it.
SessionStart registers metadata only. Broker restart waits for a new real prompt;
LAN recovery alone retains context. Active phases expire after 120 seconds
without a genuine event; MCP leases and description reports never renew them.
Rejected turn correlations log the event and expected/observed turn hashes,
without weakening old-turn protection or persisting raw hook contents.

`ccp_report_progress(session_id, turn_id, description)` adds a short actual
purpose only to the currently followed active turn. Identities come from
`ccp_list_sessions`. It cannot select a chat, change a hook phase or claim task
completion. Reports reject inactive/stale turns and unsupported text. Purpose
expires independently after 120 seconds and clears on a new prompt, Stop,
PermissionRequest, Interrupt or SessionEnd. The skill asks for actual English
work descriptions, never commands, logs, percentages or invented estimates.

Firmware and Cody Studio use shared native presentation helpers. The main title
shows Working, Editing code, Running command, Waiting for you or Reply ready.
Ordinary tool subtypes require 2.5 seconds of stable observation while generic
Working covers short bursts; completion, stale data and attention bypass that
dwell. The observed work clock starts before subtype admission, ignores repaint
and purpose changes, pauses during genuine interaction views and disappears on
completion/unavailability. Its timer appears only after ten observed seconds;
it is neither a CPU measurement nor a progress estimate.

The fixed identity row rotates project, `branch: <actual branch>` and
`model: <current reported model>` with minimum holds of five, three and four
seconds. Each transition fades out for 200 ms, remains black for 100 ms, then
fades in for 200 ms. Missing branch/model observations are omitted. Project is
white; branch uses WhiteSmoke, and model uses its current cost-band color.
Actual attention keeps the project visible. Native font metrics
measure long identity captions and scroll their bounded native viewport. Long
identity dwell extends until its full scroll and end hold complete. A purpose fitting
the 320-pixel viewport stays still; overflow scrolls at 20 pixels per second
with 1.2-second start and 1.8-second end holds. This animation never triggers
hook activity, typewriter counts, touch admission or repeated audio.

Home reserves a separate persistent chip at y=330-357: green Economy, orange
Balanced, red Expensive!, gray Unknown. It stays centered without a timer, or
moves into the left slot beside a current work/compaction timer. This chip does
not participate in the identity fade and never replaces compaction feedback.
Model/catalog freshness comes from the canonical observation; a band-only
change redraws the chip without restarting the carousel or replaying audio.

## Authenticated technical metadata

Version-four FIND yields READY; fresh COMMITTED then yields ACTIVITY and TECH. Activity and technical
metadata are acquired under one ActivityStore lock, preserving one chat and
revision even when a hook arrives during UDP transmission. EVENT and other
independent extensions retain their own authenticated contracts.

```text
CCP1|TECH|device=<12hex>|pc=<32hex>|nonce=<32hex>|chat=<64hex>|turn=<64hex>|revision=<16hex>|model=<hex or ->|effort=<hex or ->|modelage=<ms or ->|compaction=<token>|compactseq=<16hex>|compactage=<ms or ->|activityage=<ms or ->|turnelapsed=<ms or ->|used=<uint64 or ->|capacity=<uint64 or ->|cumulative=<uint64 or ->|input=<uint64 or ->|output=<uint64 or ->|threshold=<uint64 or ->|usageage=<ms or ->|cost=<band>|costage=<ms or ->|costttl=<ms or ->|proof=<64hex>
HMAC input: technical-v2|<device>|<pc>|<nonce>|<chat>|<turn>|<revision>|<model>|<effort>|<modelage>|<compaction>|<compactseq>|<compactage>|<activityage>|<turnelapsed>|<used>|<capacity>|<cumulative>|<input>|<output>|<threshold>|<usageage>|<cost>|<costage>|<costttl>
```

Field order is mandatory. TECH accepts at most 1024 bytes without fragmentation.
All identifiers and hex text use lowercase hex. `turn` hashes the genuine
current turn ID; `model` retains a full 1..128-byte ASCII slug, with character
codes 33..126, hex encoded. Unrepresentable values are rejected with their real
cause rather than truncated. Milliseconds are canonical unsigned decimal
0..4294967295 with no leading zeroes; `-` means unavailable. A missing model
requires missing modelage and vice versa.

The receiver admits one TECH per current FIND challenge only after READY and
the matching ACTIVITY have been accepted. Chat and revision must exactly match
that ACTIVITY. TECH and ACTIVITY have independent consumed-nonce flags. TECH
before ACTIVITY, repeated TECH, old challenges, mismatched peers and invalid
proofs are rejected; the next FIND supplies a new coherent snapshot. No TECH ACK
or staged merge with an older activity is used. On pairing replacement, the
technical projection is cleared. Disconnect invalidates transport freshness;
historical metadata may remain visible only with explicit stale/unavailable
status. Transport freshness expires after eight seconds.

A zero chat requires zero turn, missing model and source ages, missing duration,
and `compaction=none|compactseq=0000000000000000|compactage=-`. A known idle chat
may have zero turn only while model, duration and compaction are unavailable;
its last genuine activity observation may still have an age. SessionStart after
an ended chat preserves the known chat without inventing a new turn.

| Field | Actual source and meaning |
| --- | --- |
| model, modelage | Exact native turn-context model or admitted hook model, with its original source age; not proof of rerouted execution |
| effort | Bounded native turn-context reasoning effort, hex-encoded printable ASCII, or unavailable |
| compaction, compactseq, compactage | Genuine PreCompact/PostCompact lifecycle within the exact current chat/turn; age of the latest observed lifecycle transition |
| activityage | Age of the latest admitted hook emission; a broker heartbeat never refreshes it |
| turnelapsed | Observed UserPromptSubmit-to-now duration, frozen at genuine Stop, Interrupt or SessionEnd |
| used, capacity | Native last_token_usage.total_tokens and explicit model_context_window from the same token-count metadata |
| cumulative, input, output | Native total_token_usage.total_tokens, last_token_usage.input_tokens and last_token_usage.output_tokens |
| threshold, usageage | Explicit automatic threshold when observed (currently unavailable); original age of the native token sample |
| cost | Exact-model qualitative Codex Standard catalog band: `unknown`, `economy`, `balanced`, `expensive`; no rates/currency/bill |
| costage | Original public-catalog retrieval age on the PC, independent of hook/packet liveness |
| costttl | Remaining PC local-calendar lease until next midnight; no receiver wall-clock comparison |

Known cost requires a current model observation (model age < 120000 ms), and
catalog age/expiry <= 93600000 ms (26 hours, permitting DST); expiry must be
positive. Unknown requires both durations to be `-`. Firmware independently
expires cost on this lease, stale model/transport or disconnection. Repeated
packets do not change the original retrieval age or the catalog's local day.
The source, numeric rates, attempt/error state and classification thresholds
remain on the PC; see the [catalog policy](../README.md).

Counts are canonical uint64 decimal with no signs, padding or leading zeroes.
Presence is independent of value: measured zero is not absence. Any present
counter requires usageage; a present capacity must be positive. Both used and
capacity are required for Free and the bar. Free subtracts the last-call total
from that sample's window; it does not promise exact future-request headroom.
Cumulative usage is never substituted for window use. The currently unobserved
threshold stays `-`. No percentage or free-token bar is derived from missing data.
Native journal timestamps are mapped once to monotonic age, preserving original
source time rather than the polling time. Hook source ages use the PC monotonic clock and genuine
emission timestamp, distinct from receiver-local transport timestamps. Catalog
retrieval age and its local-day lease use the PC wall clock; the receiver expires
that lease using its own monotonic elapsed time, without comparing wall clocks.
Native token, reported model and active compaction observations expire independently after
120 seconds. Same-turn steering preserves the observed turn start and active
tools; a new actual turn clears the previous model and compaction scope.

Compaction tokens are `none`, `active`, `refreshed`, `orphaned`, `interrupted`
and `stale`. `none` requires zero sequence and missing compactage; other states
require a nonzero sequence and nonzero turn. PreCompact accepts only genuine
`auto`/`manual` trigger metadata, increments the lifecycle sequence and enters
active. A matching PostCompact changes active to refreshed. Ordinary tool hooks
never clear it. Duplicate boundaries and old-turn completions are rejected.
PostCompact without an observed start reports orphaned, never success. Stop
without PostCompact reports lost updates; interruption or session end reports
interrupted. An expired active observation projects stale, never completion.
Only active compactage denotes observed compaction start age; other ages are
transition ages, not an invented operation duration. Even a genuine refreshed
state supplies no new token sample and cannot revive old token numerators.

Official Codex documents the common hook model field and genuine compact
boundaries in [Hooks](https://learn.chatgpt.com/docs/hooks), and
`thread/tokenUsage/updated` plus `model/rerouted` in
[App Server](https://learn.chatgpt.com/docs/app-server). The plugin reads allowlisted
local native journal metadata for the genuine hook-owned chat; it does not parse
conversation text for estimates. No second App Server or chat resume/start is
introduced. Changed plugin hooks require native review before execution;
enabling the plugin alone does not establish trusted live hook delivery.

## Historical EVENT v1 / events=2 contract (CCP 0.6)

The v1 payload table, earlier size limits and single-question queue description
below are retained as historical protocol evidence. The current schema 2 and
multi-question contract above supersede them. EVENT/REPLY/REPLYACK/EVENTACK proof
domains and recipient-confirmed answer semantics remain as documented here.

Automatic chat metadata and explicit MCP requests have separate ownership.
An MCP request creates a random 128-bit ticket and 256-bit capability, disclosed
only to its creator. `context` is caller-declared, without establishing chat identity
or focus. Explicit questions need no invented hook. Genuine PermissionRequest
produces only On your PC; native authorization stays in the normal Codex prompt.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=1|events=2|current=<32hex>|state=<device-state>|proof=<64hex>
HMAC input: find-events-v2|<device>|<pc>|<nonce>|<current>|<state>

CCP1|EVENT|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<state>|ttl=<seconds>|data=<hex or ->|proof=<64hex>
HMAC input: event|<device>|<pc>|<nonce>|<id>|<state>|<ttl>|<data>

CCP1|REPLY|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|proof=<64hex>
HMAC input: reply|<device>|<pc>|<nonce>|<id>|<choice>

CCP1|REPLYACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|status=<status>|proof=<64hex>
HMAC input: reply-ack|<device>|<pc>|<nonce>|<id>|<choice>|<status>

CCP1|EVENTACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<terminal-state>|proof=<64hex>
HMAC input: event-ack|<device>|<pc>|<nonce>|<id>|<state>
```

`events=2` accepts activity version 1, 2 or 3 with the matching HMAC domain.
`current` is the observed question ID, or all zeroes when absent. `state=none`
requires zero ID; other device states are pending, sending, received, delivered,
cancelled, expired or rejected. Notifications are not open questions.
EVENT admits 1280 bytes, ACTIVITY v2 and TECH admit 1024, and other packets
retain 512. Application messages are never fragmented.

EVENT is a complete immutable ticket snapshot, signed by the link key and current
FIND nonce. Admit one EVENT publication per nonce after the current authenticated
rediscovery handshake. States are pending, received, delivered, cancelled, expired,
unavailable or none. None uses zero ID, ttl=0 and data=-; it is not an answer.
TTL is remaining seconds rounded up, 0..180, not UTC; pending/received require
at least one second. Repetition never extends the original local deadline.
Offline retains question/route; timeout, restart or LAN return never selects a choice.

Since CCP 0.6.2, an open question unknown to the current association's broker
(for example after restart) first receives EVENT unavailable with the same ID,
ttl=0 and data=-. A queued successor waits. Cody retires only its own question,
keeps the local choice as unconfirmed delivery and publishes rejected with
ESP_ERR_NOT_FOUND. A later signed terminal FIND, or absence after Done, confirms
retirement before new publication. Frame/canonical publication failure repeats
retirement. An unknown ticket needs no EVENTACK; this cannot reconstruct answers
or extend deadlines.

`data` is lowercase hexadecimal bytes. The v1 payload contains these fields in order:

| Field | Format and limit |
| --- | --- |
| version | u8, value 1 |
| kind | u8: 1 notice, 2 question, 3 approval_on_pc, 4 pr_pending, 5 checks_running, 6 checks_failed, 7 checks_passed |
| context | u8 length + 0..24 printable ASCII bytes |
| title | u8 length + 1..24 printable ASCII bytes |
| body | u8 length + 1..63 printable ASCII bytes |
| choice_count | u8: 2 or 3 for question only, otherwise 0 |
| choices | Per choice: id, label, detail, tone as below |
| selected_id | u8 length + 0..12 bytes; empty until an answer |
| failure_count | u8, at most 6; nonzero only for checks_failed |
| failure_names | Per name: u8 length + 1..48 printable ASCII bytes |
| progress_valid | u8, exactly 0 or 1 |
| passed | u16 big endian; zero when progress_valid=0 |
| total | u16 big endian; zero when progress_valid=0 |

Each choice contains id (u8 length + 1..12 bytes, `[a-z][a-z0-9_]{0,11}`),
label (u8 + 1..12 ASCII), detail (u8 + 1..63 ASCII), and u8 tone:
0 neutral, 1 positive, 2 negative, 3 info. IDs/labels are unique; IDs survive
reordering. Mandatory text cannot be all spaces. Reject excess fields, trailing
bytes, incoherent combinations and oversized content without silent truncation.
PR progress needs the exact head and complete current check list. Stop never
implies passed checks. Failure names come from Codex; reject excess content with
a return to the PC. There are no PR actions in the wire. Valid progress requires
0 <= passed <= total and total > 0. checks_failed names cannot exceed total-passed;
checks_passed requires every check to have passed.

REPLY applies only to question. A valid choice becomes received, meaning the broker
recorded it without authorizing screen 31. The recipient's first MCP poll returns
the choice and random answer_id. A second poll with matching acknowledge_answer_id
confirms receipt and becomes delivered. Only REPLYACK delivered or that EVENT state
permits Reply sent. Other REPLYACK states are received, expired, cancelled, conflict
and unknown. Repeating one choice is idempotent; another cannot overwrite it.
Lost ACK does not clear state: later authenticated FIND/REPLY reports it.

Since CCP 0.6.1, recipient receipt and observed device closure are separate.
A published broker-terminal question precedes its successor until Cody applies
the terminal state and sends EVENTACK. It admits delivered, cancelled, expired
or rejected only, with active key, observed endpoint and current FIND nonce.
It carries no choice, grants no operation permission and does not change the
recipient result. Local expiry may precede delivered: preserve real local closure
without inventing Reply sent or rewriting the MCP answer.

| Transition | Owner | Effect |
| --- | --- | --- |
| pending -> sending | Cody after explicit Send | Locally confirmed choice; delivery unknown |
| pending -> received | Broker after authenticated REPLY | Recorded answer awaiting recipient confirmation |
| received -> delivered | Agent poll with exact answer_id | Recipient confirmed; device closure still pending |
| terminal -> EVENTACK | Cody after canonical publication | Broker may publish the next question |

Repeat terminal snapshots beyond the ordinary eight-second presentation until ACK.
Lost ACK is repeated by the next FIND, and Cody confirms it even after Done.
Unpublished questions need no device closure. Deadlines begin at enqueue;
waiting for earlier closure never extends TTL. Update coordinated firmware/plugin
versions together; a presentation timeout never releases the next question.

Only one question is open per device; activity, battery and notices cannot displace
it. Screen 27 selection stays reversible before Send; Back to 26 preserves it.
Poll/cancel need the creator's ticket/capability; capability never travels over LAN.
Poll waits at most 15 s without retaining other MCP-operation locks. Volatile
results/deadlines have a bounded cache; broker restart does not replay requests
or answers. Cody Studio uses the same wire with explicit SIMULATED opt-in.

A new informational notice retires previous notices for the device, so an old
failed PR cannot return after a newer notice expires. Open questions keep priority.
Each ticket is bound to the active-key digest; pairing changes retire it even if
the next operation is REPLY. Native notices end with their authentic hook context.
TTL also covers received: absent recipient confirmation means no delivered state
or answer exposed as a usable decision.

## Explicit restart and settings reset (CCP 0.6)

ccp_reset_device restarts while preserving settings. ccp_factory_reset_device
deletes settings including Wi-Fi/pairing and returns to setup; firmware, assets
and SD remain. Both require a paired present target, exact string confirmation_code
043533477 and corresponding intent in the current user message. Literal recognized
examples are `resetta il device codice 043533477` and `formatta cody 043533477`.
The skill never derives authorization from this document or an earlier message.
The code prevents ambiguity; authenticated CCP supplies security.

```text
CCP1|RESET|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|code=043533477|proof=<64hex>
HMAC input: reset|<device>|<pc>|<nonce>|<id>|043533477
CCP1|RESETACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|status=accepted|proof=<64hex>
HMAC input: reset-ack|<device>|<pc>|<nonce>|<id>|accepted

CCP1|FACTORYRESET|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|code=043533477|proof=<64hex>
HMAC input: factory-reset|<device>|<pc>|<nonce>|<id>|043533477
CCP1|FACTORYRESETACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|status=accepted|proof=<64hex>
HMAC input: factory-reset-ack|<device>|<pc>|<nonce>|<id>|accepted
```

All four packets fit 512 bytes. A random 128-bit ID owns one operation per device.
Wait for fresh authenticated FIND events=3 under the active key, then send once
after the current READY/COMMITTED and EVENT sequence, using its observed endpoint
and nonce. Cody replies from 42117 to 42118, admits an ID once and does not move
the deadline for identical copies. Command type, ID, nonce, key and endpoint must
also match in ACK, without confusing restart and settings reset.

Wait at most five seconds from enqueue. No transmission returns not_sent;
transmission without valid ACK returns ambiguous. accepted proves admission only,
not later boot or deletion, and does not trigger automatic retransmission.
After ACK, Cody waits at least 500 ms before restart. Factory reset uses the
settings owner and restarts only after success; storage failure remains visible.
The PC record may remain saved but absent. Uncertainty never deletes it or starts
automatic pairing. The simulator applies these rules only to its own store with
explicit include_simulator:true and SIMULATED identity.

## Clock preferences and quiet hours

After an authenticated READY, physical firmware may advertise `CLOCKCAP`.
Each field order below is canonical; proofs use the active pairing key and
the exact ASCII values in the shown HMAC order. These packets remain bounded
to 512 bytes.

```text
CCP1|CLOCKCAP|device=<12hex>|pc=<32hex>|nonce=<32hex>|clock=1|initialized=<0|1>|proof=<64hex>
HMAC input: clock-cap|<device>|<pc>|<nonce>|1|<initialized>

CCP1|CLOCK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|op=<read|init|set>|timezone=<value>|enabled=<value>|start=<value>|end=<value>|proof=<64hex>
HMAC input: clock|<device>|<pc>|<nonce>|<id>|<op>|<timezone>|<enabled>|<start>|<end>

CCP1|CLOCKACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|op=<read|init|set>|status=<applied|rejected>|error=<decimal>|initialized=<value>|timezone=<value>|enabled=<value>|start=<value>|end=<value>|proof=<64hex>
HMAC input: clock-ack|<device>|<pc>|<nonce>|<id>|<op>|<status>|<error>|<initialized>|<timezone>|<enabled>|<start>|<end>
```

Timezone identities are `0` UTC, `1` Rome and `2` Ho Chi Minh City. Other
identities are rejected with a real error; an unsupported host timezone is
reported to the user and is never silently substituted. The RTC remains UTC.
Quiet hours use an explicit `enabled` switch (`0` or `1`) and local minutes
`start`/`end` in `0..1439`, forming a half-open window that may cross midnight.
Equal endpoints are rejected when quiet hours are enabled. Numeric fields use
canonical decimal without leading zeroes; `-1` means preserve the existing
setting, never infer a value.

`read` requires all four setting fields to be `-1`. `init` requires a supported
timezone and `enabled=-1|start=-1|end=-1`: automatic first connection adopts only
the host timezone and preserves all quiet-hours preferences. `set` requires at
least one explicit setting. The firmware retains a separate strict durable
`clock_adopted` marker across reconnects and restarts. An initialized device
answers `init` with its existing configuration without changing it. Explicit
`set` seals automatic adoption before changing preferences, so a partial failure
cannot make a later reconnect replace the user's configuration.

Quiet hours default to disabled only when the clock preference key is genuinely
absent. Existing enabled settings survive firmware updates. Disabling quiet
hours on an existing device requires an explicit `set` with `enabled=0`.
Manual Sounds, speaker volume and standby remain independent audio controls.

Only the exact active READY peer, fixed port and challenge are admitted. The
device consumes each CLOCK nonce once and rejects the most recent request ID
again. The host sends a mutation once and does not replay it after a lost ACK;
the result remains ambiguous until a separate authenticated read. ACK validation
retains the request's original nonce and endpoint, even if a newer poll arrives.

`applied` requires `error=0` and confirmed settings. `rejected` carries the original
validation, storage or publication error. When durable and runtime settings
cannot be confirmed together, all five ACK result fields (`initialized`,
`timezone`, `enabled`, `start`, `end`) are `-1`; unavailable values are never
replaced by defaults. A partial write may leave actual confirmed settings in a
rejected ACK, so inspect both its status and values. An ACK confirms the settings
operation, not physical audio output or clock synchronization. NVS operations
run through the settings executor outside the CCP state mutex.

## Resource budgets and security boundary

Ingress budgets apply before decoding, DPAPI unprotect or HMAC work. Source
identities come from the socket; an IP budget never substitutes for authentication.
Global budgets remain in force when spoofed sources churn the bounded peer table.

| Owner | Initial source limits |
| --- | --- |
| Firmware receive slice | At most 8 packets or 5 ms, followed by state/event maintenance |
| Firmware UDP ingress | 32 packets/s, burst 64 globally; 16 packets/s, burst 32 per IPv4; 16 fixed source slots |
| PC UDP ingress | 64 packets/s, burst 128 globally; 16 packets/s, burst 32 per IP; 64 source slots |
| Discovery | 16 offers globally, 4 per source, 128 received packets per bounded discovery pass |
| Pairing verification | At most 16 PBKDF2 candidates and a 2-second local verification budget |
| Endpoint challenges | 128 entries, 2-second monotonic lifetime, consumed once |
| Local control HTTP | Loopback only, bearer authentication, 32 concurrent workers, 3-second socket deadline, 64 KiB body |
| Rejection diagnostics | At most one summary per 5 seconds, with suppressed rejection count |
| Resident broker log | 2 MiB per file; active broker.log plus 2 rotated backups |

The exclusive loopback control listener is acquired before the resident owner
opens these rotating files. Losing startup processes cannot rotate the running
owner's log. The separate launcher `runtime.log` still retains startup failures
in append-only form; it no longer receives resident UDP rejection traffic.
Bounding that bootstrap history remains separate hardening work.

Ambiguous discovery identities or exhausted offer/packet budgets discard the
whole result with an explicit failure. Pairing work overflow fails before any
new candidate is persisted. Firmware receive budgeting prevents a permanently
non-empty socket queue from postponing maintenance indefinitely. Individual
blocking storage or crypto calls are not preempted by the receive time slice.
These initial values bound local resources; tuning and physical timing still
need separately authorized runtime verification. They cannot guarantee service
availability on a hostile or jammed network.

The six-character code is never transmitted, but its 30-bit search space and
public OFFER tag permit offline candidate checking. PBKDF2 increases the work
per guess; it does not remove the offline oracle. A captured full pairing
transcript also contains the public fields needed to derive the resulting link
key after the code is recovered. Expiry and online rate limits do not invalidate
such a captured transcript. No attack-cost benchmark or security certification
has been performed.

CCP authenticates ASCII/hex payloads with HMAC; it does not encrypt them. A LAN
observer able to capture unicast traffic can read project names, purpose,
questions and technical metadata. Replay hardening does not provide
confidentiality or protection against a compromised local computer. Windows
DPAPI protects persisted keys for its current-user boundary, not against code
already executing as that user.

An evaluated standard PAKE or high-entropy QR secret, followed by authenticated
encryption with explicit session/replay ownership, remains deferred. Do not
introduce custom PAKE or present this commissioning protocol as equivalent to
TLS, SPAKE2 or a production security audit. Stable device and PC identities can
be retained when that coordinated future protocol is designed.
## One-way encrypted audio channel (plugin 0.6.5 source increment)

UDP port 42119 carries a separate one-way enhanced PCM16 mono 16 kHz stream.
An utterance is at most 60 seconds, or 1,920,000 real PCM bytes. The device has a
separate ten-second acknowledgement ring, not a sixty-second recording buffer.
Deliberate shake or a double tap discards a recording; a single tap sends it.
Start/send countdowns admit immediate tap cancellation before native submission.
The header is network-order `!4s6s16s16sIBH`: magic, binary device identity,
16-byte utterance identity, 16-byte authenticated discovery nonce, uint32 sequence,
uint8 kind and uint16 plaintext byte count. Device magic is `CYA1`; PC magic is
`CYA2`. Device kinds are START=1 (sequence zero, no PCM), DATA=2 (at most 960 PCM
bytes), END=3 (no PCM) and CANCEL=4 (no PCM, new sequence/nonce). PCM bytes are
little-endian int16. Up to four DATA packets may be in flight; START, END and
CANCEL remain individually acknowledged. ACKs carry the highest contiguous
sequence. Retries within the four-packet window never append PCM twice.
Out-of-order PCM is discarded and receives an ACK for the preceding contiguous
prefix; missing blocks are recovered through identical retries after 100 ms.

The utterance key is HMAC-SHA256(link_key,
`cody-audio-v1|<utterance lowercase hex>|<discovery nonce lowercase hex>`).
AES-256-GCM uses the entire 49-byte header as authenticated data, a 16-byte tag
and the 12-byte nonce `DAT1 || uint64(sequence)` for device packets. Retries repeat
identical ciphertext, never different plaintext under the same nonce. PC ACKs
use `ACK1 || uint64(ack counter)` with a strictly increasing counter. ACK plaintext
is `!IB`: acknowledged device sequence plus status START accepted=1, PCM received=2,
whole utterance received=3, native Codex queue accepted=4, failed/cancelled=5 or native
submission unconfirmed=6. An unconfirmed input is never automatically repeated.

START requires both a fresh authenticated CCP peer and that peer's current
discovery nonce. This prevents an old recorded START from reopening an utterance
after the bounded transient record expires. Retained pairing alone never admits
audio. A recent successful native initialization/thread-read probe for the followed
chat is also required: five-second refresh, ten-second maximum age. Each bounded
public RPC allows up to fifteen seconds; a stalled probe does not renew freshness.
This is reachability evidence, not model/audio capability evidence.
Later blocks remain bound to the original endpoint, credential, utterance
and discovery nonce; ordinary CCP challenge rotation does not rekey an active stream.

The device retains every PCM block until its valid acknowledgement. Its ten-second
ring survives temporary CCP liveness loss within the same ten-second ACK deadline
and fails explicitly on overflow. Reconnection requires the same authenticated
credential and IP. A transmitted END makes later transport failure Unconfirmed.
The four ciphertext slots use at most 4,352 component-owned bytes, not task stack.
The transport service cadence is five milliseconds, with a minimum of one RTOS tick.
The PC freezes the genuine followed chat at START. After END it writes the complete
WAV exclusively and calls the public experimental `thread/queue/add` once, using
that same chat and `clientUserMessageId` equal to the recording ID. A last-moment
chat switch or cancellation prevents admission. The stable ID is correlation,
not an assumption of server deduplication. A matching native receipt publishes
status 4; a lost or mismatched receipt publishes uncertainty without replay.

The queue-only standard stdio app-server permits initialization, `thread/read`,
`thread/queue/list` and `thread/queue/add` only. It never starts/resumes a model
session or answers a native approval. Readiness reuses one owned process rather
than spawning repeatedly. Its `notLoaded` metadata describes this client, not
Desktop's actual active state. The installed native executable is explicit;
there is no loopback endpoint, separate API credential or transcription service.

Status exposes the recording ID, real native queue ID, WAV path and uncertainty
without audio contents. END receipt is not queue acceptance; queue acceptance is
not Desktop consumption, transcription or task completion. The verified synthetic
trace on 15 October preserved the full WAV in chat, while Codex prompt preparation
replaced unsupported audio with the omission placeholder for the model. Physical
recording remains a separate gate. CANCEL cannot revoke queue admission already
in flight; its actual eventual result is returned rather than claiming rollback.
Metadata CCP packets remain authenticated
plaintext; this audio extension does not replace or strengthen the existing pairing
scheme. Cryptographic/wire/physical checks of this source increment remain pending.

## Authenticated device diagnostics, 16 October 2026

The Cody Companion profile offers one transient device diagnostic screen in
Release and Debug: `character` renders the live character with canonical snapshot
readout for measured CPU utilization, free RAM/PSRAM, FPS, IP and SD status.
`normal` restores the retained route. The facial-channel list was removed to save
device space; `face_channels` is rejected, without a substitute view. Internal
animation channels remain. Wire names do not select a firmware build mode.
No arbitrary command, channel override or settings write is admitted.

After an authenticated READY, Cody advertises `CCP1|DEBUGCAP` with ordered fields
`device`, `pc`, `nonce`, `debug=1`, `proof`. HMAC-SHA256 uses the retained link
key over `debug-cap|device|pc|nonce|1`. This denotes available screens, not Debug
build identity. The broker binds it to the current authenticated endpoint and
live READY nonce; saved pairing without this fresh capability is insufficient.

`CCP1|DEBUG` carries `device`, `pc`, `nonce`, `id`, `mode`, `scroll`, `proof`.
Its HMAC input is `debug|device|pc|nonce|id|mode|scroll`. The request ID is exactly
32 hexadecimal characters. The reserved scroll field must be exactly `0`.
Mode and all fields are validated before mailbox admission.
Both peers bind the operation to current credentials, endpoint and nonce, consume
that nonce once, and require a new authenticated capability before another command.

The four-second mailbox belongs to the existing CCP socket worker. The display
worker takes its immutable projection, validates lifecycle/display readiness and
native layout, and completes that exact request with its original error. Capability
and mailbox service are compiled for the Cody profile in both build modes.
Diagnostic consumers never acquire sensor values independently of the canonical
snapshot. Unmeasured or stale fields remain explicitly unavailable/stale.

`CCP1|DEBUGACK` carries `device`, `pc`, `nonce`, `id`, `mode`, `scroll`, `status`,
`error`, `count`, `proof`. Its HMAC input is
`debug-ack|device|pc|nonce|id|mode|scroll|status|error|count`.
Status is `applied` or `rejected`; error preserves signed `esp_err_t`, and count
is the actual compiled channel count. Missing/stale/retired requests and ambiguous
timeouts never establish success or authorize automatic replay. An authenticated
applied ACK proves worker application, not physical display pixels.
