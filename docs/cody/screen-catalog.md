# Retained screen catalog

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
| atlas-v1:05 / wifi-guide | Wi-Fi setup | ON + SC; provisioning | Not connected/Connections -> app QR or provisioning; Back to caller | Active; S |
| atlas-v1:06 / install-app | App download QR | ON + SC; installation QR | Guide -> app QR; tap returns to guide | Active; S |
| atlas-v1:06-bis / provisioning-preparation | Provisioning preparation | ON; current state of 07 | Start -> ready QR or error | Retained alias, not another route; S |
| atlas-v1:07 / provision | Scan to pair / Connecting | ON + SC; BLE session | Start provisioning -> result; Cancel waits for cleanup | Active; S |
| atlas-v1:08 / provisioning-error | Pairing failed | ON + SC; provisioning error | Failed stage -> Retry/cancel through owner | Active; S |
| atlas-v1:09 / wifi-result | You're online / Wi-Fi result | ON + SC; valid network/IP | Provisioning -> continue setup | Active; S |
| atlas-v1:10 / options | Make it yours / Options | ON + SC; settings | Hold BOOT 2 s -> Tools/Preferences/Device pages; short BOOT returns to Muse | Active; S; distinct from next-v1:10 |
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
| atlas-v1:28 / voice-capture | Preparing / Listening | UI + DV; voice owner | Record voice/double tap/wake -> Send or Cancel | Retained voice-input source; excluded from current wake-only variant; S |
| atlas-v1:29 / voice-delivery | Sending / delivery result | UI + DV; voice transport | End capture -> receipt/failure/unconfirmed -> retained caller | Retained voice-input source; excluded from current wake-only variant; S |
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
| next-v1:16 / muse-home | MU; UI `Layer::Mascot` | VibeWare or Home -> BOOT hold for Options; touch reaction | Active source; two-page swipe navigation remains phase 09 |
| next-v1:10 / tools | ON `OptionsPage::Menu` is reusable source | Proposed top-level peer of Muse Home | Planned exposure; current Tools is inside Options |
| ui-v1:branding | `VIBEWARE_boot_animation.c`, `assets/branding/VibeWare` | Cold boot/software restart -> Muse | Active source |
| ui-v1:options-preferences, options-device | ON `MenuMore`, `MenuMaintenance` | Options pages -> child setting or Home | Active source |
| ui-v1:display, behavior, behavior-more, synth-voice | ON corresponding `OptionsPage` | Settings owner readback -> caller | Active source; synth rows report unsupported where excluded |
| ui-v1:guide-buttons, guide-touch, guide-offline | ON `GuideButtons/Touch/Offline` | Device help -> previous/help page | Active source |
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
| ui-v1:visible-screens | `CODY_widget_visibility_view.cpp` | Options -> settings owner -> previous route | Active source |
| ui-v1:timer | `CODY_local_timer_view.cpp`, `CODY_local_timer.cpp` | Options -> timer; Close preserves active countdown | Active source |
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
| reference-v1:muse-wifi-keypad | External Muse settings/keypad sources below | Network SSID/password editor -> Wi-Fi owner | Reference only; no Cody native port yet |

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

## Voice preservation

`CODY_device_ui.cpp::activate` maps Record voice to
`VoiceInputTrigger::DeviceButton` and calls `LUNASHELL::requestVoiceInput`.
The same owner admits DoubleTap, WakeWord and StandbyWake. This is start/Send/
Cancel behavior; it does not establish hold-to-record/release-to-send semantics.
The default lightweight profile omits microphone acquisition. The current
`wake-word` variant enables Sophia for standby wake, but explicitly disables the
separate Codex voice-input owner. The capture route below describes that retained
owner when an explicitly supported configuration enables it; the wake-only variant
does not establish voice capture or delivery.

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
This reference stays external to Cody's CODY2D build; phase 13 owns its native
adaptation, credential lifetime and route assignment.

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
