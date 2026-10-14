# CCP - Cody's Codex Plugin

**CCP** connects Cody to Codex over the local LAN. The Windows Python service
uses local MCP for Codex and CCP1 for the device. Local Wi-Fi/IP is sufficient
for pairing; public Internet availability is a separate condition.

CCP 0.6.8 adds semantic question routing and other-chat reply notices to followed-chat token metadata and the optional one-way audio channel with native readiness,
recording correlation and explicit uncertain-delivery handling. Source changes
do not activate it.
Its signed READY handshake retains the 0.6.4 endpoint-binding contract. Signed READY binds a fresh
broker challenge to the observed device endpoint. Cody validates the endpoint
and returns matching COMMITTED before the broker admits presence, events or
current activity/technical data. Earlier handshake formats are rejected.
Source changes do not install the plugin or flash the device.

The packaged stdio entrypoint uses `cwd: "."` and `./mcp/ccp_server.py`.
The native manager resolves that working directory to the installed plugin root;
hook-only environment variables must not be left literally in the MCP launch
directory or arguments. A real MCP transport renews its own ten-second lease.
Without it, a broker started by a hook exits after 60 seconds without another
hook; a running Codex window alone is not that transport. Inspect the native
resolved MCP configuration and startup result when diagnosing idle disconnects.

## One-way voice input preparation

The dedicated encrypted receiver uses UDP 42119, independently of CCP metadata
ports 42117/42118. It requires `mcp/requirements-audio.txt` in the plugin's Python
environment. A missing dependency, bind failure, missing followed chat or native
Codex rejection remains an explicit optional error, available through the
read-only `ccp_audio_status` tool. Audio failure does not fabricate a CCP disconnect.

Set `CODY_CODEX_AUDIO_ENDPOINT` to an explicit already-running **loopback** native
desktop/app-server WebSocket endpoint. When that host requires a capability token,
`CODY_CODEX_AUDIO_TOKEN_FILE` names its existing local token file; keys and tokens
never appear in prompts or logs. No endpoint is guessed and no second Codex host
or chat is launched. The installed CLI protocol export confirms `localAudio` on
`turn/start` and `turn/steer`; it does not prove that this desktop version exposes
an endpoint, that the account/model accepts audio or that approvals route to its UI.
Those are required integration checks before treating dictation as operational.

The native worker performs a read-only initialization/thread-read probe with a
five-second deadline, refreshed every five seconds. START requires a successful
probe for the followed chat no more than ten seconds old. `native_ready` reports
this reachability check, not proof of model audio support or transcription.
At authenticated START, bind the capture to the genuine followed chat.
After END, retain a private mono PCM16 16 kHz WAV under the runtime's
`artifacts/voice`. A changed followed chat rejects submission, rather than retargeting
it. `turn/steer` resolves the current hook-observed turn in that same chat at the
submission boundary and passes it as `expectedTurnId`. A concurrent turn change
can still cause an explicit native rejection; its WAV is retained. An idle chat
receives a new turn. Native requests for approval or user
input are never answered automatically. Ambiguous native submission is never retried.
Each submission carries the recording identity as `clientUserMessageId` for
correlation, without assuming server-side deduplication. Audio status includes
the recording ID, frozen chat, returned native turn, retained WAV path and uncertainty.

Four DATA packets may be in flight. The PC acknowledges the highest contiguous
sequence; gaps are discarded and recovered by byte-identical retries. The device
retains its ten-second unacknowledged ring through temporary CCP disconnections,
resuming only with the same authenticated credential and IP. After END has been
transmitted, transport failure is Unconfirmed rather than a claim of non-delivery.

The receiver bounds utterances to 60 seconds (1,920,000 PCM bytes), four active buffers/delivery jobs,
64 transient replay identities and 64 MiB/1,024 owned WAV artifacts. Full storage
rejects new delivery explicitly; recordings are not silently erased. PCM is cleared
from the transient record after file/native processing. An immutable file-write
copy may temporarily double one recording's host memory. No device SD/flash writes
are involved. PC receipt, native acceptance and task completion remain distinct.

Generation, installation, firmware build/size, wire tests, account/model access,
desktop approval routing and physical voice behavior are separate evidence.
The 0.6.7 package adds token metadata to the 0.6.6 audio preparation.
Deployment evidence belongs in [done.md](../../done.md).
Physical voice tests remain deferred; native audio requires an explicitly
configured endpoint and verified account/model access.

Home shows a **Cheap / Reasonable price / Expensive / Unavailable** price chip only
while the Model carousel item is visible. Cheap is green, Reasonable price orange,
Expensive red, and Unavailable gray. The elapsed timer remains independent.
A band-only update does not restart the carousel, title reveal or sound.
**Model & Context** shows **Reasonable price** or the other known bands as
**Std price: ...** and otherwise
shows **Price unavailable**, keeping compaction and context status separate.

The portable Python catalog owner downloads the
official [Codex Standard credit table](https://learn.chatgpt.com/docs/pricing)
in the existing broker's background thread on its first start each local day.
It makes no API call and requires no API key. Hooks never download prices.
Rates, source, retrieval time and the daily attempt record stay in
`%LOCALAPPDATA%/CodyCodexPlugin/artifacts/pricing/codex-standard.json`.

The product's qualitative policy uses uncached input and output credits per
million tokens: Cheap requires input <= 10 and output <= 50; Reasonable requires
input <= 100 and output <= 500; other recognized rates are Expensive. These
thresholds describe a Standard reference, not OpenAI's model categories, account
balance, speed, included-plan allowance or the cost of a chat. Cached-input rates
are retained for source completeness and never used to guess chat expenditure.
No dollar amount, rate or token-cost calculation is sent to Cody.

Only explicitly mapped exact observed model IDs qualify. Missing/unsupported models,
expired observations, an old-day catalog, failed refresh/persistence, or invalid
cache produce Unknown. A failed download is attempted only once that day, even
across broker restarts; it preserves the last valid catalog without presenting
it as current. Midnight alone does not fetch: the badge expires and the next
broker start or newly attached MCP transport triggers the daily refresh.
Lease renewals and ordinary hooks never fetch. Corrupt cache is
reported and left intact for explicit repair. The normal activity/link still
works when this optional metadata fails. The existing credential/launch boundary
remains Windows-specific; the new pricing module uses only portable Python.

## Followed-chat tokens and compaction

Home's connection panel uses the actual native chat title, without a `Chat`
prefix or an opaque ID. A metadata-only reader queries the exact followed hook
session and verifies its checkout in `state_5.sqlite`, opened read-only. The
current native contract selects `name` for paginated threads and `title` for
legacy threads; see the official [thread metadata owner](https://github.com/openai/codex/blob/main/codex-rs/state/src/runtime/threads.rs).
No transcript, preview or first-user-message field supplies a title. Successful
reads are cached for five seconds; only another successful read refreshes age.
The complete title must fit 1-255 printable ASCII characters. Unsupported text,
missing metadata and source errors clear the visible title instead of truncating,
transliterating or inferring it. FIND `activity=5` negotiates `title`, `titlestate`
and `titleage` under the `activity-v3` authenticated projection; a title remains
current for at most 45 seconds after source observation.

Genuine hooks select the chat and turn. The existing broker reads only allowlisted
metadata from that chat's local native session journal; the exact transcript locator
or a read-only state-index lookup must match the hook identity and checkout.
Conversation messages, prompts and tool output are not interpreted as telemetry.
No extra App Server, resumed chat or artificial activity hook is created.
Windows extended drive/UNC paths are normalized before resolved-path confinement.
The native envelope may include an ordinal. Known unrelated world-state,
inter-agent and token-usage-record bodies are skipped; only the native
`event_msg/token_count` observation supplies the displayed counters.

Native `token_count` supplies **Last call**, **Input**, **Output**, **Total** and an
explicit **Window** when present. Total is cumulative session usage, not current
window occupancy or a bill. **Free** is Window minus Last call from the same sample;
it is not a guarantee about the next request's usable budget. Counts remain exact,
including zero. Missing values stay unavailable; over-limit values are explicit.
The automatic compaction threshold remains unavailable without its own source.
Original source timestamps are preserved, independently of fresh network packets.
A current native model takes precedence; when it expires, a fresh genuine hook
may supply reported model metadata with its own original timestamp. That does
not refresh native reasoning effort. `ccp_connection_status` exposes activity
and technical fields from one coherent owner projection for each paired device.

Home cycles Project, Branch, Model and token pages. On a healthy PC connection or
a newly followed chat, an **Available commands** banner rotates **Change my
name**, **Set my timezone**, **Set quiet hours** and **Ask Cody a question**.
Ready for More keeps the first three rotating alongside the token carousel. These
are requests to write in Codex chat, not device buttons or terminal commands.

Genuine **PreCompact** takes priority with orange **Compacting**, the existing
character animation and its sound. **PostCompact** releases it only for the
matching observed lifecycle. Sound is admitted once per observed chat/turn/sequence,
after a complete visible frame. Pre-completion token samples remain unavailable
after PostCompact until a new native sample arrives. Lost, stale, interrupted and orphaned boundaries
remain explicit; neither a timer nor a network heartbeat proves completion.

Recipient delivery and device closure remain distinct confirmations through
EVENTACK and FIND `events=3`. A question surviving broker restart is retired
before a new one is offered. See the [wire contract](protocol/CCP1.md) and
[Communication Atlas](../../docs/cody/cody-ccp-connection-atlas-v1.pdf).

## Install and review hooks

1. Make `python` available to the desktop application's environment.
2. From the repository root, register the local catalog if absent:
   `codex plugin marketplace add .`.
3. In **Cody local plugins**, install/enable **CCP - Cody's Codex Plugin** and
   open a chat loading the updated package.
4. In a local Codex session, open `/hooks`, review and authorize CCP definitions.
   Installation and native hook trust are separate operations.
5. Send a real prompt in the chat to establish observed activity.

The canonical manifest is `.codex-plugin/plugin.json`. Native inspections of
Codex 0.161.0 and 0.162.0-alpha.17.2 found zero hooks when a portable root
`plugin.json` took precedence, and all eight with the canonical manifest alone.
Do not include that root manifest. The public inspection command is
`scripts/device.ps1 check -Check cody-hook-discovery -ToolArguments
@('--codex-executable', '<native Codex executable>', '--installed')`.
It inspects discovery without executing hooks or approving trust; execution
still follows [AGENTS.md](../../AGENTS.md). Restart an updated client and review
its discovered hooks in `/hooks`.

Use the documented [local plugin installation](https://developers.openai.com/plugins/build/plugins)
and [hook review](https://learn.chatgpt.com/docs/hooks) flows. Command hooks
require local orchestration. An empty session list calls for environment/trust
diagnosis, not invented activity or manual trust-record edits.
Codex launches `python -B <plugin-root>/mcp/ccp_server.py --stdio` from the installed
package. No OpenAI API key is required or Codex credential distributed over LAN.
For physical LAN use, allow Python private UDP traffic on ports 42117 and 42118.

## Pair or retain Cody

On Cody, open **Setup > Connect Codex**. For a first association, give Codex the
code visible on screen 12. Discovery requires exactly one cryptographically
matching offer. Saved state alone is not completed pairing or current presence.

For an existing association, **Keep pairing** resumes the link without a new
code. A missing PC, IP change or ordinary firmware update does not require
replacement. **Cancel** withdraws the new attempt while retaining Cody's previous
record. A new PC candidate remains explicitly pending; proof from an old key
cannot silently delete it. A persisted candidate may resume confirmation after
restart, and storage errors remain visible.

Windows user DPAPI protects `%LOCALAPPDATA%\CodyCodexPlugin\state.json`.
Before PAIR, CCP persists a separate candidate in `pending-pairings.json` and
replaces the active record only after authenticated ACTIVE or COMMITTED.
After Cancel/Forget, replacing a remaining PC candidate requires a new visible
code and explicit `replace_pending:true`. A timeout grants no such permission.
Status distinguishes `pairing_pending` and `retained_pairing`. Forget on Cody
does not revoke the independent PC record. The [wire contract](protocol/CCP1.md)
defines the two-store transaction and its recovery limits.

## Follow authentic activity

Cody follows the latest chat receiving a real user prompt. Tool, completion and
other-chat startup events do not steal that context. Opening a tab supplies no
documented focus signal; the plugin does not inspect windows or transcripts.
Before a prompt, screen 15 shows **Connected / Ready to team up** and
**Start a chat in Codex**, confirming the PC link without claiming active work.

Activity contains the work phase, chat identity and short project name. Chats
in one project retain different IDs. `Stop` means response ready, without proving
task success. Active work without updates becomes **UNAVAILABLE** after 120 s.
Broker restart waits for a new prompt; LAN reconnect returns the current snapshot.

One hidden broker serves the PC's chats. MCP clients renew independent leases;
closing one chat does not stop the others. Transport retries wait 1, 3, 10 and
at most 30 s, retaining errors until authenticated recovery. Ambiguous tool/pairing
results are not retried automatically. Identity/version mismatch requires explicit
action. The broker exits after 60 s without leases or hooks.

## Tools and result meaning

| Tool | Operation |
| --- | --- |
| `ccp_discover_devices` | Current offers; `include_simulator` defaults to false |
| `ccp_pair_device` | Pair the uniquely matching visible code; replacement needs explicit consent |
| `ccp_connection_status` | Identity, authenticated presence and latest broker snapshot |
| `ccp_list_sessions` | Observed metadata and `followed`; no manual session selector |
| `ccp_ask_question` | Self-contained question with two/three choices and private ticket/capability |
| `ccp_publish_notification` | Known fact or read-only PR snapshot with exact-head metadata |
| `ccp_poll_request` | Read a reply, then acknowledge its `answer_id` in a second poll |
| `ccp_cancel_request` | Retire the caller's ticket without choosing an option |
| `ccp_reset_device` | Explicit confirmed restart preserving settings |
| `ccp_factory_reset_device` | Distinct confirmed settings wipe returning to setup |

Results distinguish `LAN`, `SIMULATED` and `UNOBSERVED`; MCP status does not prove
which pixels appeared on physical Cody.

## Complete a question

With reviewed synchronous hooks, `UserPromptSubmit` returns the documented
`hookSpecificOutput.additionalContext` only when a retained physical peer has
recently advertised the authenticated events extension. This static instruction
asks the current agent to create semantic questions with `ccp_ask_question`,
preserve the complete original English choices, then poll and acknowledge its
own answer. The hook forwards no prompt, response or tool-argument contents.
An explicit user preference for PC-only questions takes precedence. See the
official [Codex hook contract](https://learn.chatgpt.com/docs/hooks).

An explicit MCP question needs no hook. The recipient owns the private ticket;
`context` is a caller-declared label, not inferred chat identity. Screen 26 opens
the question; screen 27 selects a card, shows its detail and offers **Send**.
Two choices are visible at a time; a third is reached by scrolling. Choice labels
allow 63 printable ASCII characters and details allow 255. Selection remains
reversible before Send, and LAN loss retains the route/question. Questions that
need free text or cannot fit without losing meaning remain on the PC.

The first UDP ACK means CCP recorded the choice. The recipient polls the private
ticket/capability, then confirms `answer_id` in a second poll. Only that confirmation
permits screen 31 **Reply sent**. The default 180 s TTL includes this final step;
expiry, cancellation and disconnection never invent a choice. A poll waits at most
15 s and cannot wake a suspended chat. Broker restart requires a new explicit request.

Several callers may queue questions, each retaining its private capability.
Cody browses the actual pending tickets without changing the followed PC chat;
a reply in flight or awaiting recipient acknowledgement cannot be displaced.
Informational notices never displace a question. A genuine `Stop` from another
chat queues the typed **Reply ready** event once per native turn, with a bounded
project label. Before delivery to an eligible physical Cody, the existing title
owner performs one read-only lookup of that exact chat and checkout in the native
index, without reading a transcript or changing the followed chat or its cache.
The complete current title is used when it fits 63 printable ASCII characters;
otherwise the body says **Open the other chat in Codex**. No GUID or guessed title
is displayed. This reports a ready response, never task success. A new prompt
in that chat retires its old native notices; activity in another chat does not.
Check reports need complete current data for the exact PR
head, whole failure names, at most six failures and 48 characters per name; excess
content is rejected with a return to the PC. Screens 21-24 are read-only; 25 is retired.
A genuine `PermissionRequest` may show **Approval needed / On your PC**. Native
approval remains on the computer; private command arguments, logs and stack traces
are not sent to Cody. EVENT admits 4096 bytes; pairing retains 512.

This integration creates future semantic questions through the supported MCP
tool; it does not import an already open native `request_user_input` request.
The current Desktop app-server uses stdio without a public listener, so there
is no verified public attach route to that process. A separate app-server would
not own its outstanding request. No second Codex, private UI automation, transcript
question scraping or fabricated native response is used. Native permissions and
existing native questions remain on the PC; see the official
[app-server transports](https://learn.chatgpt.com/docs/app-server).

## Explicit restart or settings reset

`ccp_reset_device` preserves Wi-Fi, pairing and preferences.
`ccp_factory_reset_device` deletes settings including Wi-Fi/pairing, while retaining
firmware, assets and SD. Both require the corresponding intent and exact nine-digit
string `043533477` in the current user message. Recognized literal input examples
are `resetta il device codice 043533477` and `formatta cody 043533477`.
These examples describe syntax; this document never authorizes an operation.

The code avoids ambiguity; the authenticated CCP link supplies security.
`accepted` proves admission only, without verifying later boot or wipe.
CCP sends once and waits at most five seconds: `not_sent` means no transmission,
`ambiguous` means transmission without valid confirmation. Do not retry uncertainty
automatically. After factory reset the PC may retain an absent old record;
setup and pairing require explicit steps.

## Explicit simulator connection

Open `.\scripts\device.ps1 simulator -Ccp` from the root, select **Connect Codex**
and read the code. Explicitly request **Cody Studio SIMULATED** in Codex, with
`include_simulator:true` for discovery/pairing. The real CCP1 exchange uses loopback
`127.0.0.1:42117` and separately protected
`artifacts/cody-simulator-state/device.json`, preserved by cleanup.
The panel exposes replacement, cancellation, forgetting and simulated LAN loss.
Synthetic data never enters firmware snapshots. See the
[simulator guide](../../simulator/windows/README.md).

## Security and evidence

Ingress, receive slices, discovery/PBKDF2 work and local control have finite
budgets; rejected traffic is summarized and broker logs rotate. Runtime tuning
needs separate evidence. Startup-only `runtime.log` still appends across launches;
its multi-process retention is deferred and UDP rejection traffic cannot grow it.
The six-character pairing transcript permits offline guessing, and HMAC payloads
remain readable. Standard PAKE/high-entropy QR and authenticated encryption stay
in [planned work](../../planned-todo.md). No confidentiality or compromised-PC
protection is claimed.

`mcp/ccp_windows.py` owns DPAPI, user paths, Winsock exclusivity and hidden startup.
Transport/broker preserve the stored-key format. Linux/macOS need explicit
credential/startup owners and remain deferred.

Authorized checks use the public console:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
.\scripts\device.ps1 check -Check cody-ccp-simulator
```

These use labelled synthetic fixtures. The simulator suite includes temporary
loopback UDP without LAN broadcast, personal state, windows or real chats.
Follow [AGENTS.md](../../AGENTS.md) before execution. Dated results live in
[done.md](../../done.md); pending checks/device evidence live in [TODO.md](../../TODO.md).
