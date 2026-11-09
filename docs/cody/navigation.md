# Cody navigation and recovery

[Dashboard and Options map](device-navigation-map-a4.pdf)

## Start and connect

The VibeWare floppy animation plays once after a cold boot or software reset.
The first device route starts with the saved network and pairing state.

| Current state | Device route |
| --- | --- |
| No saved Wi-Fi | Not connected with Tap to pair |
| Saved Wi-Fi, association or IP pending | Wi-Fi connecting with the animated icon and configured network name |
| Saved Wi-Fi, between attempts | Not connected with Previous Wi-Fi, the saved network name and Tap to pair |
| Wi-Fi ready, no retained pairing | Prepare the Codex pairing code |
| Valid retained pairing | Search for the remembered PC until authenticated presence arrives |
| Storage or service error | Show the original failure and an available recovery action |

**Tap to pair** opens **Wi-Fi setup**. **App download QR** opens the installation
screen. Its QR links to Google Play and the caption names **ESP BLE Provisioning**
for users of the App Store. **Show pairing QR** starts Bluetooth provisioning
from either page. Scan the QR on **Scan to pair** from inside the Espressif app.

On the **Wi-Fi offline** recovery screen, **Later** opens **Not connected**.
When Cody loses a working link, it tries the saved network again after about
one second. Further failures are retried about one minute apart. The animated
Wi-Fi icon returns for each attempt while the
setup screen is awake. **Set up Wi-Fi** opens **Wi-Fi setup** directly.

In Options, Connections opens the Codex and Wi-Fi icons with PC link and Join
network hints. The lateral Back chevron returns to Options and preserves an already
valid discovery or connection. Wi-Fi and authenticated Codex presence are
separate. Device shows Wi-Fi status, measured battery and the current chat
title when fresh. Statistics show a compact Codex status at the top. PC linked
means the device authenticated the bridge and is waiting for app presence.

An opt-in Cody Companion build with `-RelayHost <dns-name>` adds **Check relay**
to Connections. Supply the same deployed HTTPS hostname for build and flash.
Cody checks Wi-Fi and device time on a separate worker. It verifies the
certificate and hostname for both the HTTPS liveness request and the secure
WebSocket upgrade. **Relay reachable** reports a successful liveness response,
WebSocket frame and close handshake. Enrollment and event delivery remain pending.

## Manage the retained PC

Open BOOT, Options, Connections and Codex. The saved link is
reused when available. Replacing or forgetting it requires the corresponding
explicit confirmation.

| Retained state/action | Behavior |
| --- | --- |
| Valid saved record, including temporary LAN loss | Saved PC, with configured Wi-Fi identity shown separately |
| Keep | Reuse valid Connected/Discovering or resume retained-PC discovery |
| Re-pair | Explicitly authorize replacement, then require usable network |
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
Codex stats and Timer. Page 2 has Visible screens, Display and Behavior.
Page 3 has Restart device, Factory reset and Quick guide. The lateral arrows move between pages. Each page
offers **Home** to return to the first enabled statistics screen, or Device
if all are hidden.
The Tools, Preferences and Device pages show three position marks above the
**Home** footer. Each destination has a colored icon, title and short description.
Display holds brightness, rotation and orientation. Behavior holds sound,
shake-to-wake and, in the voice-enabled profile, Dictation. **More behavior**
contains **Screensaver**, disabled by default.

**Quick guide** has three color-coded pages for the physical buttons, display
gestures and offline features. Available arrows stay visible while reading.
The left arrow on the first page and **OK** on the last page return to Options
page 3. The voice instruction reflects the firmware profile.

**Timer** sets a duration from 1 to 120 minutes and selects Bell or Chime.
**Start** begins the countdown. Back returns to the previous screen while the
timer continues. Reopen Timer to cancel it. At expiry the device can wake from
software standby, and **OK** clears the finished timer.

**Restart device** opens a confirmation and reboots Cody with Wi-Fi credentials,
pairing, preferences and saved statistics intact. Any running local timer ends.

**Factory reset** opens a confirmation with **Cancel** and **Reset device**.
Confirming clears Wi-Fi credentials, Codex pairing, saved statistics and preferences.
Cody restarts into first setup after the platform verifies the reset and loads
the running firmware's defaults. The action is available offline.

The carousel order is Device, Codex reset, Context, Codex limits, Today, Last 7 days,
Token history, Credits, Codex account and About. The arrows move
between enabled pages. A shake returns to the first enabled statistics page.
With the default settings that is Codex reset. Device is the fallback if all
statistics pages are hidden. Shake preserves active pairing, voice capture
and open Codex questions.

**Last 7 Days** covers the seven calendar days before the current UTC date.
This is the Windows widget's complete-day cutoff in the time zones Cody
supports. The date range advances at UTC midnight. Missing daily counts appear
as gaps. Partial coverage shows how many of the seven dates have reported
counts, and the total requires all seven. A measured zero has a baseline mark.
CCP 0.6.13 or later can deliver the additional date needed when the service
already includes today's partial count.

**Today** uses the current UTC date. When Codex omits today's total, the page
identifies the latest earlier reported total by its actual date, if available.
**Token Activity** samples the account's reported lifetime token counter even
when today's daily bucket is absent. It falls back to today's counter only
when the lifetime counter is unavailable. The page labels the source ACCOUNT,
shows collection status until there is a measured point, and preserves gaps.
Provider updates can arrive late: the graph describes changes observed by the
plugin, not the exact time of each request. It does not backfill earlier hours.

**Codex account** shows the current ChatGPT plan with a standalone icon and
label. The public US monthly list price appears when available. CCP 0.6.15 or
later refreshes the public catalog on the computer and supplies the icon, label
and price through the authenticated link. **Workspace messages** appears only
when the current account observation contains a count, including a measured zero.

In **Credits**, **Reset Vouchers** lists the expiry of each available voucher in
the configured timezone. Cody numbers the rows, even when dates match, and
shows up to three at a time. Swipe up or down within the list when there is more
than one voucher. The plugin sends up to eight expiries, earliest first. The count
is independent of expiry details. A current expiry within 24 hours can open a
dismissible visual notice on the awake dashboard. Setup, questions and recording
take priority. Dates and notices use current observations from CCP 0.6.18 or later
and a valid device clock for the 24-hour check.

After ten seconds without touch, the lateral arrows fade. Touch or shake
reveals them. A hidden arrow requires a new touch once it is visible. Token
history switches among 1, 5 and 24 hours, using fifteen-minute observations
and identifying measurement gaps. The reset date switches between UTC and the
configured local offset every five seconds when local time is available. The
five-pixel gradient perimeter follows remaining weekly allowance across the
awake interface while Codex is connected and the weekly sample is current.
Only the optional globe screensaver hides this informational chrome.

About contains three Info pages followed by the existing project QR:

| Page | Content |
| --- | --- |
| System 1/3 | Measured CPU 1/core 0 and CPU 2/core 1, current uptime and last observed uptime from the previous boot when retained |
| Memory 2/3 | Internal RAM and PSRAM as separate allocatable heaps: used, free and largest free block |
| Firmware 3/3 | Firmware version, compilation date/time, build timezone and build host |
| Scan for more info | The project QR; Back returns to Firmware and there is no Next action |

The same lateral arrows move through these pages. Back from System returns to
the previous enabled widget. The child page belongs to the device model; the QR
is not a separate item in the top-level carousel. Invalid or stale measurements
retain their status and never display an invented percentage or empty bar.

When enabled, the screensaver replaces an idle dashboard after fifteen seconds
without interaction. It shows only a soft colored globe on black. It does not
reset dimming or standby timers. A touch dismisses it without activating the
underlying control; shake returns to the first enabled statistics page. Options,
pairing, the project QR, voice, questions and battery notices keep their own presentation.
The allowance perimeter returns with the dashboard after dismissal.

Visible screens groups the nine configurable screens into three pages,
including hidden entries. Tap the full row to change its visibility. The left
arrow on page 1 returns to Options. Device remains available. A visibility
change takes effect after confirmed storage. A failed save leaves the previous
policy in effect.

Double-tap open dashboard space or use Record voice on Device to start
dictation in a voice-enabled build. Dictation must be on and the paired LAN
link must be current. The optional wake-word build can also listen for Sophia.
The default build omits microphone acquisition and its audio front end.

When online, Cody asks its configured SNTP servers for time after Wi-Fi obtains
an IP. Once synchronized, the clock continues to run offline. A cold boot
without an available RTC and network reports time unavailable.

With the paired PC unavailable, Reset, Limits, Today, Last 7 Days and Credits
use saved observations and show their UTC update times. The Codex reset countdown uses Cody's
clock and the saved reset instant. Once the instant passes, a fresh Codex
observation is required to show the next reset and its allowance.

Battery notices appear while the screen is awake at 50%, 20%, 10% and 5%, with
one threshold-specific sound request under Sounds and quiet-hours policy.
An observed 0% shows a separate prompt to connect USB. If the PMU cuts power
before it reports 0%, the last visible warning is the one from a measured
threshold.

Question, reply-ready, answer-delivered and connection effects belong to the
device effects model, independently of the currently selected screen. Only
confirmed semantic outcomes request their corresponding sounds. The initial
connection observation is silent, and loss must last three seconds before its
single cue. Pending questions can remind after sixty awake seconds; ongoing
work and navigation remain silent. See [audio policy](runtime.md#device-first-notifications)
for the exact event mapping and capture/standby admission.

## Pairing progress and reconnection

Screen 12 shows a pulsing link and Creating pairing code until the actual code
is available. The code then appears above the instructions for the Cody plugin.
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
Cancel and explicit pending replacement follow [CCP1](../../cody-desktop/plugins/cody-codex-plugin/protocol/CCP1.md#pairing).

## Followed chat, questions and PR state

The dashboard shows Codex connected once authenticated app presence is
current. CCP follows the latest authorized `UserPromptSubmit`
with exact chat/turn identity. Background tool/completion events cannot replace
it. Opening a tab is not a focus event. Broker restart needs another real prompt;
LAN reconnection may restore the current snapshot. Project names are labels.
Packet state expires after eight seconds; active work becomes unavailable after
120 seconds without another hook. A genuine Stop can publish a Chat waiting
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
PC state belong to the [CCP guide](../../cody-desktop/plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).

Current gates are in [TODO.md](../../TODO.md).
