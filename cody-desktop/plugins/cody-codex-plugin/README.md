# CCP - Cody's Codex Plugin

**CCP** connects Cody to Codex over the local LAN. The Windows/Linux Python service
uses local MCP for Codex and CCP1 for the device. Local Wi-Fi/IP is sufficient
for pairing; public Internet availability is a separate condition.

CCP **0.6.20** provides one local owner for Codex and the Windows/Linux companion
control panel. Codex activity and semantic questions retain their own identities.
The panel reads genuine native MCP presence and original activity freshness.
Its signed READY handshake retains the 0.6.4 endpoint-binding contract. Signed READY binds a fresh
broker challenge to the observed device endpoint. Cody validates the endpoint
and returns matching COMMITTED before the broker admits presence, events or
current activity/technical data. Earlier handshake formats are rejected.
The Cody Events view requires the authenticated inbox exchange in this plugin
version. Source changes do not install the plugin or flash the device.

The packaged stdio entrypoint uses `cwd: "."` and `./mcp/ccp_server.py`.
The native manager resolves that working directory to the installed plugin root;
hook-only environment variables must not be left literally in the MCP launch
directory or arguments. A real MCP transport renews its own ten-second lease.
Without it, a broker started by a hook exits after 60 seconds without another
hook; a running Codex window alone is not that transport. Inspect the native
resolved MCP configuration and startup result when diagnosing idle disconnects.
The companion panel can explicitly retain the same owner while the widget is
open or in its tray, including with Codex closed. That lifetime request is not
an MCP lease and cannot establish Codex presence. The existing widget guardian
remains responsible for its child processes; no OS startup service is installed.

## Local companion control

The native Windows/Linux panel calls `mcp/ccp_server.py --control <operation>`
with a bounded JSON object on stdin. Output is one `{ "result": ... }` or
`{ "error": { "code": ..., "message": ... } }` envelope. Secrets are never
command-line arguments or returned by status reads.

| Operation | Purpose |
| --- | --- |
| `bridge/status` | Read actual owner, authenticated devices and per-device provider state without starting it |
| `bridge/start` / `bridge/stop` | Explicitly retain or stop the owner; stop rejects while a native MCP client is live |
| `device/discover` / `device/pair` | Use existing discovery and the physical display's six-character code |
| `device/read-name` / `device/set-name` | Read or change the actual preferred name through the existing ACK flow |
| `device/read-clock` / `device/configure-clock` | Read or change supported timezone and quiet hours |
| `device/normal` | Restore the retained route through the existing authenticated display control |

`bridge/status` returns `bridge`, global `source`, and `devices`. Each device
also carries `provider_source`; source identity is `codex` or `local`. A genuine
open native MCP transport selects Codex even without an active chat. Activity
freshness remains independent; stale work becomes unavailable. Device status
additionally requires a retained authenticated connection without pending pairing.
A disconnected device reports unavailable instead of claiming what its physical
display currently shows. Polling and widget lifetime cannot create native presence.

Without Codex, the device keeps its local companion mode. The computer-side
control panel still needs the PC running. Existing name and clock operations use
the original authenticated acknowledgement flow; an applied ACK confirms settings
handling, not physical rendering.

Windows retains per-user DPAPI. Linux uses `secret-tool` and an unlocked Secret
Service, with private state under `$XDG_STATE_HOME/cody-companion` or
`~/.local/state/cody-companion`. Missing/locked keyrings fail explicitly; there is
no plaintext credential fallback. Automatic host timezone adoption remains
Windows-specific; Linux users can select a supported zone explicitly. Optional
audio uses the existing `cryptography` dependency; absence remains an explicit
operation failure.

Before introducing any new downloaded dependency archive or extracted package,
perform one targeted on-demand Windows Defender scan of that first import and
wait for a clean completion. Reuse the result only for unchanged staged inputs.
Do not retain scan reports or claim that existing imports were scanned here.

App presence now has an independent authenticated APPCAP/APPSTATE extension.
An actual MCP transport starts **Open**; normal EOF of the last live transport
produces **Closed**. A crashed bridge, expired lease or failed close remains
**Unavailable**. The broker retains at most 64 active transport leases, renews
through the existing ten-second worker and expires source evidence after
60 seconds. The device expires received presence after eight seconds without
an authenticated update. A prior ambiguous transport loss prevents a later
normal release from falsely proving every client closed during that broker's
lifetime. Chat switching and `SessionEnd` do not change this state.

This observes the MCP client connection, not the OS process or window: disabling
or reloading the plugin can also close that connection. Hook-only broker startup
does not imply Codex is Open. The read-only connection-status result exposes
`app_presence.source=mcp_transport`, source age and any original publication
failure. Source changes require a coordinated future plugin/firmware update;
they do not install or activate it. See the [presence wire contract](protocol/CCP1.md#authenticated-codex-client-presence).
The current version includes explicit normal-EOF reporting. An already-loaded
client's id-only release is explicitly treated as abnormal, with no Closed
claim. A real Codex reopen that starts the updated broker/client loads the new
EOF reporting; changing files on disk does not hot-reload a running broker.

## PC approval notices

A genuine followed-chat permission request displays `On your PC:`,
`Approval needed`, the complete originating chat title in cyan and one
`Continue on your PC` instruction. Long titles scroll without a project box or
`Chat:` prefix. The colorful computer's tick/cross are read-only artwork; Back
closes this notice and returns to Home. Native permission decisions stay on the PC.
Unavailable titles are explicit and never borrowed from a different chat.

Update the plugin and firmware together for the 255-byte approval-title body.
The source marketplace already points to this plugin directory. Use the supported
local plugin installation/refresh flow and restart Codex to load the updated
broker; source changes alone do not update a running process.

## Device diagnostics

One diagnostic entry with overview and Memory details is available in both build modes:
`character` shows the character with measured CPU utilization, free RAM/PSRAM,
FPS, IP and SD status from the canonical snapshot. Unmeasured or stale values
are labeled explicitly. `normal` restores the retained screen.
The facial-channel list and its buffer were removed to save device space;
internal animation channels remain. The reserved `scroll` field must be zero.
Tap **Memory** in the overview for internal RAM/PSRAM capacity, largest free block,
minimum free memory, free-block count and fragmentation index. Capacity bars are
not a map of block addresses. Back returns to the overview before restoring the
normal route. The canonical measurements already exist; no tracing is added.

Use `ccp_connection_status` to identify the intended connected physical Cody,
then `ccp_debug_screen` with its `device_id` and the requested mode. These are
transient display controls, with unchanged pairing, fresh signed capability and
request/acknowledgement checks. An applied ACK is a worker result, not proof of
physical pixels. Firmware without the screen capability needs a firmware update;
changing the plugin alone cannot enable an unavailable device capability.

## One-way WAV attachments

The encrypted receiver uses UDP 42119 independently of CCP metadata. Install
`mcp/requirements-audio.txt` in the plugin's Python environment. Missing
configuration/dependencies, stale followed chat, native rejection and uncertain
admission remain explicit in `ccp_audio_status`; audio errors do not fabricate a
CCP disconnect.

Opt in with `scripts/device.ps1 codex-audio -EnableAudioQueue`. Setup selects the
installed native Codex executable and verifies public queue access to the genuine
followed chat before saving `%LOCALAPPDATA%\CodyCodexPlugin\audio-config.json`.
The audio receiver re-reads this configuration during readiness checks and
probes the explicitly selected executable before admitting a new recording.
If the executable changes during a recording, the completed WAV stays on the PC
and is not submitted through the replacement executable.
There is no separate OpenAI API key, transcription service or model session.

The receiver owns one standard stdio app-server client, restricted to initialize,
thread metadata and public experimental queue operations. It never resumes or
starts a chat, steers a turn, overrides model modalities or answers approvals.
Cody shows the destination and the receiver checks its digest at START before
recording opens. The followed chat changes on a genuine prompt in Codex. Merely
opening another chat in Desktop leaves the selection unchanged. Update the
plugin and firmware together for this START contract.
At END the receiver retains the completed WAV and calls `thread/queue/add` once
with the capture's frozen chat and recording ID. A changed followed chat cancels admission;
ambiguous acknowledgement remains unconfirmed and is never automatically retried.
The process is reused for read-only readiness and retired with the audio owner.

**Queue acceptance is not transcription.** The 15 October synthetic trace proved
that all WAV bytes persisted in the same Desktop chat. The installed model catalog
advertised text/image; Codex replaced unsupported audio with an omission message
when building the model prompt. The attachment can therefore arrive while its
spoken contents remain unavailable to the model. This limitation is accepted for
file delivery; no transcript is invented. `ccp_audio_status` reports
`delivery_scope=queue_admission_only`, the real queue ID, and
`transcription_available=false`.

The owner declined separately billed APIs on 15 October. Future built-in voice
integration remains deferred until the existing Desktop host exposes a usable
public ingress. The public experimental queue may change with Codex versions;
setup/native errors remain visible instead of switching to a private integration.

## Followed-chat tokens and compaction

Home's connection panel uses the actual native chat title, without a `Chat`
prefix or an opaque ID. A metadata-only reader queries the exact followed hook
session in `state_5.sqlite`, opened read-only. The working directory is mutable
metadata, not chat identity. The
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
or a read-only state-index lookup must match the exact session and turn identity.
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
[Communication Atlas](../../../docs/cody/cody-ccp-connection-atlas-v1.pdf).


Native PC-approval attention is limited to one successfully queued notice per
originating chat turn. Repeated PermissionRequest hooks in that turn do not renew
its ticket, deadline or device route, including after Back or expiry. A new turn
can signal again. A new prompt retires approval notices from the previous
followed turn. Accepted native tool activity retires the originating chat's
approval notice. Both paths preserve caller-owned questions and other chats'
reply notices. A rejected queue admission remains observable and can retry on a
later genuine event.
Partial multi-device retries retain already admitted ticket identities/deadlines.
This is an advisory attention policy, not a statement that each native request is
still pending or was approved. Broker diagnostics record accepted permission
boundaries and queued/suppressed/retired outcomes using hashed chat/turn identities;
commands, tool inputs and chat text are never included in this trace.

## Account dashboard on Cody

On a paired physical Cody, open **Codex Usage** to view the account allowance
and reset countdown, service-reported daily tokens, credits, reset
vouchers and the current ChatGPT plan when available. CCP 0.6.18 also carries
the earliest reported expiry dates for available reset vouchers. Cody uses the existing
authenticated pairing. The plugin resolves the installed native Codex
executable from its search path and uses its existing ChatGPT sign-in. The
five-hour graph shows observed changes in the account's reported
`summary.lifetimeTokens` between adjacent 15-minute UTC sampling slots, using
the documented [token usage response](https://learn.chatgpt.com/docs/app-server#7-token-usage-chatgpt).
It can collect while today's daily bucket is absent. If the lifetime counter
is unavailable, the sampler can use today's reported daily total within the
same UTC date. It never combines counters or substitutes chat context tokens.
A counter/source change, decreasing counter or account change starts a new
baseline. Failed observations and skipped slots leave gaps; daily rollover
also leaves a gap when sampling a daily counter. Already published intervals
are immutable and history starts when monitoring starts, without backfilling.
The page labels these as ACCOUNT observations. The provider may update late,
so a point describes when the plugin observed a change, not the exact time
the underlying requests consumed tokens.

The existing CCP broker makes bounded reads through a private
`codex app-server` stdio child only when a native MCP lease is open and an
authenticated device asks for account data. It attempts at most one refresh per
minute and forwards the supported metrics and plan presentation. Closing Codex,
losing the link, an unsupported account field or an expired sample produces
the corresponding unavailable or stale state on Cody. Account reads use the
[public App Server methods](https://learn.chatgpt.com/docs/app-server#auth-endpoints).

**Codex account** shows the active plan's standalone icon, name and public US
monthly list price when available. The broker checks the official
[pricing document](https://learn.chatgpt.com/docs/pricing) on the PC and sends the
selected presentation over the authenticated LAN link. Cody can render new
icons and prices through the same protocol. Pro shows the published choices
when the account's price tier cannot be established. Standard Business uses
the monthly per-seat list price. Unpublished prices and missing counters are
omitted. These public prices can differ from the signed-in account's billing.

## Install on Linux

Use a signed-in local Codex client. Both the MCP server and the hooks run
`python`. Pairing uses `secret-tool` and an unlocked desktop Secret Service
keyring. On Ubuntu, install the prerequisites if they are missing.

```bash
sudo apt install python-is-python3 libsecret-tools
```

From the repository root, register its local catalog and install CCP as your
regular user.

```bash
codex plugin marketplace add .
codex plugin add cody-codex-plugin@cody-local
codex plugin list --marketplace cody-local
```

The list should show `cody-codex-plugin` as installed. The same catalog appears
as **Cody local plugins** in the Codex desktop Plugins Directory. Open a new
local chat to load CCP. If the running desktop app still does not show it,
reopen Codex and start a new local chat.
[Official OpenAI CLI documentation](https://learn.chatgpt.com/docs/cli/reference#codex-plugin)

In the new chat, open `/hooks`, review the CCP definitions and authorize them.
Send a real prompt in that chat so Cody has activity to follow.

To pair a physical Cody, put the PC and device on the same local network and
unlock the desktop keyring. On Cody, press **BOOT**, open **Options → Connections**
and select the Codex icon. Ask Codex to pair with the six-character code on
Cody's display. See [pairing and retained links](#pair-or-retain-cody).

## Install and review hooks

1. Make `python` available to the desktop application's environment.
2. From the repository root, register the local catalog if absent:
   `codex plugin marketplace add .`.
3. In **Cody local plugins**, install/enable **CCP - Cody's Codex Plugin** and
   open a chat loading the updated package.
4. In a local Codex session, open `/hooks`, review and authorize CCP definitions.
   Installation and native hook trust are separate operations.
5. Send a real prompt in the chat to establish observed activity.
6. For optional WAV attachments, enable the queue route with the same Python
   interpreter as MCP. Pairing and queue admission are independent of model audio
   understanding. Ordinary CCP works without audio.

```powershell
# Inspect the installed native executable without changing settings.
.\scripts\device.ps1 codex-audio

# Enable WAV attachments to the existing chat; no transcription service.
.\scripts\device.ps1 codex-audio -EnableAudioQueue -InstallAudioDependencies
```

Setup normally selects the native executable owned by the running Windows Desktop
app. `-CodexExecutable 'C:\absolute\codex.exe'` explicitly selects an installed
native CLI when automatic selection is ambiguous or on another platform. The
optional `-PythonExecutable` must match MCP; it is not the simulator environment.
Dependency installation runs only with `-InstallAudioDependencies`.

The read-only setup probe checks the exact hook-observed chat and public queue,
then checks that the followed chat did not change. It saves no API keys or tokens
and submits no audio. Run setup again after a Desktop update removes the
previously configured executable. Setup replaces that path after the new
executable passes the probe. Malformed settings and old endpoint configuration
require explicit review. An intentional
`CODY_CODEX_AUDIO_EXECUTABLE` environment override is supported; remove it before
saving installation settings. Obsolete endpoint/token environment settings fail
explicitly instead of activating a fallback.

Reload the installed CCP broker after updating source/configuration and send a
genuine prompt so that it follows this chat. `ccp_audio_status.native_ready`
confirms recent read-only access, not transcription, a spoken reply or physical
recording behavior. The existing device's **Sent to Codex** state acknowledges
native queue admission only. The WAV stays in private PC artifacts for inspection.

The canonical manifest is `.codex-plugin/plugin.json`. Native inspections of
Codex 0.161.0 and 0.162.0-alpha.17.2 found zero hooks when a portable root
`plugin.json` took precedence, and all eight with the canonical manifest alone.
Do not include that root manifest. The public inspection command is
`scripts/device.ps1 check -Check cody-hook-discovery -ToolArguments
@('--codex-executable', '<native Codex executable>', '--installed')`.
It inspects discovery without executing hooks or approving trust; execution
still follows [AGENTS.md](../../../AGENTS.md). Restart an updated client and review
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

Windows user DPAPI protects keys referenced by `%LOCALAPPDATA%\CodyCodexPlugin\state.json`;
Linux stores protected Secret Service references in the corresponding private state.
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

`mcp/ccp_tool_labels.py` owns the display-only English tool table. For example,
`mcp__codex_apps__github_fetch_file` becomes `Tool: GitHub: read file`, and
`functions.exec_command` becomes `Tool: Run command`. Unknown identities retain
their complete readable server/method spelling with underscores replaced by
spaces. An overlong label explicitly reports the display limit. Correlation IDs
and work phases do not change. The hook supplies the tool ID, not its catalog
title; a wrapper such as `functions.exec` is labeled as a tool script without
examining its arguments or inferring nested operations.

One hidden broker serves the PC's chats. MCP clients renew independent leases;
closing one chat does not stop the others. Transport retries wait 1, 3, 10 and
at most 30 s, retaining errors until authenticated recovery. Ambiguous tool/pairing
results are not retried automatically. Identity/version mismatch requires explicit
action. The broker exits after 60 s without leases or hooks unless the companion
panel has explicitly retained its lifetime. A bridge lifetime never changes the
native transport's Open/Closed/Unavailable evidence.

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
Informational notices never displace a question. A genuine `Stop` from the
followed chat or a background chat queues the typed **Chat waiting** event once per
native turn, with a bounded project label. An authenticated delivery can wake
Cody from connected software standby and play the existing cue when sounds are enabled. Before delivery to an eligible physical Cody, the existing title
owner performs one read-only lookup of that exact chat and checkout in the native
index, without reading a transcript or changing the followed chat or its cache.
The complete current title is used when it fits 63 printable ASCII characters;
otherwise the body says **Open Codex to read the reply**. No GUID or guessed title
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
[simulator guide](../../../cody-device/simulator/windows/README.md).

## Security and evidence

Ingress, receive slices, discovery/PBKDF2 work and local control have finite
budgets; rejected traffic is summarized and broker logs rotate. Runtime tuning
needs separate evidence. Startup-only `runtime.log` still appends across launches;
its multi-process retention is deferred and UDP rejection traffic cannot grow it.
The six-character pairing transcript permits offline guessing, and HMAC payloads
remain readable. Standard PAKE/high-entropy QR and authenticated encryption stay
in [planned work](../../../planned-todo.md). No confidentiality or compromised-PC
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
Follow [AGENTS.md](../../../AGENTS.md) before execution. Pending checks and physical
acceptance are tracked in [TODO.md](../../../TODO.md).
