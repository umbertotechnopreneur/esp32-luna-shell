<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Cody screen catalog

**VibeWare · Cody Companion**

**Author:** Umberto Giacobbi · **Created:** 2026-11-10 · **Updated:** 2026-11-15 · **Revision:** 1.2

This is the `CODY-NEXT-00` source inventory. Retain implementations in place,
including inactive presenters and versioned assets. This catalog is their reuse
index, not a second application or a claim that every illustrated route runs.
The recoverable baseline is Git commit `5a177578` on `feat/cody-vNext`.
Current navigation remains described in [navigation.md](navigation.md).

## Identity and evidence

Always qualify codes. `atlas-v1:10` is Make it yours / Options;
`next-v1:10` is the requested Tools destination. `atlas-v1:16` identifies the
older Home composition; `next-v1:16` identifies Muse Home. Neither changes
`SceneId::MakeItYours = 10` or `SceneId::CodexChoice = 16`. A bare integer is not
a catalog lookup key. Preserve `atlas-v1:28/29/30` for voice independently of
`scene-v1:28/29/30` (checks). New subpages use symbolic keys, not reused numbers.

Evidence labels below mean **S**: inspected source; **E**: editorial/design
artifact only. **Active** means reachable in source with its admission/profile
conditions, not observed on hardware. **Retained** means reusable source or
design whose old route is excluded, replaced or conditional. No hardware
acceptance was performed for this inventory. In particular, older atlas
generator prose can describe routes that the current lightweight build excludes.

## Presenter and asset index

Paths in the tables refer to these retained owners:

| Key | Source and dependencies |
| --- | --- |
| ON | [Onboarding](../../cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp), [event route identities](../../cody-device/components/CODY_companion/src/scenes/CODY_codex_event_presentation.hpp), canonical pairing/settings/events, QR service |
| SC | [Scene presenter](../../cody-device/components/CODY_companion/src/scenes/CODY_scene.cpp), [typed SceneId](../../cody-device/components/CODY_companion/include/CODY_scene.hpp), [authored scenes and committed fonts](../../cody-device/assets/cody/scenes/) |
| UI | [Device UI owner](../../cody-device/components/CODY_companion/src/runtime/CODY_device_ui.cpp), native CODY2D, retired-frame touch admission |
| DV | [Device view](../../cody-device/components/CODY_companion/src/scenes/CODY_device_view.cpp), [device model](../../cody-device/components/CODY_companion/src/runtime/CODY_device_model.cpp), DeviceSnapshotService |
| CP | [Codex panels](../../cody-device/components/CODY_companion/src/scenes/CODY_codex_panels.cpp), inbox/technical projections |
| MU | [Muse scene](../../cody-device/components/CODY_companion/src/scenes/CODY_pixel_demo.cpp), [adapter](../../cody-device/components/CODY_companion/src/rendering/CODY_pixel_avatar.cpp), [original C renderer](../../cody-device/components/CODY_companion/third_party/muse/muse_pixel.c) |
| AT | [Current atlas source](../../scripts/generators/generate_cody_current_docs.py), [earlier navigation illustrations](../../scripts/generators/generate_cody_navigation_atlas.py), retained atlas PDFs |

All native presenters share CODY2D clipping, fonts and completed-frame ownership.
Assets remain versioned build inputs; they are not temporary cleanup targets.

## Original atlas routes

The symbolic key after each namespace is permanent. Entry/return descriptions
identify the owning journey; exact current controls remain in navigation.md.

| Qualified original code / key | Title | Presenter / owner | Entry and return | State / evidence |
| --- | --- | --- | --- | --- |
| atlas-v1:01-03 / character-boot | Original character introduction | AT; retained character/rig sources | Boot concept -> Home | Retained, replaced by VibeWare/Muse; E |
| atlas-v1:04 / welcome | Welcome / Not connected | ON + SC; Wi-Fi/pairing | Setup state -> network setup | Active; S |
| atlas-v1:05 / wifi-guide | Wi-Fi setup | ON + SC; provisioning | Not connected/Connections -> character-led choice of Bluetooth guide or local network list; Back to caller | Active; S |
| atlas-v1:06 / install-app | App download QR | ON + SC; installation QR | Guide -> app QR; tap returns to guide | Active; S |
| atlas-v1:06-bis / provisioning-preparation | Provisioning preparation | ON; current state of 07 | Start -> ready QR or error | Retained alias, not another route; S |
| atlas-v1:07 / provision | Scan to pair / Connecting | ON + SC; BLE session | Start provisioning -> result; Cancel waits for cleanup | Active; S |
| atlas-v1:08 / provisioning-error | Pairing failed | ON + SC; provisioning error | Failed stage -> Retry/cancel through owner | Active; S |
| atlas-v1:09 / wifi-result | You're online / Wi-Fi result | ON + SC; valid network/IP | Provisioning -> continue setup | Active; S |
| atlas-v1:10 / options | Make it yours / Options | ON + SC; settings | BOOT tap opens the menu -> Settings; hold BOOT 2 s opens Options without voice input or requests voice input when included | Active; S; distinct from next-v1:10 |
| atlas-v1:11 / skin | Cody's look | ON + SC; legacy character settings | Legacy personalization -> caller | Retained; S; not a Muse skin picker |
| atlas-v1:12 / pair-pc | Pair Codex | ON + SC; CCP pairing transaction | Explicit connect/replace -> confirmed 13; Cancel retains old association | Active; S |
| atlas-v1:13 / pairing-complete | You're all set | ON + SC; committed pairing | Two-sided confirmation -> saved-PC discovery | Active; S |
| atlas-v1:14 / discover-pc | Finding your PC | ON + SC; CCP link | Retained association -> authenticated presence | Active; S |
| atlas-v1:15 / pc-connected | Connected | ON + SC; presence | Authentication -> activity/setup | Active; S; no inferred chat |
| atlas-v1:16 / companion-home | Earlier Home/idle | SC, legacy home dashboard/identity/technical presenters | Legacy activity route -> panels/setup | Retained; S; visible startup now MU |
| atlas-v1:17 / project | Let's get to work | ON + SC; activity | Followed prompt -> working | Retained activity presentation; S |
| atlas-v1:18 / working | Working | ON + SC; activity | Followed turn -> observed result | Retained activity presentation; S |
| atlas-v1:18U / activity-unavailable | Activity unavailable | ON + SC; source health | Unavailable observation -> fresh state | Retained activity presentation; S |
| atlas-v1:19 / reply-ready | Chat waiting | ON + SC; reply observation | Observed reply -> resumed work/caller | Retained; S; not success evidence |
| atlas-v1:20A / paused | Taking a pause | ON + SC; execution | Interrupt -> later observed activity | Retained; S |
| atlas-v1:20B / ended | Chat closed | ON + SC; lifecycle | Explicit close -> retained context | Retained; S |
| atlas-v1:21 / pr-pending | PR pending | ON + SC; event owner | Exact PR observation -> status | Conditional event route; S |
| atlas-v1:22 / checks-running | Checks running/passed | ON + SC; check evidence | Same PR head -> status | Conditional event route; S |
| atlas-v1:23 / checks-failed | Checks failed | ON + SC; check evidence | Failure -> detail 24 | Conditional event route; S |
| atlas-v1:24 / check-failures | Failure details | ON + SC; check names | View failures -> Back to 23 | Conditional event route; S |
| atlas-v1:25 / check-log | Former log route | AT | No current entry | Retired design; E; never reuse code |
| atlas-v1:26 / question-notice | Codex needs you | ON + SC; event/inbox owner | Question -> queue/choices; Back retains question | Active when admitted; S |
| atlas-v1:27 / question-choice | Choose / Review reply | ON + SC; stable choice identity | Choice -> full detail -> explicit Send; Back reverses selection | Active when admitted; S |
| atlas-v1:28 / voice-capture | Preparing / Listening | UI + DV; voice owner | Record voice/double tap/wake -> Send or Cancel | Included in Cody wake-word variant; physical validation pending; S |
| atlas-v1:29 / voice-delivery | Sending / delivery result | UI + DV; voice transport | End capture -> receipt/failure/unconfirmed -> retained caller | Included in Cody wake-word variant; physical validation pending; S |
| atlas-v1:30 / transcript-review | Review transcript | AT | Proposed transcription -> review | Retained design only; E |
| atlas-v1:31 / reply-delivered | Reply sent | ON + SC; recipient acknowledgement | Confirmed recipient -> Done/caller | Active when confirmed; S |
| atlas-v1:32 / link-lost | Link unavailable | ON + SC; link health | Lost link -> recovery; question identity retained | Active/conditional; S |
| atlas-v1:33 / battery-low | Battery low | DV; battery alerts | Eligible awake dashboard -> retained page | Active notice; S; old expressive art E |
| atlas-v1:34 / charging | Charging / Fully charged | DV; battery alerts | Measured transition -> retained page | Active notice; S; old expressive art E |
| atlas-v1:35A-35B / sleep-wake | Sleep / Wake | UI; platform power owner | Power intent -> retained route on wake | Source lifecycle S; old pose illustrations E |
| atlas-v1:36 / settings | Settings | ON + SC; settings | Options -> caller | Active settings subpages; S |
| atlas-v1:37 / rig | Rig preview | Retained rig/Studio sources, AT | Development preview -> caller | Retained, excluded from lightweight firmware; S/E |
| atlas-v1:38 / channels | Expression channels | Retained rig/Studio sources, AT | Development preview -> caller | Retained, no current facial-channel UI; S/E |
| atlas-v1:39 / provision-debug | Provisioning diagnostics | ON + SC; confirmed network data | Debug provisioning -> fresh tap -> 09 | Conditional build route; S |
| atlas-v1:40 / connect-choice | Connect Codex? | ON + SC; pairing owner | Explicit Yes -> 12; No -> local Home | Active; S |
| atlas-v1:41 / connections | Setup / Connections | ON + SC; network and pairing owners | Options -> Codex/Wi-Fi; Back to caller | Active; S |
| atlas-v1:42 / network-wait | Wi-Fi needed / Offline | ON + SC; network owner | Saved-network failure -> Retry/setup/Later | Active; S |

## Runtime and additional retained pages

| Stable key | Source/identity | Entry and return / data | Availability |
| --- | --- | --- | --- |
| next-v1:16 / muse-home | MU; UI `Layer::Mascot` | VibeWare or Home -> BOOT tap for menu; BOOT hold follows the voice-input build profile; touch reaction | Active source; two-page swipe navigation remains phase 09 |
| next-v1:10 / tools | ON `OptionsPage::Menu` is reusable source | Proposed top-level peer of Muse Home | Planned exposure; current Tools is inside Options |
| ui-v1:branding | `VIBEWARE_boot_animation.c`, `assets/branding/VibeWare` | Cold boot/software restart -> Muse | Active source |
| ui-v1:birth | UI `advance_birth_intro`, original Muse renderer and settings owner | First-boot birth introduction -> mascot/setup; host direct sample preserves the original renderer | Active source; boot-only entry requires explicit preparation |
| ui-v1:wifi-networks, wifi-scan-unavailable | ON `LocalWifiPage::Networks`, scan result | Wi-Fi setup -> network list or unavailable scan; Back returns to setup | Active source; synthetic host network observations are labelled in the simulator |
| ui-v1:options-preferences, options-device | ON `MenuMore`, `MenuMaintenance` | Options pages -> child setting or Home | Active source |
| ui-v1:display, behavior, behavior-more, synth-voice | ON corresponding `OptionsPage` | Settings owner readback -> caller | Active source; synth rows report unsupported where excluded |
| ui-v1:quiet-hours, quiet-hours-edit | ON `QuietHours`, `QuietEdit`; clock/settings owner | Behavior -> quiet-hours summary -> selected start/end editor; Back retains caller and draft context | Active source; summary and editor are distinct identities |
| ui-v1:guide-buttons, guide-touch, guide-sofia, guide-offline, guide-project, guide-thanks | ON `GuideButtons/Touch/Sofia/Offline/Project/Thanks` and `CODY_guide_presenter.cpp` | First-run tour or Device help -> adjacent guide page | Active source; Sofia requires the wake-word profile and first-run progress is independent of Wi-Fi first-boot completion |
| ui-v1:restart-confirm, restart-pending | ON restart pages | Explicit confirmation -> power owner, no reset of saved settings | Active source |
| ui-v1:reset-confirm, reset-pending, reset-failed | ON reset pages | Explicit confirmation -> reset owner or visible error | Active source |
| ui-v1:retained-pc, forget-pc, pairing-cleanup, pairing-forgotten | ON `CodexReplace/Forget/Cleanup/Forgotten` | Connections -> Keep/replace/forget; Back retains caller | Active source |
| ui-v1:question-queue, sending-reply, notice, approval-pc, event-unavailable | ON event routes | Inbox/reply lifecycle -> previous route | Active source; native approvals advisory only |
| widget-v1:Device=0 | DV; `WidgetScreen` | Statistics carousel/Options -> caller; network/battery/followed chat, Record voice | Active source |
| widget-v1:Reset=1 | `CODY_reset_countdown*.cpp` | Carousel -> neighbor; canonical allowance/clock | Active source |
| widget-v1:Limits=2, Today=3, LastSevenDays=4 | `CODY_account_dashboard.cpp` | Carousel -> neighbor; account/usage | Active source |
| widget-v1:TokenHistory=5 | `CODY_token_history.cpp` | Carousel + interval choice -> neighbor; history | Active source |
| widget-v1:Credits=6, Account=7 | `CODY_account_dashboard.cpp` | Carousel; voucher scroll/neighbor; account data | Active source |
| widget-v1:Context=8, About=9 | `CODY_home_dashboard.cpp`, `CODY_system_info.cpp` | Carousel or Options > Device > About -> child info/previous page | Active source |
| ui-v1:info-system, info-memory, info-firmware, vibeware, project-qr | `CODY_system_info.cpp`, `InfoPage` | About -> next/Back; diagnostics, build metadata, branding and repository QR | Active source |
| ui-v1:visible-screens | Retired | Pages are selected automatically according to available data | Retired; identity preserved |
| ui-v1:timer | Retired | Local countdown screen removed from Options and runtime | Retired; identity preserved |
| ui-v1:diagnostics, memory-detail | `CODY_debug_views.cpp`, UI diagnostics layer | Debug/Release entry -> overview -> retained route | Active source |
| ui-v1:screensaver | `CODY_screensaver.cpp` | Enabled idle policy -> first-touch dismissal | Active source, setting initially disabled |
| ui-v1:activity-events, current-turn, reply, statistics | CP; UI `ActivityPage` | Activity navigation -> event/detail/statistics | Active source, canonical selected-chat projection only |
| next-v1:101 / codex | Native Codex browser hub | Options -> Chats, Active calls, Questions, Events, Statistics or Saved when its bookmarked chat is available | Source |
| next-v1:102 / chats | Canonical chat page | Codex -> selected chat / Turns; Back -> Codex | Source |
| next-v1:103 / turns | Exact chat and its turns | Chats or Saved -> Tool calls or PC chat activity; Back -> caller | Source |
| next-v1:104 / tool-calls | Calls belonging to one exact turn | Turns -> Tool calls; Back -> Turns | Source |
| next-v1:105 / active-calls | Source active-call page | Codex -> Active calls; Back -> Codex | Source |
| next-v1:106 / pending-questions | Read-only canonical pending-question previews | Codex -> Questions -> Open events for signed question replies; Back returns to the originating page. | Source |
| legacy-v1:technical-panels, home-identity, character-direction | `CODY_technical_panels.cpp`, `CODY_home_identity.cpp`, `CODY_character_direction.cpp` | Retained behavior/presenters for selective reuse | S; not an instruction to restore the vector face |
| reference-v1:muse-wifi-keypad | External Muse settings/keypad sources below | Design reference for network entry | Reference only; current Cody local Wi-Fi pages use a separate native presenter |

`SceneId` is a third, independent namespace. The full `scene-v1` source map is
the retained enum plus `scenes/scenes.json`: 0 AnimationDemo; 1 Greeting;
2 PairInvitation; 3 InstallApp; 4 ScanToPair; 5 Pairing; 6 PairingFailed;
7 Online; 8 CodyLook; 9 DebugText; 10 MakeItYours; 11 Settings; 12 ConnectCodex;
13 SetupComplete; 14 LookingForPc; 15 Connected; 16 CodexChoice; 17 SetupMenu;
18 NetworkWait; 19 ProjectActive; 20 Working; 21 ResponseReady; 22 SessionPaused;
23 SessionEnded; 24 ConnectionLost; 25 CompanionIdle; 26 ActivityUnavailable;
27 PrPending; 28 ChecksRunning; 29 ChecksFailed; 30 CheckFailures;
31 DecisionNotice; 32 DecisionChoices; 33 DecisionConfirm; 34 DecisionWaiting;
35 ReplySent; 36 CodexNotice; 37 ApprovalOnPc; 38 EventUnavailable.
Those numbers are not atlas codes and must not be converted by array position.

## Studio catalog and inventory mapping

The [machine-readable Studio catalog](../../cody-device/simulator/windows/studio_catalog.json)
extends this source inventory with scenario, capture, dependency and transition
contracts. Studio and automated capture consume that same file. The existing
source tables remain the reuse index; do not maintain a second hand-written list
of Studio cases or infer inventory IDs from their row positions.

The reference **Cody screen map and static inventory**, dated 14 November 2026,
was located and copied without modification to
`artifacts/docs/studio/Cody-screen-inventory.pdf`. Its revision identity is the
stated working-tree baseline `e971b313` plus local changes, the document date
and SHA-256 `4db789f7facf03efa79f6f0efb97ab7e57412b0b73435e50a6c24e8a803b9c77`;
the document assigns no numeric revision. It enumerates 100 inventory IDs and
214 declared links, including state families and conditional entries. These
counts are a declared model, not acquired coverage or complete reachability.
The separate older native atlas indexes source identities rather than these
local inventory IDs.

The machine catalog owns the explicit pilot mappings. In particular P04 maps
to `ui-v1:display`, P12 denotes the **quiet-hours editor**, whereas P11 denotes
its summary. S15 denotes **Provisioning error**, and C04 denotes the
turn-scoped **Tool calls** browser. The latter's ordinary user-facing entry is
unconfirmed in the inventory and requires explicit host context. Do not infer
these IDs from enum numbers, screen names or table positions.

Catalog availability describes the declared rendering path: full runtime,
isolated presenter, profile conditional, unsupported or not applicable. It is
separate from acquisition, pixel comparison and human review status. All ten
pilot cases have executed Linux exact-pixel repeatability and Tk acceptance:
nine full-runtime cases and C04 through the production C++ presenter with typed
synthetic queries. C04 does not establish transport/reducer or general
pagination acceptance. S01/S02 inspect explicit source-animation samples;
they do not replay asynchronous startup workers. The suite also checks
out-of-order preparation, error recovery and isolation from an interactive
session. Source presence alone does not extend that evidence to new cases or
physical devices. Clock and worker limits remain attached to each capture.

## Current conditional screens and source-only states

The rows below use existing source symbols and descriptive names, not new screen
IDs. They inventory the working tree; source presence is not a build, flash,
visual review, or physical-device acceptance. Existing atlas IDs above continue
to identify their original designs even when current entry or presentation has
changed. See [navigation.md](navigation.md) for the user journey and
[notifications.md](notifications.md) for the notification contract.

| Source owner / state | Current source presentation and condition | Evidence and limitations |
| --- | --- | --- |
| ON `start_device_setup`, `OptionsPage::GuideButtons/GuideTouch/GuideSofia/GuideOffline/GuideProject/GuideThanks` | Six catalogued guide pages cover buttons, touch, Sofia, offline, project QR and thanks. Initial setup can enter the character-led Wi-Fi choice first; the saved tour step is a separate setting. | S; on first run Project hands off to the optional Codex choice instead of always displaying the project QR; Sofia is skipped in both first-run and manual guide replay in builds without wake word. Layout and resume behavior need physical verification. |
| ON `Route::Invitation05`, `draw_wifi_menu` | Character-led Wi-Fi choice offers Bluetooth provisioning and on-device Wi-Fi. Connection recovery and setup return through their owning routes. | S; BLE is also an explicit direct Home action and has its own route. QR instructions are child pages, not a replacement for the choice. |
| ON `LocalWifiPage::Networks/Password/ConfirmOpen/Connecting` | Network scan/list, native credential entry, open-network confirmation and connecting states. | S; input validation can show `Invalid WPA password`; connection result may be `Connection not confirmed`, which is not proof of an incorrect password. |
| ON provisioning result and `Route::Failed08` | Bluetooth pairing can show `Pairing timed out`, `Network not found` or `Wi-Fi login failed` when the corresponding observation is available; cleanup failures retain their own error. | S; do not infer an authentication failure from a generic timeout or advertise an action absent from the current page. |
| ON `OptionsPage::Time` | `Sync time` and observed states include `Connect Wi-Fi`, `Syncing...`, `Synchronized`, `Timed out`, `Sync failed`, `Auto time failed` and unavailable/unsupported cases. | S; enabled only with the required current network observation and no pending command. Manual and automatic outcomes remain distinct; no NTP success is implied by a network connection. |
| ON settings and `CODY_guide_presenter.cpp` | Settings-write errors show `Save failed`; tour progress can show `Save failed. Try again` or `Save failed. Swipe again`. `RestartConfirm/RestartPending` and `ResetConfirm/ResetPending/ResetFailed` represent explicit maintenance. | S; do not represent save, restart, or reset as successful when the owner reports failure. Recovery cannot assume an intact or erased settings store. |
| `CODY_status_overlay.hpp` | Transient status includes `Settings not saved: retrying` and a separate boot-sound settings failure. | S; these are overlays, not full-screen startup failure recovery. A failed display or mandatory startup owner can prevent any recovery screen from rendering. |
| UI `Layer::Voice`, DV voice presentation | Preparing, listening, sending and delivery/failure states; `Voice unavailable` and transport-specific `Audio link failed` are separate conditions. | S; build profile and supported PC connection constrain availability. Unavailable voice does not identify a failed microphone or prove delivery. |
| UI `Layer::Events`, ON local/remote notification detail, `CODY_notifications` | Merged notification list and detail, exact-source read actions, and unavailable/removed detail states. | S; local history is RAM-only and bounded to 32. Provider content is limited to what the source actually publishes. |
| UI `Layer::EventCody` and `CODY_device_effects` | Eligible remote event presentation retains animated Cody behind the notice; information, warning and critical severity select different reactions. | S; display priority, admission and actual event type constrain when it appears. Animation, text and cue timing need physical verification. |
| UI `Layer::Diagnostics`, `CODY_debug_views.cpp` | Shared diagnostics overview and memory detail use observed runtime data; authenticated Normal restores the retained route. | S; available values depend on acquisition support and freshness; source existence is not a measurement on the current device. |
| `CODY_codex_browser.cpp` `Route::Hub/Chats/Turns/Calls/ActiveCalls/PendingQuestions` | Codex hub, chat/turn/tool call navigation and read-only pending-question previews. | S; observations can be unknown, stale, unsupported or failed. Signed question replies use the separate event flow. |

In builds without wake word, first-run navigation and manual guide replay skip
the Sofia page in both directions. The presenter also supplies neutral fallback
text if that page is prepared. This is source evidence, not physical validation.

## Shared dark failure presentation

The native source now applies the live Muse error pose beyond first-run setup:

| State | Character and recovery |
| --- | --- |
| Provisioning and Codex retry | Animated error pose on first run and later recovery; existing cleanup and retry admission retained. |
| Local Wi-Fi scan/connection failure | Dedicated failure view, including when retained results exist; Show networks and a fresh scan remain distinct actions. |
| Settings save failure | Could not save, original error, existing Retry and parent navigation. |
| Restart/reset failure | Animated error with existing explicit confirmation/retry semantics; settings are never silently reset. |
| NTP/manual or automatic time failure | Time sync failed, observed status and error; Sync time is available only with current connectivity. |
| Synthesis failure/unavailable | Voice unavailable with owner error and admitted voice test retry. |
| Voice capture/delivery failure | Animated error replacing the static microphone; original cause and automatic return timing preserved. |
| Voice entry unavailable | Animated character with settings/connection guidance; no invented microphone diagnosis. |
| QR/statistics unavailable | Shared dark message, animated error character, existing navigation. |
| Power-off failure | Cody needs help with the actual power-off failure; existing device menu remains the recovery route. |
| Generic local operation failure | Something went wrong retains the original error and owner-gated recovery action. |

Particles remain limited to actual waiting states and startup. Error entry requests
one existing sound cue, respecting sound and quiet-hour settings. These are source
changes, not device acceptance. Fatal display/startup failures still propagate to
the runtime owner; they cannot guarantee a drawable recovery page. Code mismatch
is shown only when the receiving owner actually exposes that cause. Dense input,
QR and diagnostic views keep their functional geometry rather than placing a
character over controls or measured data.

## Priorities for visual and interaction consistency

1. **First-run and Wi-Fi:** unify title, mascot, Back and action placement across
   the character-led choice, both connection paths, their errors and the saved
   tour; retain QR readability and each route's real cleanup and return behavior.
2. **Errors and recovery:** share clear severity, factual cause and available
   action patterns across provisioning, local network, save, time, voice and
   maintenance states; do not invent a universal recovery screen or reset action.
3. **Notifications and Cody:** align semantic colors and notice/detail hierarchy
   while preserving event identity, severity, animated character and independent
   audio/read policies. Validate contrast and visibility on the round device.
4. **Settings and diagnostics:** use consistent rows, status wording and Back;
   distinguish pending, unavailable, stale and failed observations, including
   manual versus automatic time synchronization.
5. **Codex and retained artwork:** align navigation and text density with current
   patterns without treating atlas illustrations, planned routes or old Home
   compositions as proof of a reachable current screen.

These are proposed priorities, not claims of completed design work or device
acceptance. A physical pass must separately verify layout, touch, animation and
legibility in each supported profile.

## Voice preservation

`CODY_device_ui.cpp::activate` maps Record voice to
`VoiceInputTrigger::DeviceButton` and calls `LUNASHELL::requestVoiceInput`.
The same owner admits DoubleTap, WakeWord and StandbyWake. This is start/Send/
Cancel behavior; it does not establish hold-to-record/release-to-send semantics.
The default lightweight profile omits microphone acquisition. The Cody
`wake-word` variant enables Sophia, speech synthesis and the Codex voice-input
owner. A supported PC connection is required, and physical capture and delivery
remain to be verified.

`LUNASHELL_voice_input.cpp` serializes capture with `s_mutex`, obtains admission
and destination through DeviceSnapshotService, freezes the chat digest/title,
and begins countdown only after the Preparing frame showing that destination
retires. Send/cancel use the presented session's `started_at_us`, so a stale
button cannot act on another recording. Preparing, Listening, Sending,
Delivered, Cancelled, Unconfirmed and Failed retain distinct presentation.
Receipt is not transcription or proof that Codex consumed speech.
Selection in the new domain must not retarget this owner.

## Muse Wi-Fi reference

Retain `references/muse-gadget-sdk/esp32/components/muse/` under the outer Cody
workspace (`C:/Users/Umberto/Repos/Cody/references/` in this environment).
`muse_settings_ui.c::build_text_page/build_keyboard/on_wifi_pass` and
`muse_keypad.c::muse_keypad_create` are the precise reference entry points.
They use LVGL. Below 2 inches or on a narrow panel, large grouped keys replace
the full keyboard. Round layouts inset the bottom row and adjust title, field
and Back placement. The keypad uses 1,000 ms multi-tap, 600 ms hold-for-digit,
letter/digit/symbol modes, shift, space, backspace, password Show/Hide and Done.
This reference stays external to Cody's CODY2D build. The current Cody source
implements its own native local Wi-Fi presenter and text entry; do not assume it
is an LVGL port or that it has every behavior described by this reference.

## Ownership, lifetimes and portability inputs

| Owner | Current boundary / extension rule |
| --- | --- |
| PC ingress | Plugin hooks -> `mcp/ccp_activity.py::ActivityStore`; `_lock` serializes lifecycle/selection, `_usage_lock` serializes metadata I/O outside that lock. 64 sessions and 64 tools per session are current admission bounds. ACTIVITY/TECH still project the followed chat. |
| Authenticated device ingress | `LUNASHELL_codex_link.cpp` serializes link/events under `s_mutex`, publishes canonical observations; settings I/O is kept outside the socket-state lock. Future domain input enters here, not via a UI network client. |
| Canonical observations | `DeviceSnapshotService` bounded read methods return original lock/publication errors. Acquisition owners alone read hardware. Extend bounded projections instead of copying a new registry into every snapshot. |
| Future domain mutations | One serialized store owner in phase 05; no competing UI copy. The domain registry does not exist in the baseline. |
| Display/touch | UI worker owns `DeviceUi` in external-RAM storage, strips and retired mapping. `SceneContent` borrows immutable state through complete frame retirement. Touch acts on that mapping, and invalidation cancels old touch ownership. |
| Muse | Two 64 x 64 indexed frames plus palettes total 8,448 bytes. The 1.75C viewport is 320 x 320 at (73,73). The 40 ms frame target awaits on-device measurement. Muse callbacks do not submit DMA or acquire hardware. |
| Settings/persistence | Existing settings, pairing and voice owners retain serialized storage/readback. Domain persistence will have a separate bounded adapter; UI does not read NVS or files. |
| Audio | `CODY_device_effects.cpp` uses the existing audio owner; voice capture has its own admitted session and buffer lifetime. Quiet hours are configured through the current settings owner; actual cue admission follows the clock and audio policies. |
| Desktop OS | `ccp_runtime.py` has OS-specific credential filenames; `ccp_windows.py` contains Windows DPAPI, Linux Secret Service and process launch paths. These are candidates for explicit platform adapters, not domain imports. |
| Device platform | ESP error/log/clock types, PSRAM placement, BSP display/touch, codec and storage belong to adapters. Current 466 x 466 round layout and microphone count must not enter domain records. |
| Host | Native bridge sources live in `simulator/native`; Python preview/compiler owners live in `simulator/windows` and `scripts/common/cody_host_compiler.py`. Compiler discovery/shared-library flags are platform concerns. No simulator was started. |

The desired top-level two-dot Muse/Tools switch is not implemented. Existing
Options has three page markers and its own horizontal gesture recognition;
dashboard activity has a separate swipe path. Phase 09 must resolve ownership
against completed frames before reusing these gestures.

Font sources: Muse's `montserrat.ttf` and `unscii.ttf` are retained in
`assets/cody/muse-fonts`; dashboard assets document their Telex source. The scene
font README explicitly says the full JetBrains Mono source is not packaged.
Retain committed generated fonts without assuming that a full rebake is possible.
No assets or PDFs are regenerated by this catalog.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
