# Cody navigation and recovery

## Connection search and working presentation - 2026-10-12

Looking for PC uses a ten-minute screen standby interval. A current Wi-Fi link
with an acquired IP first requests the connected symbol, followed by a repeating
96-second discovery presentation: question mark, calm gaps, moving search lens,
worry, brief fear and recovery. This silent narrative does not assert Internet
reachability or failed pairing. Real canonical link transitions take priority;
hidden or sleeping views do not advance the narrative.

Working uses a three-minute screen standby interval. Project, Branch and Model
are explicit carousel labels; Model is orange and its price chip appears only
during Model. Display bands are Cheap, Reasonable and Expensive; missing price
metadata remains unavailable. These Standard catalog classes do not measure chat
spending or model quality. The activity timer is retained. Current native tool
names supplement absent progress descriptions; arguments and command bodies are
not transmitted.

Model & Context labels model, reasoning, context/window, free space and token
availability explicitly. Current Desktop hooks provide no authoritative usage
stream; unavailable counts remain unavailable. Session & Link includes canonical
PC IP and running firmware identity. The valid-battery wake prelude is described
in [the personality contract](personality-icons.md). Source changes remain
pending authorized compilation, simulator and physical validation.

This is the current source contract for device routes. Screen IDs come from the
atlases and remain independent of physical page numbers. Installation belongs
to the [CCP guide](../../plugins/cody-codex-plugin/README.md); authentication,
packet fields and budgets belong to [CCP1](../../plugins/cody-codex-plugin/protocol/CCP1.md).

## Choose the startup route

After branding and local checkpoints, read one canonical observation:

| Saved and observed state | Route |
| --- | --- |
| Wi-Fi profile genuinely absent | 04 / Welcome, then network setup 05-09 |
| Credentials present, association or IP unavailable | 42 / No connection: Network setup or Later |
| Wi-Fi/IP valid, Codex pairing absent | 40 / Connect Codex?: Yes or No |
| Retained pairing valid | 14 / Looking for PC, then 15 after authenticated READY |
| Read, type, CRC or storage failure | Visible original error; never substitute an absent profile |

LAN connectivity and Internet availability are separate observations. No returns
to Idle without starting CCP or persisting a refusal. A later boot may offer
the choice again. The Cody profile sets `CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=n`;
`y` is explicit development replay. Software standby retains RAM state; reset
and Deep-sleep reconstruct admission from saved data and current observations.

## First setup and return routes

| Screen/action | Result |
| --- | --- |
| 04, introduction retired | 05 / Let's pair |
| 05, Show QR | 06 / installation QR; no radio action |
| 06, Pair Cody admitted | 07 / Starting Bluetooth |
| 07, current attempt ReadyToScan | Show that session's QR inside 07 |
| 07, credentials/connection in progress | Remove QR and show real progress |
| 07, Cancel | Remove QR, await confirmed cleanup, return to 06 |
| 08, Retry | Resume the correct stage after successful cleanup |
| Wi-Fi/IP healthy and Online | Persist and read back `cody_firstboot=false` |
| Completed handoff, Debug | 39 / copied real data; a fresh tap continues |
| Completed handoff, Release | 09 / Online |
| 09, Continue | 10 / Make it yours |
| 10, Next; 36, Continue | Settings 36, then Skin 11 |
| 11, explicit choice and Done | Return to caller; first setup then evaluates the saved Codex link |
| 40, Yes with no pairing | 12 / code preparation |
| 12, confirmed two-sided handshake | 13 / You're all set |
| 13, Let's go | Immediately search on 14 or reuse a current authenticated presence on 15 |
| 15, authentic followed prompt | Show that chat's activity |

`06-bis` remains an authored ID; the runtime session QR is a state of screen 07.
The former ten-second Idle detour after Let's go is retired.
`CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX=n` skips Codex setup explicitly.
On 09, loss of Wi-Fi/IP replaces confirmation with No connection or UNAVAILABLE.
Wake cannot activate Continue. If only first-boot persistence fails after Online,
Retry repeats that write rather than starting another BLE session.

## Open Setup and manage the retained PC

Hold the visible character for **1.5 seconds** to open 41 / Setup. Movement
over 12 pixels, a real sample gap over 250 ms, orientation change or a power
transition cancels the candidate. After wake, begin with the first fresh visible
sample. The opening contact must end before another action is admitted.
Stable routes, Looking for PC and recoverable local CCP errors retain this entry;
active provisioning/cleanup keeps ownership until confirmed closure.

Setup places **Connect Codex** and **Network setup** side by side, with
**Options** below. Network setup reopens 04-09. Back restores the calling route
without restarting a valid discovery or connection.

| Retained state/action | Behavior |
| --- | --- |
| Valid saved record, including temporary LAN loss | PC remembered |
| Keep link | Reuse valid Connected/Discovering or resume retained-PC discovery |
| Pair again | Explicitly authorize replacement, then require usable network |
| Forget | Open a separate confirmation |
| Incomplete candidate | Pairing pending |
| Read/CRC/storage failure | Pairing error; explicit removal only |

Opening a dialog, Back or Retry does not authorize replacing a PC. Backend
requests retain the attempt identity and reject obsolete confirmation.
Forget pairing shows Go back / Forget and Only Codex is reset. It removes only
the Cody CCP active/pending records; Wi-Fi, preferences and logs remain.
Display Unpaired only after the actual deletion result. The PC's protected
record is not remotely revoked by this local action.

## Options and Skin

Options opens Make it yours. Done returns to Setup; Back from 10 returns to 41,
and Back from 36 returns to 10. Visiting preferences does not commission radios.

| Preference | Contract |
| --- | --- |
| Shake to wake | Persisted On / Off |
| Auto-rotate | Persisted On / Off |
| Sounds | Persisted master playback gate; Off preserves microphone/wake word |
| Orientation | 0 / 180 when Auto-rotate is Off; otherwise show Auto |
| Usage | Strict Desk / Away; see [runtime](runtime.md#desk-and-away) |

Saving is separate from admission. Apply a value only after successful write and
readback; retain real errors and retry the same request. Invalid persisted values
are never silently repaired. The settings executor owns flash operations on an
internal stack. Skin retains the old preview until the new request succeeds;
late results cannot reopen retired navigation.

Skin 11 has eight-row capacity, whole-row scrolling without inertia, and stable
IDs for Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic and Dreamwave.
First setup requires an explicit choice, including selecting the shown default.
`DeviceSnapshot.character_look_save` publishes request identity, Saving/Saved/
Failed, original error and timestamps. Coordinates are in [layout](layout.md).

## Pairing progress and reconnection

Screen 12 shows Preparing secure code / Please wait / About 30 seconds until
the actual code is available. The estimate is historical, not measured progress.
The six characters never enter logs, public snapshots, HTTP or datagrams.
The backend window is five minutes; this screen has a separate 180-second
standby override anchored once when the prepared code reaches presentation.
Real touch renews inactivity. Cancel awaits owner completion and retains the
previous active pairing; a cleanup failure remains visible.

Active and pending records remain separate on both peers. All set requires
confirmed persistence and the matching handshake; an ACK or journal alone is
insufficient. Retained-PC recovery requires authenticated current presence.
FIND normally runs every two seconds; Cody expires presence after eight seconds
and the PC reports its own ten-second threshold. Interrupted commissioning,
Cancel and explicit pending replacement follow [CCP1](../../plugins/cody-codex-plugin/protocol/CCP1.md#pairing).

## Followed chat, questions and PR state

Screen 15 shows Connected / Ready to team up and Start a chat in Codex until
authentic context arrives. CCP follows the latest authorized `UserPromptSubmit`
with exact chat/turn identity. Background tool/completion events cannot replace
it. Opening a tab is not a focus event. Broker restart needs another real prompt;
LAN reconnection may restore the current snapshot. Project names are labels.
Packet state expires after eight seconds; active work becomes unavailable after
120 seconds without another hook. Stop means response ready. SessionEnd shows
20B for five active seconds, then Idle.

| Route/action | Meaning |
| --- | --- |
| 26 / Codex needs you, Open | Open an explicit two- or three-choice question on 27 |
| 26 / From Codex | Informational notification; no response |
| 27, select a card | Reversible review, without sending |
| Review, Send | Admit that immutable choice and await delivery |
| UDP received | Broker recorded the reply; continue waiting |
| Authenticated recipient delivered | 31 / Reply sent, then restore previous context |
| Expired/cancelled/offline | Preserve the real outcome; never choose for the user |

The review page has one Send capsule; lateral Back returns to choices. Back from
choices returns to 26 with the question retained. Before admission, offline
disables Send; after admission it preserves Sending your reply / Reply reached
your PC / Confirming delivery. Only recipient acknowledgement permits Reply sent.
One open question retains priority over activity, notifications and battery.
Dismissal and the next question require confirmed device closure, independently
of recipient delivery. The 180-second TTL includes delivery acknowledgement.

Native PermissionRequest shows On your PC / Continue on your PC. Native
approvals stay in Codex. PR screens 21-24 remain read-only; screen 25 is retired.
Only complete current check lists for the same head support counts or pass state.
More than six failures or a name over 48 characters returns detail to the PC.

## Timeouts, network owner and maintenance

Local route timeouts are 30 awake seconds for 05/06 and 90 for 07. Pause does not
stop these route clocks; standby does. Real touch or changed canonical attempt
progress renews them. Animation, typewriter, IMU and unchanged snapshots do not.
Timeout retires the view/QR, returns to Welcome and finishes cleanup; late results
cannot restore the old route. Backend deadlines and screen standby overrides
remain separate. QR copying requires current ReadyToScan and attempt ID;
ownership and DMA handoff are in [provisioning](../bsp/services/connectivity-pairing.md).

Short PWR/BOOT, screenshot and sleep behavior follow the power owner and
[runtime](runtime.md). HTTP remains a separately enabled service and never
exposes CCP secrets. Remote restart and factory reset require explicit current
intent and the exact confirmation string; syntax, ambiguous results and retained
PC state belong to the [CCP guide](../../plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).

Current gates are in [TODO.md](../../TODO.md). Source, host and physical evidence
remain dated in [done.md](../../done.md). Historical atlas production notes are
preserved in Git checkpoint `9956446`; they do not describe current installation.
