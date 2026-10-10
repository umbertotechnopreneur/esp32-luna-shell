# CCP - Cody's Codex Plugin

**CCP** connects Cody to Codex over the local LAN. The Windows Python service
uses local MCP for Codex and CCP1 for the device. Local Wi-Fi/IP is sufficient
for pairing; public Internet availability is a separate condition.

CCP 0.6.4 and its firmware must be updated together. Signed READY binds a fresh
broker challenge to the observed device endpoint. Cody validates the endpoint
and returns matching COMMITTED before the broker admits presence, events or
current activity/technical data. Earlier handshake formats are rejected.
Source changes do not install the plugin or flash the device.

The **Model & Context** panel includes a colored **Standard: Economy / Balanced /
Expensive / Unknown** badge. The portable Python catalog owner downloads the
official [Codex Standard credit table](https://learn.chatgpt.com/docs/pricing)
in the existing broker's background thread on its first start each local day.
It makes no API call and requires no API key. Hooks never download prices.
Rates, source, retrieval time and the daily attempt record stay in
`%LOCALAPPDATA%/CodyCodexPlugin/artifacts/pricing/codex-standard.json`.

The product's qualitative policy uses uncached input and output credits per
million tokens: Economy requires input <= 10 and output <= 50; Balanced requires
input <= 100 and output <= 500; other recognized rates are Expensive. These
thresholds describe a Standard reference, not OpenAI's model categories, account
balance, speed, included-plan allowance or the cost of a chat. Cached-input rates
are retained for source completeness and never used to guess chat expenditure.
No dollar amount, rate or token-cost calculation is sent to Cody.

Only explicitly mapped exact hook model IDs qualify. Missing/unsupported models,
expired observations, an old-day catalog, failed refresh/persistence, or invalid
cache produce Unknown. A failed download is attempted only once that day, even
across broker restarts; it preserves the last valid catalog without presenting
it as current. Midnight alone does not fetch: the badge expires and the next
broker start or newly attached MCP transport triggers the daily refresh.
Lease renewals and ordinary hooks never fetch. Corrupt cache is
reported and left intact for explicit repair. The normal activity/link still
works when this optional metadata fails. The existing credential/launch boundary
remains Windows-specific; the new pricing module uses only portable Python.

Recipient delivery and device closure remain distinct confirmations through
EVENTACK and FIND `events=2`. A question surviving broker restart is retired
before a new one is offered. See the [wire contract](protocol/CCP1.md) and
[Connection Atlas](../../docs/cody/cody-ccp-connection-atlas-v1.pdf).

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

An explicit MCP question needs no hook. The recipient owns the private ticket;
`context` is a caller-declared label, not inferred chat identity. Screen 26 opens
the question; screen 27 selects a card, shows its detail and offers **Send**.
Selection remains reversible before Send, and LAN loss retains the route/question.

The first UDP ACK means CCP recorded the choice. The recipient polls the private
ticket/capability, then confirms `answer_id` in a second poll. Only that confirmation
permits screen 31 **Reply sent**. The default 180 s TTL includes this final step;
expiry, cancellation and disconnection never invent a choice. A poll waits at most
15 s and cannot wake a suspended chat. Broker restart requires a new explicit request.

Only one question is open per device. Informational notices replace older notices
without displacing it. Check reports need complete current data for the exact PR
head, whole failure names, at most six failures and 48 characters per name; excess
content is rejected with a return to the PC. Screens 21-24 are read-only; 25 is retired.
A genuine `PermissionRequest` may show **Approval needed / On your PC**. Native
approval remains on the computer; private command arguments, logs and stack traces
are not sent to Cody. EVENT admits 1280 bytes; pairing retains 512.

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
