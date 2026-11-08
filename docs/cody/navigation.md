# Cody navigation and recovery

[Dashboard and Options map](device-navigation-map-a4.pdf)

## Start and connect

The VibeWare floppy animation plays once after a cold boot or software reset.
The first device route starts with the saved network and pairing state.

| Current state | Device route |
| --- | --- |
| No saved Wi-Fi | Not connected with Tap to pair |
| Saved Wi-Fi, association or IP pending | Connecting to Wi-Fi, then a recovery menu if the attempt does not finish |
| Wi-Fi ready, no retained pairing | Prepare the Codex pairing code |
| Valid retained pairing | Search for the remembered PC until authenticated presence arrives |
| Storage or service error | Show the original failure and an available recovery action |

**Tap to pair** opens **Wi-Fi setup**. **App download QR** opens the installation
screen. Its QR links to Google Play and the caption names **ESP BLE Provisioning**
for users of the App Store. **Show pairing QR** starts Bluetooth provisioning
from either page. Scan the QR on **Scan to pair** from inside the Espressif app.

On the **Wi-Fi offline** recovery screen, **Later** opens **Not connected**.
The saved network can still reconnect in the background. **Set up Wi-Fi** opens
**Wi-Fi setup** directly.

In Options, Connections opens the vertical Connect Codex and Set up Wi-Fi
entries. Back preserves an already valid discovery or connection. Wi-Fi and
authenticated Codex presence are separate. Device shows Wi-Fi status, measured
battery and the title of the currently followed chat when fresh. Statistics
show a compact Codex status at the top. PC linked
means the device authenticated the bridge and is waiting for app presence.

## Manage the retained PC

Open BOOT, Options, Connections and Connect Codex. The saved link is
reused when available. Replacing or forgetting it requires the corresponding
explicit confirmation.

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

## Dashboard and Options

PWR toggles software standby. BOOT opens Options. Page 1 has Connections,
Codex stats and Visible screens. The right arrow opens page 2 with Display,
Behavior and Factory reset. The left arrow returns to page 1. Both pages offer **Go home** to
return to the first enabled statistics screen, or Device if all are hidden.
The title sits above a grid of up to three 72-pixel-high rows. A house glyph
identifies the larger **Go home** footer.
Display holds brightness, rotation and orientation. Behavior holds sound,
shake-to-wake and, in the voice-enabled profile, Dictation.

**Factory reset** opens a confirmation with **Cancel** and **Reset device**.
Confirming clears the device's Wi-Fi credentials, Codex pairing and preferences.
Cody restarts into first setup after the platform verifies the reset and loads
the running firmware's defaults. The action is available offline.

The carousel order is Device, Codex reset, Codex limits, Today, Last 7 days,
Token history, Credits, Codex account, Context and About. The arrows move
between enabled pages. A shake returns to the first enabled statistics page.
With the default settings that is Codex reset. Device is the fallback if all
statistics pages are hidden. Shake preserves active pairing, voice capture
and open Codex questions.

**Last 7 Days** covers the seven calendar days before the current UTC date.
This is the Windows widget's complete-day cutoff in the time zones Cody
supports. The date range advances at UTC midnight. Missing daily counts appear
as gaps, and the total requires all seven counts. CCP 0.6.13 can deliver the
additional date needed when the service already includes today's partial count.

After ten seconds without touch, the lateral arrows fade. Touch or shake
reveals them. A hidden arrow requires a new touch once it is visible. Token
history switches among 1, 5 and 24 hours, using fifteen-minute observations
and identifying measurement gaps. The reset date switches between UTC and the
configured local offset every five seconds when local time is available. The
five-pixel gradient perimeter follows remaining weekly allowance across the
awake interface while Codex is connected and the weekly sample is current.

About shows current uptime, the last observed uptime from the previous boot
when retained, and the firmware version, build date and build host. Use the
right arrow for Scan for more info and the project QR. The left arrow returns
to About.

Visible screens browses all statistics pages, including hidden entries.
Device remains available. A visibility change takes effect after confirmed
storage. A failed save leaves the previous policy in effect.

Double-tap open dashboard space or use Record voice on Device to start
dictation in a voice-enabled build. Dictation must be on and the paired LAN
link must be current. The optional wake-word build can also listen for Sophia.
The default build omits microphone acquisition and its audio front end.

When online, Cody asks its configured SNTP servers for time after Wi-Fi obtains
an IP. Once synchronized, the clock continues to run offline. A cold boot
without an available RTC and network reports time unavailable.

Battery notices appear while the screen is awake at 50%, 20%, 10% and 5%.
An observed 0% shows a separate prompt to connect USB. If the PMU cuts power
before it reports 0%, the last visible warning is the one from a measured
threshold.

## Pairing progress and reconnection

Screen 12 shows Preparing secure code / Please wait / About 30 seconds until
the actual code is available. The estimate is fixed, not measured progress.
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

The dashboard shows Codex connected once authenticated app presence is
current. CCP follows the latest authorized `UserPromptSubmit`
with exact chat/turn identity. Background tool/completion events cannot replace
it. Opening a tab is not a focus event. Broker restart needs another real prompt;
LAN reconnection may restore the current snapshot. Project names are labels.
Packet state expires after eight seconds; active work becomes unavailable after
120 seconds without another hook. A genuine Stop can publish a Reply ready
notice for the followed chat or a background chat. If the paired LAN link
is active, the notice can wake the screen from software standby.

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

The guide and installation pages return to Not connected after 30 awake seconds
of inactivity. Once **Show pairing QR** starts a session, the pairing service owns
the five-minute deadline. The device remains awake through the session and its
cleanup window. **Cancel** still closes the attempt explicitly. A service timeout
shows the pairing error and its retry action. QR copying requires current
ReadyToScan and attempt ID;
ownership and DMA handoff are in [provisioning](../bsp/services/connectivity-pairing.md).

Short PWR/BOOT, screenshot and sleep behavior follow the power owner and
[runtime](runtime.md). HTTP remains a separately enabled service and never
exposes CCP secrets. Remote restart and factory reset require explicit current
intent and the exact confirmation string; syntax, ambiguous results and retained
PC state belong to the [CCP guide](../../plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).

Current gates are in [TODO.md](../../TODO.md).
