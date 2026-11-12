# Cody navigation and recovery

[Dashboard and Options map](device-navigation-map-a4.pdf)

## Start and connect

The VibeWare floppy animation plays once after a cold boot or software reset.
Home uses the saved Counter mode when enabled; the current source defaults it
to On when no preference has been saved. It presents three pages for reset,
current context and model, with horizontal swipes between them. Missing or
incoherent current observations are shown as unavailable. When Counter mode is
off, Home shows Muse waking and the showcase changes pose every ten seconds
while visible. Tap the character to trigger its reaction and the optional Cody
touch sound. Status and navigation chrome hide after inactivity and
return on touch or measured IMU motion. Battery percentage display is prepared
as a renderer option; its settings UI is deferred.

Hold BOOT for two seconds to open Options, then choose **Connections** to set up
Wi-Fi or connect Codex. The Wi-Fi action offers a local network list and Bluetooth
provisioning with app download and pairing QR screens. Back returns from the list
to the Wi-Fi choices, then to Connections. The public BLE name is `AiGadget-` with a
boot-scoped random suffix shared by the radio and the pairing QR. A short BOOT
press returns to Home. PWR puts the device in standby or wakes it. Pending
setup, pairing and other owned operations retain their normal cleanup and
admission rules when changing screens.

The native **Codex** hub in Options opens **Chats**, **Active calls**,
**Events** and **Statistics**. Open a chat to see its turns, then open a turn
to see its calls. The lateral Back control returns to the parent view.
Pages use one canonical generation. If that generation changes, paging starts
again at the first page. Historical and stale facts are labeled separately
from current observations. Selecting a chat saves a local bookmark; its row
shows **Saved** after storage confirms it. The hub's **Saved** control opens it
when the ID matches a current paired-source record. This bookmark does not
change the chat followed by Codex. The selected chat briefly shows a pulsing
confirmation and requests one sound only after the saved result is visible.
The Turns page also offers **PC chat activity**. It opens the existing Current
turn view for the chat Codex actually follows on the PC, even if that differs
from Cody's saved chat. If its observation is unavailable the view says so;
Back returns to the originating Turns page. The entry control briefly pulses
and the opened view requests a navigation cue once.
The read-only Questions page offers **Open events** to reach the signed inbox;
its control briefly pulses on entry and a navigation sound is requested once
the Events page is visible. Back returns to Questions. The signed Events flow
continues to own question review and Send. Cody saves the exact unresolved
ticket and selected option before the first Send. On restart the saved
reference is historical and cannot send an answer. Only the current signed
exchange can confirm delivery or cancellation.

Connection status follows the saved network and pairing state.

| Current state | Device route |
| --- | --- |
| No saved Wi-Fi | Not connected with Tap to pair |
| Saved Wi-Fi, association or IP pending | Wi-Fi connecting with the animated icon and configured network name |
| Saved Wi-Fi, between attempts | Not connected with Previous Wi-Fi, the saved network name and Tap to pair |
| Wi-Fi ready, no retained pairing | Prepare the Codex pairing code |
| Valid retained pairing | Search for the remembered PC until authenticated presence arrives |
| Storage or service error | Show the original failure and an available recovery action |

**Tap to pair** opens **Wi-Fi setup**. **App download QR** opens the installation
screen. Swipe right on the QR screen to return to the guide, then choose **Show pairing QR**
to start Bluetooth provisioning. Scan the session QR from inside the Espressif
app. QR screens show a title and a large code with its complete quiet zone. A
tap reveals status and perimeter temporarily; a right swipe returns through the
existing session cleanup path.

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

Hold BOOT for two seconds, then open Options, Connections and Codex. The saved link is
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

PWR toggles software standby and wake. A short BOOT press returns to Home; a
two-second BOOT hold opens Options. Swipe down from the top status area to return
to Home; this leaves the rest of the screen available for vertical list scrolling.
Existing pairing and pending-operation exit rules still apply. Page 1 has Connections,
Codex stats and Timer. Page 2 has Visible screens, Display and Behavior.
Page 3 has Restart device, Factory reset and Quick guide, plus an About action.
Swipe left or right to move between these pages; Back from
the first page returns to the opening route. **Codex stats** opens the first enabled
statistics page with data, or Device when none is available.
Counter mode can be changed under **Behavior > More... > More...**;
its saved state controls the Home/dashboard presentation. The full statistics
carousel described below applies when Counter mode is off. The Tools, Preferences and Device pages show three position marks without a Home
footer. Each destination uses an icon, title and separator. Footer actions use
the Home gradient text, with a label appropriate to the current action.
Display holds brightness, rotation and orientation. **Display > More... >
Display & time** independently controls the assistant quota and battery arcs;
its **Time** page contains automatic network time synchronization and **Sync time**.
Auto time defaults to On and uses
`pool.ntp.org`, `time.cloudflare.com` and `time.windows.com` when Wi-Fi has an
IP address. Turning it Off stops network synchronization without changing the
configured time zone or other clock sources. Sync time requests the PC's current
UTC instant and local UTC offset through the authenticated connection. The
device stores and calculates time in UTC; the offset is used for local display
and is refreshed on synchronization rather than independently calculating
daylight-saving changes.
Both default to On, survive restart, and return to On after a factory reset.
The battery status and alerts remain available when its arc is hidden. Behavior holds sound,
shake-to-wake and, in the voice-enabled profile, Dictation. **More behavior**
contains **Screensaver**, disabled by default, and opens **Synth Voice**. The
Synth Voice page offers an Enable Synth Voice switch, off by default, and a
Test voice action that requests the English phrase `Hello from Cody`. The test
works independently of that switch and follows Sounds and quiet hours; its
status distinguishes admission, playback completion and failure. With the
switch on, Cody may speak a current question and its delivered selected answer.
ReplyReady currently carries a chat title rather than the assistant's response,
so it does not supply text for reading an assistant reply. Voice volume and
voice type controls remain unavailable; playback uses the saved Audio volume.
Without an explicit feature or board configuration, the Cody console selects
`tts-flash` and includes English flash speech. The explicit Cody `wake-word`
variant also includes English flash speech; `none` excludes both engines.

**Quick guide** has three color-coded pages for the physical buttons, display
gestures and offline features. Swipe left or right to change guide pages.
The button page explains the short BOOT press and two-second BOOT hold, along
with the PWR standby, wake and power-off actions.
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
Token history, Credits, Codex account and About. Horizontal swipes move between enabled
pages with data. Device stays available, and About follows its visibility setting.
A shake on the dashboard returns to the first eligible statistics page, or Device.
Context and Current turn show the followed chat's title. A long title scrolls
within the heading. Swipe left from Events to enter Current turn when observed
activity is available. When the feed and turn are both unavailable, Device is the
dashboard fallback.

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
label. The signed-in person's display name is outside the current CCP account
projection. The public US monthly list price appears when available. CCP 0.6.15 or
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

After ten seconds without touch, navigation chrome fades. Touch or shake
reveals it. A hidden Back control requires a new touch once it is visible. Token
history switches among 1, 5 and 24 hours, using fifteen-minute observations
and identifying measurement gaps. The reset date switches between UTC and the
configured local offset every five seconds when local time is available. The
five-pixel gradient perimeter follows remaining weekly allowance across the
awake interface while Codex is connected and the weekly sample is current.
Only the optional globe screensaver hides this informational chrome.

About is available from **Options > Device > About** and from the statistics
carousel. It contains three Info pages followed by VibeWare and the repository QR:

| Page | Content |
| --- | --- |
| System | Measured CPU use for both cores, current and last observed uptime, and used/total internal RAM and PSRAM |
| Memory | Internal RAM and PSRAM as separate allocatable heaps: used, free and largest free block |
| Firmware | Firmware version, compilation date/time, build timezone and build host |
| VibeWare | VibeWare logo and umbertogiacobbi.biz |
| Scan for more info | QR for the [GitHub repository](https://github.com/umbertotechnopreneur/esp32-luna-shell); a right swipe returns to VibeWare |

Horizontal swipes move through these pages, with marks indicating the current
position. Back from System returns to
the page that opened About. The child page belongs to the device model; the QR
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

The Audio page shows the current output and two canonical microphone meters.
Use the output minus/plus controls to change the saved speaker volume in
10% steps and the input controls to change shared microphone gain in 3 dB
steps. Settings are applied on the existing worker; unavailable readings,
pending changes and errors remain explicit. Driver-confirmed settings are
not physical register measurements. The page remains available when an input
is absent, and unavailable controls do not accept touch. Visual and physical
device verification of these controls remains pending.

The source retains a separate Codex dictation flow. It requires a supported
voice-input configuration, Dictation enabled and a current paired LAN link;
its manual triggers include a dashboard double tap and Record voice on Device.
The default build omits microphone acquisition. The current `wake-word` variant
enables Sophia for standby wake but does not enable the Codex voice-input owner.
Options and its child routes suppress new dictation. A BOOT hold waits for an active
voice session to end before opening Options.

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
| 27, select a numbered row | Reversible review, without sending |
| Review, Send | Admit that immutable choice and await delivery |
| UDP received | Broker recorded the reply; continue waiting |
| Authenticated recipient delivered | 31 / Reply sent, then restore previous context |
| Expired/cancelled/offline | Preserve the real outcome; never choose for the user |

The review page has one Send text action; lateral Back returns to choices. Back from
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

PWR, BOOT and sleep behavior follow the power owner and
[runtime](runtime.md). HTTP remains a separately enabled service and never
exposes CCP secrets. Remote restart and factory reset require explicit current
intent and the exact confirmation string; syntax, ambiguous results and retained
PC state belong to the [CCP guide](../../cody-desktop/plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).

Current gates are in [TODO.md](../../TODO.md).

## Native text entry

`CODY_text_entry` is a reusable CODY2D keypad for a 466 x 466 display. It is
compiled into the companion component and used by the local Wi-Fi password
route. The existing phone-based setup remains available.

The caller supplies a title, optional field hint, native font, byte limit and
whether input is secret. Drafts accept printable ASCII and are bounded to 64
bytes. Callers apply their own field-specific limits and validation. The editor
currently appends and deletes at the end; it does not move a cursor within text.
Long values show their trailing characters. Secret values start masked; Show and
Hide change only their presentation.

Character groups use multi-tap with a one-second window; a single-character key
appends immediately on every tap. Shift toggles letter
case. `123` opens digits; the first digit group cycles through 0 and 1. `#+=`
opens printable symbols, and `123` returns to digits. `ABC` returns to letters.
Space and Del edit the draft. Done emits a submission event; the caller decides
whether the value is valid and what to do with it. Back and cancellation belong to
the caller, and must not submit the value.

The input owner passes acquired, already-oriented contact samples to
`handle_text_entry_touch`. A key activates on release only if the gesture stays
within its original target and does not move more than 12 pixels. Rotation
changes cancel the gesture. Reset `TextEntryTouch` when the screen is hidden.
Use a scene with its ordinary title hidden, since this view renders its own
heading. Keep each prepared view and its title, hint and font alive and immutable
until the frame retires. Prepare a new frame after edits, then clear retired
views. On cancellation or final exit, clear the draft and every retired view;
the latter may contain a revealed value. Do not log or publish drafts or capture
them in screenshots or diagnostics.

The first tap on a Wi-Fi network highlights its row and shows its complete SSID,
security and BSSID in the button. If the caption exceeds the available width,
it scrolls in both directions with pauses at the ends. A second tap on that row
selects the network; choosing a different row starts its own preview. The
animation pauses when another surface covers the scene.
The local route selects the scanned access point by BSSID and security; persisting
that selection pins the profile to that AP and restricts roaming. Hidden and
non-printable/non-ASCII names are not supported by this local route.

The layout and interaction are implemented in source. Compilation, visual
inspection, touch comfort, rotation and physical-device behavior have not been
validated for this component.
