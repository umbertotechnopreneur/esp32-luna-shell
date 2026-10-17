# Completed implementation and evidence

This is the compact dated record. [TODO.md](TODO.md) owns unresolved gates;
[planned-todo.md](planned-todo.md) owns deferred work. Guides describe contracts,
not an implicit claim of executed tests or current-device behavior.

Historical detail, original dates, image hashes, failed attempts and capture
filenames are preserved in Git `9956446` (the source-preservation checkpoint
before Markdown consolidation). Read `git show 9956446:done.md` for the complete
prior record and `git show 9956446:TODO.md` for its original open/checked context.
Original guide content is preserved at the same commit. Older ignored artifacts
were removed by the authorized 2026-10-05 cleanup or later build cleanup; a recorded
path below does not promise the disposable file still exists. Core dumps are
private local diagnostics, not publication artifacts.



## Approval-notice source fix, host checks and firmware flash - 2026-10-17

Source checkpoint `ce83bd1` corrects repeated native approval notices in the PC
plugin: each physical recipient is admitted once per genuine originating turn;
admission survives Back, expiry and partial delivery failure. Accepted resumed
tool activity retires only that chat's advisory approval ticket. Questions and
native PC decisions retain their owners. Broker traces record accepted permission
boundaries and queued/suppressed/retired outcomes with hashed identities, never
commands, tool inputs or chat text. PC package 0.6.10 is prepared; installation and
live activation are tracked in TODO.md rather than claimed complete here.

The explicitly authorized CCP suite ran 147 cases: 146 passed and the native
firmware-parser case stopped because the existing host compiler environment was
not loaded. After a separate exact-command authorization, the same native parser
case passed with Visual Studio's developer environment. Consolidated evidence is
**147/147 passed**, including five new notice-lifetime/error/privacy regressions;
this was not one entirely green invocation. Both original and recovery logs are
preserved. Package version metadata changed afterward; the tested notice logic
and parser sources are unchanged. No additional simulator/physical test, size
command, diff check, publication scan or push was performed.

The separately authorized new Release build and USB flash completed on COM10,
using IDF 6.0.2 and eight jobs. Image version: `cody-220-gce83bd10`; source:
`ce83bd106312103262310b5bb5036bd22f3537db`. App: 6,925,344 bytes,
SHA256 `cfa37c4ba3f69a3db500ea04667595ccc2dcf6ba2a8cf78278a15d942dc198ee`. The flash command refreshed generated build metadata
before writing; firmware-manifest.json retains both pre-command and actual final
file hashes. All five segment hashes were verified, followed by the flash tool's
RTS reset request. This is flash evidence, not physical UI/touch/acoustic/wake or
post-reset behavior validation. No firmware logic was changed for the PC notice
fix.

Earlier authenticated LAN presence and openable Espressif COM10 were observed.
The SD current-log readback failed with esp_err=263 / errno=0, without reset or
formatting. Historical accepted permissions were not traced; other retained
locator/capability errors do not establish the cause of the original repetition.
The bounded historical log window, failed and passing test invocations, compiled
native-parser evidence, build log, flash log and image hashes are preserved under
`artifacts/build-cody-approval-evidence-20261017/`, copied before mandatory cleanup.

## Debug footprint, dependency locks and PDF validation - 2026-10-17

The owner authorized all automatic checks, compilation, checkpoint commits,
push and USB flash. The three requested implementation gates are resolved:

- **BUILD-DEBUG-OTA-01:** Exact nonzero-alpha storage bounds reduce the existing
  sprite payload from 1,778,624 to 1,475,720 bytes, saving 302,904 flash bytes.
  Full raw indexed/coverage planes and PNGs remain byte-identical. Native tests
  compare all logical pixels and the rendered output for fourteen assets, two
  byte orders and two opacities; bounded rows retain their existing ownership.
  Both modes preserve their original compiler policy, diagnostics, task stacks
  and OTA/storage geometry. The earlier general-code size attempt still exceeded
  Debug by 73,232 bytes and was removed; its failed evidence remains local.
- **PROFILE-LOCKS-01:** Both formerly missing canonical locks are generated and
  checked: display-touch has nine dependencies; full-wake-word has 46. IDF is
  6.0.2 and Luna uses official Brookesia 0.8. Cody's existing lock only refreshes
  its manifest hash, without changing dependency identities or versions.
  New-graph rules do not read invisible Kconfig values. The LVGL 9.5 patch now
  applies to the pinned upstream source. A full build exposed the missing public
  LVGL image-cache declaration; including its documented header fixes the cause.
- **CODY-DOC-TOPOLOGY-01:** The checker distinguishes the two publisher pages
  from the retained body and validates actual register/Security bookmarks.
  The current CCP guide describes diagnostics in both modes and local Memory;
  its ten-page visual body delegates exact protocol fields to CCP1.md and keeps
  historical corrections and Security in the final revision register.
  The quickstart publisher now preserves exactly four A5 instructional pages,
  with direct bookmarks and correct page labels; it adds no technical appendix.
  Both revised PDFs were rendered, visually reviewed and copied byte-for-byte
  to the required Obsidian atlas directory. Other canonical PDFs are unchanged.

The full automatic run recorded 47/48 passing check invocations plus two passing
native graphics/audio preparations. Its PDF failure was repaired and the final
`cody-context-documents` invocation passed, rendering all four documents. Latest
consolidated evidence is **48/48 automatic checks passed**; the failed full run
is preserved rather than rewritten. This includes CCP 142 cases, CCP/Studio 47,
Character Studio 29 with real Windows Tk/canvas capture, the native personality
pixel/render parity checks, the complete profile catalog, the production power
owner with fake peripherals and both selected compiled-frame checks. Native
SIMD arithmetic is a host model, not execution of S3 assembly. Native public hook
metadata discovery passed; it does not prove a live hook or user prompt ran.

Four builds and four size reports passed at eight jobs. Initial validated images:

| Profile | Source / version | App bytes | Free OTA bytes |
| --- | --- | ---: | ---: |
| display-touch Release | 18a363c0 / cody-214-g18a363c0 | 396,976 | 7,401,808 |
| full-wake-word Release | dacb2ed2 / cody-215-gdacb2ed2 | 6,445,232 | 829,264 |
| Cody wake-word/activity Debug | dacb2ed2 / cody-215-gdacb2ed2 | 7,081,968 | 192,528 |
| Cody wake-word/activity Release | dacb2ed2 / cody-215-gdacb2ed2 | 6,925,344 | 349,152 |

The display-touch slot is 7,798,784 bytes; other selected slots are 7,274,496.
The later boot-header change is excluded from display-touch. Full-wake-word's
configuration enables LC76G acquisition; a passing build is not sensor evidence.
Initial app SHA256, in table order:

- `f600abb25fcbfb02084af07d9ea36ee3be395402e289f35ecccec6392e15b513`
- `edd96767ed977dc2cd13b58c0b5750e2373f7506b6330dae0692b71a01fe1d9a`
- `31fd79ce2a10a12344138e1f4a8fb41b9f070dc06ce9f5e10ea079d68ce6c013`
- `d565cad5ef1cfd661bda45d0ece527fa9fb72fabfa50ba9b942cd16b886aab91`

PDF SHA256: CCP `3643215a4bd38bc7186fc89acd3911f4963caff1dbf783abd47671a188a576e0`;
quickstart `7d96d691a9d38aaa6fb6341b3826b7f942b5f8192e29698250415d800807b703`.
Evidence and exact commands are retained under
`artifacts/build-cody-delivery-evidence-20261017/`, including the full-run and
firmware manifests, first failures, targeted PDF recheck and rendered pages.

The stale POWER-ACTIVITY-CHECKS backlog item is closed for automatic evidence.
Its earlier USB worker minimum of 2504/12288 bytes remains historical, not a new
measurement. IDF 6.0.2 uses byte-sized StackType_t here; the old multiplication
therefore did not change that numeric result. Physical input/rail behavior and
nested stack headroom remain in their specific deferred entries.

No live audio pilot or core-dump decoder was run without its required input.
The full-profile resource measurement baseline does not match these feature
variants; its eight unit cases and actual size reports passed, without inventing
a replacement baseline. Physical pixels/touch/audio, measured wake latency,
endurance and nested stack headroom remain unverified. Voice/walking trials and
speech understanding stay deferred. Publication and USB flash evidence follows.


## Publication and USB flash - 2026-10-17

The source-only changed-file secret scan passed (28 files, eight signatures,
two binary assets excluded). git diff --check passed. The requested checkpoints
were pushed to origin/codex/cody; remote commit
`0f24924c1a480fc125c43df01ebf1c0992a04105` was verified after the push.

Before flash, public discovery found no present USB serial candidate. The public
port inventory and Windows PnP independently listed only COM1 and Bluetooth
COM3-COM6, with no present USB Espressif device. The owner was asked to reconnect
and wake Cody; no flash was attempted against an absent port.

After the owner confirmed reconnection, fresh discovery identified an openable
Espressif USB VID 303A / PID 1001 device on COM10. The public flash command ran
shared transient-artifact cleanup, rebuilt at eight jobs and successfully wrote
Release with wake word and the activity classifier. Its final configuration
confirms both features. The image source is
`35467b25c9cbd70e3cb1030022448b30288b816f`, version `cody-218-g35467b25`.
The application remains 6,925,344 bytes, with 349,152 bytes free in the existing
OTA slot; final application SHA256 is
`566e476be6a897bb9be51359ba8108ea2223691dd33fb704c9b14a486350654b`.

The command exited zero. Written-data hashes were verified for all five segments:
bootloader, partition table, initial OTA data, speech models and application.
The command then requested a hard reset via RTS. Its flash list does not write
NVS/settings; the partition layout and task budgets are unchanged. This is
verified flash evidence, not a physical UI/touch/audio/wake-latency or post-reset
boot observation. Exact command, segment hashes and discovery/flash logs are
retained in `artifacts/build-cody-delivery-evidence-20261017/flash-delivery.json`.

The delivery gate is closed. A final documentation-only checkpoint records this
result; its newer Git revision does not change the flashed firmware source above.

## Backlog reconciliation - 2026-10-17

The pending register was reviewed against current source and dated evidence;
this is not an exhaustive audit or proof that every physical gate passed.

- **DOC-01 resolved:** both approved 9 October UI references are present and
  tracked under docs/cody/ui-proposals/20261009. Git 1d46253f (11 October)
  already contains them; the later statement that they were missing was stale.
- **STUDIO-CAPTURE-01 resolved:** the 17 October Windows suite passed all 29
  cases, including actual Tk window/canvas ImageGrab. The 14 October run's
  28/29 failure (`screen grab failed`, twice) remains historical. Its earlier
  27-case run also captured a real window. These are SIMULATED client pixels.
- Retired PWR screenshot/double-click and expression-debug-screen-38 tasks are
  removed from the current queue. Short PWR owns standby/wake and deliberate
  shutdown; Debug BOOT reset is separate. Internal face channels remain while
  the facial-channel UI/storage is removed. UI-07 now tracks the actual shared
  diagnostics/Memory/Back path; ART-03 tracks the composed physical face.
- Known automatic checks are no longer described as unimplemented features.
  Hardware observation, larger source refactors, new boards, OTA upload/rollback
  and deferred product ideas retain explicit pending gates. Current validation
  results are recorded separately after execution.

VOICE-01 history was condensed in the pending register without reopening trials.
Sophia was physically confirmed on 7 October. Twelve owner/receiver host cases
passed on 12 October; the 13 October recording/polling build/size and frame
checks were not a physical recording result. Plugin 0.6.6 plus optional audio
packages were installed on 14 October; 0.6.8 subsequently reported two START
rejections because no native loopback endpoint was configured. Healthy CCP and
later passive capture did not prove recording or recover those attempts.
On 15 October Desktop 26.1007.9446.0 used stdio without an observed TCP listener;
the exported 170-method schema supplied no dictation/transcription RPC. Public
daemon-version failed with Windows 10050, so it proved no attach route. The
launcher/proxy was discarded before activation. A separately authorized public
queue pilot delivered all 359406 WAV bytes to this same chat with matching SHA256;
model prompt preparation stripped unsupported audio, without a transcript.
The owner accepted WAV attachment delivery and declined separately billed
transcription. The implemented receiver now uses that public experimental queue;
old listener setup is historical, and physical end-to-end recording and native
speech understanding remain deferred. Earlier TODO context is preserved in
Git b4738cbf; this reconciliation does not reinterpret it as current evidence.

## Readability, native approval fixes and verification - 2026-10-17

Static review covered Cody activity presentation, Studio native rendering, CCP
notice/protocol owners and selected canonical snapshot consumers. It was not an
exhaustive audit of every source or imported vendor file. The larger ownership,
marquee and by-value snapshot examples remain CLARITY-01 through CLARITY-03 in
planned-todo.md.

- `58c50082` shares three RGB565 conversions through `cody_native_rgb565.py`,
  removes an unused event-view wrapper/re-export, and consolidates reply/approval
  title acquisition into `refresh_notice_title()`. Exact session/turn/checkout
  checks remain before and after IO; original title age and distinct field
  limits remain explicit. Comments describe locks, buffers and state ownership.
- `83bdf266` removes ACTIVITY 1/2/4 admission, obsolete shortening and old HMAC
  branches. Only current activity=5 is accepted, with coherent TECH from the
  same source projection. Presence-only FIND remains a separate capability.
  Ordered headline branches and intermediate identity/freshness variables replace
  the nested ternary. `fbf8b3d3` preserves native preparation on hidden frames.
- `17aeefb0` fixes the invalid 18x20 monitor stem as a centered 20x20 capsule and
  restores the 63-byte Python projection bound for non-approval events. It fixes
  a self-deadlocking approval race fixture by injecting at the canonical owner,
  updates BLE/RTC/offline-power assertions to current owners, and permits the
  approved VibeWare site identity. The obsolete workflow-only test is removed:
  that workflow was intentionally deleted in `ba05e46c`. Current budget tests
  remain. Cleanup now preserves generated feature configuration; its fixture
  shares one explicit directory list for creation and retention assertions.
- `e90ca52a` gives the leading PC caption its own validated region above the
  title. Main content remains below the title; clipping, damage retirement and
  personality exclusions include the caption. Missing/duplicate headers and
  title overlap remain errors. The eight-byte region grows SceneContent without
  adding a framebuffer or increasing task stacks; the personality exclusion
  list retains its existing bound. Shared rectangle predicates simplify the
  repeated geometry comparisons.

The owner explicitly authorized all tests and compilation for this verification.
The first complete run recorded 50 invocations including two native preparations,
with ten failures; the second recorded four. Following the final region fix,
all 14 affected checks plus both preparations passed. Consolidated latest results
are **46 passed / 48 automatic check invocations**, with two failures retained:
`build-profiles` stops at missing `display-touch.lock` (static catalog inspection
also finds absent `full-wake-word.lock`), and `cody-context-documents` stops at the
navigation PDF main-section boundary. Neither gate was suppressed or marked passed.
Live audio pilots, a core-dump decoder without a supplied dump, and the full-profile
measurement checker without matching full-profile artifacts were not scheduled.

The CCP suite passed 142 cases, CCP/Studio 47 and Character Studio 29, including
the real Windows Tk window/canvas ImageGrab case. This resolves the earlier
STUDIO-CAPTURE-01 availability failure; these remain SIMULATED client images.
Runtime checks exercise the real presenter, separate caption admission and overlap
rejection. Power checks include the production owner with fake peripherals.

Both firmware modes used ESP-IDF 6.0.2, eight jobs, wake word and the activity
classifier, from `e90ca52a20bef1d2ba6bf696da716026ac30527c` as
`cody-210-ge90ca52a`. Release passes at 7,228,000 bytes, leaving 46,496 bytes
in the unchanged 7,274,496-byte OTA slot. SHA256:
`c6ae305030e9cd0aff582a4b4ff2e254ea30a3802787c9da1b664782ca870cdd`.
Debug compiles and links at 7,384,656 bytes but **fails** the actual OTA check by
110,160 bytes. Its image is not suitable for that partition table. SHA256:
`41e7c463b198727a231cfecfc17513d0ac7dad8c91c415646a18ea915f018a6c`.
The initial Debug attempt lacked a configured output; public set-target prepared
it from the existing canonical lock before compilation. No dependency lock,
partition, diagnostic logging policy or task-stack budget was changed.

The individual compiled-frame checks passed in both modes, covering 4,846 Release
and 4,879 Debug records. draw_companion_frame uses 3,168/3,184 bytes and
draw_scene_rows 176 bytes in both modes. Source call sites and the small scene
schema growth were reviewed; these individual frames do not measure nested
physical stack headroom. No flash, serial operation, plugin installation/reload
or physical display/audio/touch observation was performed.

Evidence: `artifacts/build-cody-companion-wake-word-evidence/readability-20261017/`.
`verification-summary.json` consolidates results; `final/` preserves the full
second run, `regions/` the final firmware builds and `verified/` the affected
rechecks and compiled-frame reports. Source-only publication uses
`artifacts/reviews/readability-20261017/publication-secrets.log` separately.

## PC approval Release 204 USB delivery - 2026-10-16

Release `cody-204-g7889bffe` built with ESP-IDF 6.0.2 and eight jobs from source
`7889bffe4d0d1ea4c71744d5fb507d037f5f4f99`, then flashed to COM10. All five
written image hashes verified and the normal RTS reset completed. The single
public `device.ps1 flash` operation ran the shared transient cleanup and retained
wake word plus the experimental activity classifier. Generated configuration
confirms the wake-word and Cody integration selections. The app is 7,227,264
bytes, 3,168 bytes larger than Release 199, leaving 47,232 bytes in the unchanged
7,274,496-byte OTA slot. App SHA256:
`64b97a0b274c92332c30eabe9599d797635afef4ff2d42951484abd5f915020c`.

Reviewed compiled static frames in the final build: display worker 352 bytes,
frame drawing 3,168, onboarding update 2,064, event reset 1,968, route entry 384,
event render 64, content raster 224, approval preparation 64 and chat window 48.
The new illustration/title state is outside the task stack in display-owned
PSRAM and retains its compile-time 1,536-byte bound. The canonical event snapshot
still satisfies its 2 KiB bound. CCP worker is 80 bytes, event decoder 96,
transaction application 144, publication 48, and snapshot read/report 32/48.
Task budgets remain 12,288 display / 8,192 CCP bytes. These individual frames and
source call-chain review do not measure nested physical stack headroom.

The old in-use 0.6.8 cache could not be replaced by the public installer
(Windows Access denied). The coordinated title contract was packaged as 0.6.9;
`codex plugin add cody-codex-plugin@cody-local --json` then confirmed installation
at `E:/.codex/plugins/cache/cody-local/cody-codex-plugin/0.6.9`. The manifest, five changed Python modules, README,
protocol and skill were checked against their source copies; all matched at
installation. Reopening Codex is required to load the updated broker/client;
installed files do not prove running adoption. No private cache write, process
kill, trust override or fabricated permission event was used.

Evidence is under
`artifacts/build-cody-companion-wake-word-evidence/approval-redesign-20261016/`:
`generate-scenes.log` and `flash.log`. The source-only publication gate uses
`publication-secrets.log` immediately before the separately authorized push.
No new test suite, Studio execution, standalone size report, serial monitor or
capture was run. Physical pixels, touch, sound and live approval-title delivery
remain unverified. The mistaken personal firmware-name default was reverted in
`88ee3372`; this redesign does not set profile storage defaults.

## PC approval screen implementation - 2026-10-16

Implemented the owner's approved layout with their final corrections: keep the
left Back, remove the project/title box and Chat prefix, retain only the exact
originating chat title in cyan, and scroll long titles within the 320-pixel
viewport. The heading is On your PC:, followed by Approval needed, a colorful
native 2D monitor/keyboard illustration and one Continue on your PC instruction.
The depicted green tick/red cross have no touch targets or native decision path.
Back retires only the informational notice and returns to Home. Offline and
original transport/UI errors remain explicit in the lower line.

ApprovalOnPc alone admits 255 printable ASCII body bytes in the existing schema
2 length-prefixed field. Broker, firmware decoder, canonical payload, Studio
transport and native presentation ABI were updated together. The established
title acquisition owner reads the exact accepted permission session/turn and
revalidates lifecycle/checkout after IO; absent/invalid metadata is explicit.
No GUID, project prefix, another selected Home title or partial title substitutes
it. The marquee uses a pausable visible scene clock, preserves same-event refresh
position and has bounded display-owned PSRAM state rather than task-stack copies.
The lower instruction uses its own measured circular chord. Existing fonts remain
unchanged after definitions-only generation through device.ps1.

Prepared regression coverage for 255-byte title round trips, rejection on other
kinds, stale origin races, one instruction, Back's notice-only effect and clipping
away from lateral navigation. These checks have not been executed. Source changes
do not prove current broker adoption, physical pixels, touch, sound or nested
stack headroom. Release 204 delivery evidence is recorded above.

## Memory diagnostic Release delivery - 2026-10-16

Release `cody-199-g2f640422` built from source commit
`2f640422de17925dffe5b54bc84ca82ec593a6ed` with ESP-IDF 6.0.2 and eight jobs,
completing 1,500 Ninja steps. COM10 flashing completed with all five written image
hashes verified and the normal RTS reset. Wake word and the experimental activity
classifier remain selected. The 7,224,096-byte app leaves 50,400 bytes in the
unchanged OTA slot. The added Memory page and facial-screen removal together
increase the image by 1,920 bytes relative to Release 196; this is not a claim of
net flash savings. The removed facial-list buffer is replaced by a fixed 48-byte
projection. Application SHA256:
`df8e9f30c8dc56ed73c76e10bbb7eefa9c7984137ff3a0eb2b867ffa48e7048f`.

The physical paired LAN Cody then returned an authenticated `character` applied
ACK, error 0, with its actual 34-channel internal animation catalog. The overview
was selected remotely; Memory remains a local visible action. The ACK explicitly
reports `pixels_verified=false`; Memory touch/rendering and perceived sound remain
unverified. No new test suite, simulator, Debug build, standalone size report or
serial capture was run.

Compiled-frame inspection found 352 bytes for the display worker entry, 3,168
for frame drawing, 784 for memory projection preparation, 64 for memory-page
drawing, 256 for a heap section, 128 for centered text and 160 for the Memory
button. The display task retains its 12,288-byte budget. These individual static
figures do not measure nested runtime headroom.

Evidence is under
`artifacts/build-cody-companion-wake-word-evidence/memory-diagnostics-20261016/`,
including the failed `flash.log`, successful `flash-retry.log` and the authenticated
overview command record. The required GitHub publication source-only scan uses
base `bb223f5076840ec1d6d6897c3d630eb24656e36a` and records its own result in
`publication-secrets.log`; firmware images and media are excluded. Publication
scanning is separate from firmware compilation and device writes.

## Memory diagnostic page implementation - 2026-10-16

The owner's additional memory request is implemented as a Memory action in the
existing diagnostic overview, with Back returning to that overview before the
retained normal route. Both firmware build modes and the shared native Studio
layout use the same capacity bars and controls. Studio labels device heap values
unavailable and remains visibly SIMULATED. No new CCP mode or scroll meaning is
introduced; the facial-channel view remains removed.

The page reads existing canonical heap statistics: internal RAM/PSRAM used and
free capacity, largest free block, SDK minimum free amount, free-block count and
the rounded index `100 * (free - largest) / free`, undefined at zero free bytes.
Bars show used capacity, largest free block and other free capacity, not addresses
or the spatial order of allocations. A fixed 48-byte display projection replaces
the removed facial-list storage; acquisition schema, task stacks and measurement
cadences do not grow. No heap tracing, heap walk or consumer-side acquisition is
added. Text/bar coordinates use the shared circular layout; numeric source validity is
checked before rendering, and touch waits for a complete visible page.

The preceding Release build/flash command was cancelled during compilation when
the owner added this request, before any image write. The command exited 1 and
a subsequent process observation found zero active Ninja/Xtensa/compiler tasks.
Its log is under `device-diagnostics-only-20261016/`. Final delivery remains
pending; new simulator/host suites and physical rendering/touch remain unverified.

The first integrated memory build stopped before flashing because the Back
renderer had depended on `CODY_rig.hpp` transitively including the native stroke
API. Added the explicit `CODY2D_paths.hpp` include at the rendering owner; task
stacks and warning policy are unchanged. The failed run's `flash.log` is retained
under `memory-diagnostics-20261016/`; delivery requires a successful retry.

## Device diagnostic cleanup - 2026-10-16

The owner removed the facial-channel diagnostic list from scope to save device
space. Deleted its text preparation/raster functions, PSRAM list storage, gesture
scroll state and display branches. Internal face channels and the animation
catalog remain. The retained `Character` screen keeps its numeric identity 2;
identity 1 is retired. Firmware, plugin and Studio reject the removed mode and
require the reserved wire scroll field to be zero. Studio keeps only the native
Back overlay used by diagnostics and event navigation.

The remaining diagnostic screen is unchanged across Release and Debug and reads
the canonical snapshot: measured CPU utilization on both cores, free RAM/PSRAM,
FPS, IP and SD availability/free space. Unknown/stale data stay explicit; no
consumer sensor reads, new measurement source or task allocation is introduced.
Existing CCP authentication/error assertions are adapted but have not been run.
Final build, device delivery and publication evidence will be recorded separately.

## Gentle question bells and shared diagnostic availability - 2026-10-16

Release `cody-196-gda3e03ad` built with ESP-IDF 6.0.2 and eight jobs, completing
1,501 Ninja steps, and was flashed on COM10 with all five written hashes verified
and the normal post-flash reset. The 7,222,176-byte app leaves 52,320 bytes in
the unchanged OTA slot. Application SHA256:
`5f351a196d36ddd3223fdb84f8466ee34b63e6c03846b8a1ea172d7f675f001c`.

The already-installed MCP client then applied `character` and `face_channels`
sequentially over the authenticated physical LAN, each returning `applied`,
error 0 and the actual 34-channel catalog. Face channels was left selected.
Both responses explicitly report `pixels_verified=false`. No broker restart
or plugin-cache installation was performed; canonical plugin wording is updated,
while existing loaded clients already understand the unchanged screen exchange.

Compiled-frame source review found the 12,288-byte display worker's individual
frames at 352 bytes for its entry, 3,168 for frame drawing, 1,040 for readout
preparation and 64 for channel-list preparation. The 8,192-byte CCP worker's
entry is 80 bytes, with 1,072-byte accept-READY and diagnostic-ACK frames.
These are individual static compiler figures, not measured nested headroom.
Evidence is under
`artifacts/build-cody-companion-wake-word-evidence/gentle-bell-diagnostics-20261016/`.
No new host test suite, simulator, standalone size report, serial capture or
physical pixel/audio observation was performed.

The owner rejected the repeated question alarm and the diagnostic build-mode
choice. Prepared one 1.8-second Soft Chime bell at -24 dBFS, plus one shorter
300-ms reminder at -27 dBFS on the existing ten-second pending-question cadence.
Source recordings remain read-only. Single Codex notices admit at most two
seconds through the existing 320-sample transport scratch; ordinary activity,
personality and repeated clips retain their short bounds. No extra PCM buffer,
task or sample clock is introduced. Digital level is not measured speaker loudness.

At this checkpoint both authenticated diagnostic views were included in the Cody profile
independently of build mode, including capability, request, mailbox, raster, touch,
readout preparation and paused-view refresh. The canonical snapshot, endpoint/
nonce validation, bounded channel storage and original errors remain authoritative.
The plugin's canonical skill/tool description and guide described the same
two views; `normal` restores the route. The later device-cleanup entry above
records the owner's final removal of the facial-channel screen. The mode-choice question was
cancelled after the owner's explicit chat instruction. Test coverage is prepared
but unexecuted; source implementation is not a physical rendering/audio result.

## Offline standby and question-alert Release delivery - 2026-10-16

Release `cody-193-g7eab1a9d` built from the clean source checkpoint with ESP-IDF
6.0.2 and eight jobs, completing 1,551 Ninja steps. The 7,154,832-byte application
leaves 119,664 bytes in the unchanged 7,274,496-byte OTA slot. The public profile
configuration and flash commands preserved wake-word and experimental
activity-classifier selection. COM10 flash completed with all five written image
hashes verified and the normal post-flash reset. Application SHA256:
`05976c28fb397aa2b92019309a5d08c37c293dedbe0dfbec109c3dfb297407c7`.

The public dispatcher performed shared transient cleanup before build/flash.
Model provenance and prerequisite generator logs were preserved under
`artifacts/build-cody-companion-wake-word-evidence/offline-question-delivery-20261016/`,
together with configure/flash logs. Publication inspects changed source files
against the prior remote checkpoint `13830d2725a77416c659f2e92fee48145cca9111`;
firmware images and imported media are excluded from secret-scan inputs.
Its first pass reported the unmodified Waveshare demo network password and a
Brookesia storage-key identifier. Both were reviewed; the scanner binds their
exceptions to the exact original file hash and exact assignment hash, without
altering vendor bytes or exempting other credentials/signatures.
The final source scan passed for 616 changed/untracked source files and eight
signatures, excluding 221 media/firmware assets and retaining the two exact
reviewed vendor literals. Its command result is in `publication-secrets.log`.
No test suite, simulator, standalone size report, serial capture or physical
behavior observation was performed. Actual speaker output, standby current,
manual-wake behavior, elapsed wake time and target stack headroom remain open.

## Pending-question sound reminders - 2026-10-16

The existing 340-ms Question sample now has a bounded five-note initial alert
(2.5 seconds) and four-note reminders (1.66 seconds) every ten awake seconds.
The display owner uses its existing canonical event observation and an at-most-48-byte
identity/timestamp schedule; no extra snapshot, audio buffer, task or sensor read
is introduced. Fresh connected Pending questions qualify on notice, choice and
review pages. Accepted Send and non-pending outcomes stop future attempts;
hidden, offline or stale observations reset the interval without an initial replay.
Quiet hours, volume, Sounds, standby and busy admission retain shared ownership.
Dropped/suppressed attempts never accumulate. Native cadence coverage is prepared
but not executed. Source review does not establish physical playback or timing.

Local activity-model prerequisites were regenerated after earlier disposable
artifacts were absent. The pinned ONNX/calibration identities and preprocessing
are unchanged. The new 172,304-byte W8A16 export has SHA256
`ea7835068e138fd28afa62bce9bea58dab10c841ee07ff73825018914a3532f7`;
the same seven graph operators and four convolution kernels were statically
reviewed before updating the exact compile-requirement binding. No training or
runtime inference validation was performed; weights remain ignored local artifacts.

## Offline standby farewell and shorter software wake - 2026-10-16

Implemented a 120-second confirmed-PC-absence policy on USB and battery, independent
of ordinary battery-only inactivity. Pairing/confirmation and unknown link state
defer the decision; real manual input/completed wake renew the interval. A bounded
canonical projection avoids copying CCP chat metadata onto the power stack.
Existing dark standby can shed voice/radios without a presentation wake. Touch or
physical Power restores ordinary ownership; motion, wake word and queued network
notices do not wake offline standby, and its first contact does not start recording.

The shared power presentation draws a larger upper character and centered ASCII
`Bye bye`, retires complete frames, plays the existing Sleep cue under sound/quiet
policy and holds it visibly before the existing whole-panel fade. Late connection,
setup or touch cancels entry. Original rendering/audio/resource errors remain real.
Software wake retains the 800-ms panel fade but reduces Waking/Ready to 350/250 ms,
for a nominal 1.4-second authored timeline; cold startup durations are preserved.

Static source review only. Native timer/power-owner/voice-wake/lifecycle regression
coverage is prepared, not executed. No tests, validation scans, configure/build/size,
simulator, hardware/serial/flash or remote publication. Current, actual latency,
physical rendering/sound, nested compiled frames and device stack headroom remain
open in TODO.md. Wake word is retained; sending audio to Codex remains deferred.

## Purchased enclosed Waveshare reference import - 2026-10-15

Copied both owner-supplied product folders into `hardware/references/waveshare/`:
615 original 1.75C files and 182 original 1.43C files, including schematics,
images, source examples and supplied firmware. The 134324048-byte complete
1.43C source ZIP remains local and is excluded from Git; extracted ESP-IDF
sources and a revision-specific upstream link are retained. Preserved original
notices and additionally extracted the 1.75C upstream Apache-2.0 license.
Mirrored all sixteen documentation PDFs without conversion into per-product
Obsidian atlas subdirectories.

The [static comparison](docs/bsp/boards/purchased-amoled-comparison.md) distinguishes
the current 1.75 GPS prototype from both C boards, records changed pins and
missing peripherals, and identifies the 1.43C flash/OTA and display-driver
questions. HARDWARE-PORT-01 retains unimplemented board support in
planned-todo.md. Source/document reading only; no checks, build, simulator,
serial or hardware validation, firmware changes or remote publication.

## Delivery-wait Release build and flash - 2026-10-15

Release `cody-187-g58b67f3b` built from the clean source checkpoint with ESP-IDF
6.0.2 and eight jobs, completing 1,499 Ninja steps. The 7,151,328-byte application
leaves 123,168 bytes in the unchanged 7,274,496-byte OTA slot. The public flash
command preserved wake-word and experimental activity-classifier selection.
COM10 writing completed, including application hash verification and the normal
post-flash reset. Installed image SHA256:
`b61de4a9788a267c49a26714ab1551b2f5a4f9dd351c2a41bcc9ee9b7504abc8`.
The GitHub-publication secret scan passed for eight changed files and eight
signatures, comparing against the prior remote branch checkpoint. No firmware
image was secret-scanned.

Evidence: `artifacts/build-cody-companion-wake-word-evidence/reply-delivery-20261015/`.
The earlier authorized physical delivery-probe evidence was preserved there as
`physical-delivery-result.json` before transient cleanup. No new test suite,
simulator run, serial capture, standalone size report or physical observation of
the blue pulse was performed during this delivery.

## Physical reply-delivery probe - 2026-10-15

One explicitly authorized LAN question on the already installed firmware
returned the owner's `First test reply`. The caller read its exact answer ID
and acknowledged it, producing `delivered` with `recipient_acknowledged=true`.
The owner also reported sent followed by delivered on the physical display.
The interval from reading the answer to receiving acknowledgement was 9.813 s;
the acknowledgement tool call itself took 1.166 s. These are caller wall-clock
measurements, not packet timings or a general latency guarantee.

Evidence: `artifacts/build-cody-companion-wake-word-evidence/reply-delivery-20261015/physical-delivery-result.json`, without
ticket capabilities, request/answer IDs or unrelated chat contents. The earlier
unconfirmed question was not reproduced and its cause remains unestablished.
No automatic replay, other caller's acknowledgement, firmware build/flash or
visual validation of the new indicator was performed.

## Delivery-wait indicator source refinement - 2026-10-15

Moved the single `Confirming delivery` status to y=320 below the question and
transport message. Online/error-free waits use a readable electric-blue pulse
with a 1.6-second cycle, shared between firmware and Studio through the native
presentation model. Offline/error states stay muted; real errors occupy y=354.
The line remains read-only and outside narrative audio. Delivery acknowledgement,
expiry and retry behavior are unchanged. Static source review only: no tests,
simulator, configure/build/size, flash or physical visual validation was run.
Pending validation and the separate reported delivery failure remain in TODO.md.

## WAV attachment delivery enabled without transcription services - 2026-10-15

The owner accepts same-chat WAV attachments and declines additional speech APIs,
services or billing. The optional receiver now reuses a standard native stdio
queue client, restricted to initialization, chat metadata and public experimental
queue operations. Completed recordings retain their frozen genuine chat identity;
changed chat/cancellation prevents admission. A matching queue receipt alone sets
Sent to Codex. Rejection and uncertain delivery stay distinct, with no automatic
replay. No model session, modality override, private endpoint or transcript is
created. Speech understanding remains unavailable in the installed model catalog.

The public codex-audio installer now selects the installed native executable,
probes the followed chat/queue without submitting input, then saves explicit
opt-in configuration. Invalid prior settings remain errors. The unused WebSocket
dependency and endpoint/token setup were removed.

Final local checks passed: 10 queue, 10 setup, 11 voice-input, 139 CCP and 14 console
cases (184 total; native host assertions used MSVC). The real read-only setup probe
also passed and saved configuration. Six installed plugin files match source by
SHA-256; backups and manifest are under artifacts/checks/cody-audio-delivery.
Only the verified CCP broker was restarted. A subsequent genuine user prompt
restored followed-chat metadata. Live status confirms authenticated LAN presence,
the installed public_audio_queue route, native_ready=true, no native error and
transcription_available=false. No fresh WAV or physical recording was sent during
this installation, and no firmware build or flash was performed. The earlier
synthetic WAV history/hash evidence below remains separate from physical behavior.

## Public audio trace isolates prompt normalization - 2026-10-15

The owner's second explicitly authorized pilot used a distinct ID and a text
marker alongside the same synthetic WAV. Public queue/history APIs confirmed
that its full 359406 bytes persisted in the intended Desktop chat, SHA-256
c8f0078a7fa9580ddc17f07965cd0f27ee3b108050c042d10c767790f2df38b3.
The original pilot's persisted WAV has the same hash. Thus delivery to chat is
verified, while the model-visible input contains the omission placeholder.

The live catalog returned gpt-6.1-sol with text/image inputs and no audio-capable
entry. Public OpenAI Codex source at 4ca84a10696623fcd7c200db3df9c41671d3bad0
prepares history through for_prompt/normalize_history, then
strip_audio_when_unsupported replaces InputAudio with the exact observed text.
This source mechanism matches the live catalog and observed placeholder; the
installed binary's internal call path was not instrumented. No transcription,
model-capability override, native approval, new model session or physical-device
audio submission was used. The bounded trace reader logs metadata/hashes only;
its notLoaded status belongs to the temporary trace process, not Desktop.

Evidence: artifacts/checks/cody-public-audio-queue/trace-5814395c-8468-4389-96d0-1fb33e310d1f.jsonl.

## Visual communication atlas and audio pilot result - 2026-10-15

The synthetic localAudio item left the public experimental queue for this exact
Desktop chat. The next user turn reported that the audio content was omitted
because audio input is unsupported. This confirms the queue pilot did not meet
the requested same-chat transcription/response gate; no physical voice route
was enabled or retried.

The canonical CCP communication PDF now has 16 pages instead of 49: ten visual
journey pages, cover/contents and a concise final dated revision/Security grid.
Pairing, activity, questions, recording, audio handoff and diagnosis use diagrams;
question and voice UI proposals are marked as proposals, not device captures.
The former full narrative remains in Git history and exact wire fields remain
in CCP1.md. The final rendered pages were visually reviewed. The Obsidian copy
is byte-identical, SHA-256
303623abe87c25872f09ab9a34ad1b85f16797f39ec64d6252e1c6e09baccd18.
PDF authoring proves no new firmware or physical behavior.

## Readable tool names and public audio queue pilot - 2026-10-15

The PC activity owner now projects genuine tool IDs through one English label
table. Known native/GitHub/Cody tools use short actions; unknown names preserve
all readable server/method words with underscores replaced by spaces. Tool IDs,
phases, concurrent correlation, followed-chat selection and argument exclusion
are unchanged. Final host checks passed: 139 CCP cases, including seven new
label/lifecycle cases, and 14 console checks. Three installed plugin files match
tested source by SHA-256. Only the verified CCP broker was restarted; pairing
was retained. The next genuine user prompt restored the followed chat, current
authenticated LAN presence and the canonical caption `Tool: Cody: read connection
status`. This confirms active source adoption and metadata, not display pixels.

The owner selected the public experimental audio queue prototype on physical
Cody. Its exact answer was received and acknowledged as delivered, and the owner
confirmed the physical reply flow. OpenAI's installed native CLI
0.162.0-alpha.17.2 exported the experimental queue/localAudio schema. A short-lived
standard stdio app-server passed initialize/queue-list for the genuine existing
chat and accepted one explicitly synthetic spoken PCM16/16 kHz WAV through
queue-add. It never loaded/resumed a chat or requested a model turn. A subsequent
public API observation found the pilot still queued while this chat was active.
Queue admission/removal does not prove Desktop adoption, understood audio or a
model response. The route remains disabled for physical recordings until those
same-chat checks pass; ambiguous input is not replayed. The pilot receipt is
local under artifacts/checks/cody-public-audio-queue; no capability is published.

The undocumented custom-launcher/proxy prototype was removed from source and
the installed cache before activation. Its earlier fake-backend checks are
discarded-prototype evidence, not retained audio validation. No Desktop/broker
audio capability or global startup override was installed. AGENTS.md and the
existing communication atlas now require prior explanation and specific owner
approval for hacks, undocumented interfaces or fragile overrides. CCP performs
no speech-to-text; public Codex source converts localAudio to native input_audio.

The existing communication PDF now explains the installed countdown/silence
flow, audio ownership, public experimental queue evidence and integration
policy. Its earlier cover revision entry was consolidated into the canonical
final register without changing the date or substance; Security remains
separately labeled. All 49 final pages were rendered and visually reviewed,
with a navigable audio-boundaries entry. The Obsidian copy is byte-identical:
SHA-256 a84e77ef26cc97d7b26930045b9978e071d68702dcc12bd9e0283fd283d10286.
Only this requested PDF was republished. No firmware build/flash was performed;
the installed image remains cody-178-g0d82a93f. Evidence is under
artifacts/checks/cody-public-audio-queue.

## Optional native audio installation - 2026-10-15

The public `scripts/device.ps1 codex-audio` CLI and Maintenance menu now provide
the optional installation step: explicit dependencies in the MCP interpreter,
loopback listener ownership by the Windows Store Desktop app-server, a read-only
probe of the genuine loaded followed chat, and atomic per-user `audio-config.json`
persistence. No capability contents are copied. The audio owner loads installation
settings at startup, identifies intentional environment overrides and preserves
invalid-storage errors. Failed probes do not save, resume chats or submit input.
A successful save requires a Codex restart; readiness does not prove dictation.
The installed 0.6.8 development cache now contains the new configuration module,
updated receiver and guide, verified byte for byte against source. Earlier cache
files were backed up locally. The running broker was not restarted; it loads
this preparation when Codex next reopens. No endpoint file was written.

Final host evidence passed: ten isolated installer/storage cases, 13 voice cases,
132 CCP cases and 14 console checks. Initial voice and CCP attempts lacked a host
compiler on PATH; reruns in the existing Visual Studio developer environment
passed without source or warning suppression. The live setup diagnosis found one
actual Desktop app-server and zero owned TCP listeners, returning failure without
changing configuration. Its process used default stdio on Desktop 26.1007.9446.0;
no supported same-host attach setting was verified in the official documentation.
Native endpoint activation and account/model/audio acceptance remain unresolved.

Evidence, including failed environment attempts and the expected live diagnosis
failure, is under `artifacts/checks/codex-audio-install-20261015/`. This change does
not rebuild or flash firmware. The installed Release remains cody-178-g0d82a93f.

## Codex transport presence and cancellable voice countdowns - 2026-10-15

Source implementation appends Preparing, StartCountdown and SendCountdown without
renumbering earlier voice phases. Capture starts only after complete screen
retirement, three real seconds and a fresh healthy Listening observation. Five
acquired seconds of silence, including initial silence, admit a separate
three-second sending countdown. A straddling AFE block is bounded to its exact
quiet prefix. Preparing/countdowns support immediate tap or shake cancellation;
no END leaves during the sending countdown. Recording retains gesture arbitration.
Actual capture time is separate from request time; the projection stays <=48 bytes.

Completed physical power/touch/motion standby wakes use the same flow, without
duplicating wake-word capture. Shutdown, recovery and Codex-notification wakes
do not start recording. The new optional admission preserves wake errors and
does not undo an already completed power transition.

APPSTATE is a separate signed capability/nonce/endpoint-bound projection with a
strict per-nonce replay barrier. Open reflects a current MCP client; only normal
EOF of the final known transport can report Closed. Crashes, expiry, missing EOF
evidence and source/transport errors remain Unavailable. SessionEnd never means
app closure: it can also mean archive/delete. Home Device shows Codex open/closed
only from admitted current canonical evidence. No new polling worker is added.

Final Windows host evidence passed: 13 voice-owner/receiver cases, native voice
UI/gesture/raster checks with seven SIMULATED countdown previews, 132 CCP cases,
47 CCP simulator cases, snapshot integrity, shared runtime, native renderer,
power source/model and compiled real power-owner fixtures, and git diff --check.
The CLI's global operation lock rejected two initially parallel groups before
they ran. Serial execution passed. The first simulator run rejected an outdated
native DLL; regeneration restored all 47 cases without weakening provenance.
Failed and successful logs remain separate.

Release cody-178-g0d82a93f completed 1,499 Ninja steps with eight jobs. The actual
7,150,896-byte image leaves 123,600 bytes in the unchanged OTA slot; static DIRAM
is 243,644 bytes and external static RAM is 100,804 bytes. Individual frames
passed: power 1,152/8,192, companion 352/12,288 and link 80/8,192 bytes. New voice
frames are request 192, countdown admission 32, feed 48 and transport 256 bytes.
These are not full nested chains or measured runtime headroom. The final flash
stamp is 15/10/26 06:56 in the actual compiling PC's local timezone.

COM10 flash verified the written hash. Installed image SHA256 is
`d10667354fad1cca5daeff5d94028b9e47754a8ae85cf128d780958d0c5062ce`.
A 30-second pre-flash capture included the preceding image's boot records, not
the original reported voice failure. The following 60-second capture contained
no panic/backtrace; it retained known SD/PCB timeouts, HAR cadence rejection,
two USB log losses and one NAMECAP send errno 12. Subsequent authenticated name
and clock reads succeeded. This does not prove voice, gait or optical accuracy.

The 25 installed plugin files were backed up, the new presence module added,
and all 26 registered files verified against tested source by hash. Only the
exact broker was restarted. Authenticated LAN and MCP-source Open resumed with
pairing, Umberto, Asia/Ho_Chi_Minh and quiet-hours-off retained. Existing loaded
development clients still lack the new normal-EOF flag until Codex reopens;
their release remains Unavailable. A real next prompt restores followed chat
metadata after broker restart. No focus or hook event was synthesized.

Evidence is under artifacts/build-cody-companion-wake-word-evidence/voice-presence-20261015,
including final-host-evidence and installed-release178 ELF/map/image/metadata.
Physical GUI closure, voice trials and complete stack headroom remain deferred.
The actual native audio endpoint remains unconfigured; after broker restart,
readiness additionally awaits a genuine followed chat. No second app-server or
UI workaround was introduced to conceal the delivery limitation.

## Four Home panels, finite CCP introduction and other-chat reply - 2026-10-14

Home rotates Tokens, Model, Device and Chat, or advances by tap. Tokens retains
the occupied-context arc and Free/Last/Window; Device owns canonical IP/identity
and independent IMU activity, plus actual immutable compiler-local date/time,
OS timezone identity and hostname. Chat owns the original scrolling title,
current-turn duration and authenticated work description. Changing tools cannot
extend its carousel dwell indefinitely. No whole-chat lifetime, raw command or
transcript is inferred. Long immutable rows and titles get complete reading time.

Actual build entry captures bounded printable ASCII fields into the selected
artifact tree; configure, size and capture do not replace the stamp. Unsupported
values remain explicit, and acquisition/write errors stop compilation. A fixture
initially converted its ISO string into DateTime through JSON; a fresh constructor
preserves the original type without weakening the serializer.

The footer is exactly "Use @CCP in Codex and ask for the available commands".
It enters fully from the right and exits fully to the left three times at
40 pixels/second, then disappears until the display owner restarts. Hidden
routes, standby, failed observations and reconnects pause without replay; no
personalization ticker replaces it. Dead catalogs/modes/chat storage were removed.

ReplyReady appends stable event kind 8 without changing schema 2 or body limits.
One mint headline replaces From Codex and duplicated Ready text, followed by
the admitted original chat title, project and iconless Got it. Generic notices
also omit identical body/title repetition. A genuine background Stop reads exact
indexed title metadata once, preserves the followed cache, and revalidates
session/turn/checkout/phase after IO. Unsupported, absent or overlong titles use
Open the other chat in Codex. Signed dismissal, question priority, once-per-turn
guards and terminal uncertainty remain intact; Stop is not task success.

Executed final Windows host evidence: native rendering, runtime/shared wire,
125 CCP tests, 47 CCP simulator tests, build-mode/metadata, script contracts,
selected wake-word profile and git diff --check passed. Studio ran 29 cases:
28 native/GUI-owner cases passed; desktop ImageGrab failed twice with screen grab
failed. This remains unavailable evidence, not a passing desktop capture.
Earlier 27-case Studio including actual integer-window capture passed before
the ReplyReady addition. Exact native four-panel, six-occupancy and ReplyReady
captures are preserved below four-panel-final-evidence. One native assertion
also caught an old activity purpose surviving an owner error; source admission
now marks that retained observation invalid. All failed logs remain separate.

Release `cody-176-gb43fe3de` completed the 1,500-step firmware build with eight
jobs, size and individual compiled-frame checks. Its actual image is 7,145,936
bytes, leaving 128,560 bytes in the unchanged 7,274,496-byte OTA slot. Static
DIRAM is 243,484 bytes and external static RAM is 100,628 bytes. Individual
frames include companion 352/12,288, diagnostic 208/3,072 and calibration
320/3,072 bytes; these are not complete nested chains or measured watermarks.
The final flash's immutable build stamp is 14/10/26 17:46 in the compiling PC's
local timezone. No firmware image or local build output was secret-scanned.

COM10 flash verified the written hash. The installed image SHA256 is
`58cdc468987e3fd27c0e3095119c1e7cdc8221bd6f515808d0cdee699e8ce16e`.
The following 60-second passive capture completed without a captured panic or
backtrace. It retained accepted SD/PCB timeouts, real HAR cadence rejection and
seven USB log losses; it does not establish gait accuracy or exhaustive logs.
The final ELF, map, image and metadata are preserved in four-panel-installed-release176.

All 25 registered PC plugin files were backed up, replaced from this tested
source and verified by hash. Only the exact installed CCP broker was restarted.
Authenticated physical LAN presence resumed with pairing retained; name Umberto,
Asia/Ho_Chi_Minh timezone and disabled quiet hours were read back from the device.
The preserved 22:00-07:00 schedule is inactive. Restart clears followed activity
until a genuine user prompt; no hook was fabricated to restore it. The native
audio endpoint remains unconfigured, independently of successful LAN pairing.

Evidence is under artifacts/build-cody-companion-wake-word-evidence/home-tokens-20261014:
four-panel-final build/size/frame/flash/capture logs, four-panel-final-evidence
native previews and the dedicated plugin-cache backup. No physical voice,
gait, optical display accuracy or complete stack-headroom validation is claimed.

## Occupied context progress, independent Still and taller ribbon - 2026-10-14

The Home arc now fills from left to right with occupied context, from zero to
100 percent; Free still reports the remaining tokens. White/cyan/orange/red
thresholds and the orange compaction override are retained. A bounded integer
comparison avoids uint64 overflow and a falsely full arc near UINT64_MAX.
Six explicitly SIMULATED native captures cover 0/20/40/70/90/100 percent. Studio
checks color, growing length, direction, unchanged surrounding layers and exact
RGB565 pixel preservation. One initial assertion confused dark RGB565 edge
rounding with opaque coverage; the corrected assertion still rejects every
unfilled pixel at 100 percent. The failed run is retained separately.

Still is an independent acquisition-owned IMU observation after at least 1.5
seconds of genuine quiet gravity/gyro measurements, outside calibration and
standby. No trained model or prior bias calibration is required. Missing,
failed, nonfinite, repeated or over-200-ms samples cannot prove Still. HAR
publication preserves the separate observation; proven Still parks new HAR
windows and retires in-flight results, while movement restarts collection.
Home reads only the bounded canonical projection; no additional sensor polling,
task, sample window or framebuffer was added. Static lock review found no new
inverse acquisition. The detector is at most 80 bytes; nested stack chains and
physical classifier/posture accuracy remain separate evidence.

The purple recording artwork is 58 pixels high, five extra pixels above and
below the original center. Its even DMA damage band spans 60 rows; strip
reconstruction checks both byte orders and two-/sixteen-row transfers. The
historical artwork generator can no longer overwrite the procedural ribbon.
An authenticated PC audio rejection now has an explicit firmware error log.

Executed Windows evidence: 24 Studio tests, native rendering contracts,
Cody runtime/Still/shared snapshot contracts, voice UI/gesture/DMA contracts,
snapshot integrity, the selected wake-word dependency/profile check and
git diff --check passed. Evidence is preserved under still-final-evidence and
context-final-/still-final- logs alongside the prior Release records. Firmware
compilation, size, compiled frames and installation of this increment are pending
at this implementation checkpoint. No physical gait or recording trial is claimed.

## Question flow, native chat title and context colors - 2026-10-14

The shared firmware/Studio policy implements numbered full-width answers, two
visible rows, swipe paging, complete detail review and explicit Send. All Cody
action captions omit icons; lateral Back remains separate. Multiple caller-owned
questions retain private capabilities and original host deadlines. Signed queue
navigation never answers or changes the followed chat. Lost handoffs, withdrawn
targets and informational dismissal are handled by authenticated observations.
Recipient acknowledgement alone permits Reply sent. Native prompts already open
on Desktop remain on the PC; reviewed hooks route future semantic choices through
CCP and notify other-chat replies without inferring task success.

Home reads the actual native chat title, with exact chat/checkout ownership and
read-only SQLite metadata. No GUID or Chat prefix is displayed. The complete
bounded title scrolls at 40 pixels/second; Connection stays for the full reading
duration, at least eight seconds. Hidden time pauses animation, not source age.
Title observations expire independently after 45 seconds. The context arc and
Free value are white below 25% occupancy, cyan below 65%, orange below 85%, and
red from 85%; compaction keeps orange priority. Integer ratio comparisons retain
exact boundaries even at uint64 limits. Larger event observations use owner-held
buffers rather than duplicate snapshot copies on the UI stack.

Executed Windows host evidence: native rendering and runtime contracts, 119 CCP
tests, 47 CCP simulator/batch tests, 40 Alpha tests, 22 Studio tests, and snapshot
integrity passed. Studio/native were repeated after the color change. The selected
wake-word profile/dependency contract passed. Whole-catalog checking stopped at
the pre-existing missing display-touch.lock; unrelated profile resolution is not
claimed. Failed attempts retain their logs: one stale DLL was rejected, followed
by a fixture that incorrectly excluded immediate detail text; both were corrected
and the full simulator suite passed. Proposal PNG/HTML were regenerated and
visually reviewed. Existing PDFs were not revised.

Evidence logs use questions-final-* below
artifacts/build-cody-companion-wake-word-evidence/home-tokens-20261014/.
Release cody-174-g111525b1 was built and flashed on COM10 with eight build jobs.
The image is 7,141,264 bytes, with 133,232 bytes remaining in the OTA partition;
SHA256 is 39ca4427fa4cf02a62c13f13aad96087ab60fc8b7cfcbd6c24bd6680570e88f4.
Static DIRAM uses 243,468 bytes; external static data uses 99,220 bytes. Compiled
frames passed, including the CCP worker (8,192-byte budget), question projection
(32 bytes), and presentation helpers (largest individual frame 1,968 bytes).
This is not a runtime task watermark or complete nested-chain measurement.

The first firmware attempt exposed a void publication API used as a result.
It now returns the original failure, borrows the synchronous source instead of
copying the growing link schema, and queues failed asynchronous projection for
the existing worker. The corrected full build passed without compiler warnings
from changed sources. Passive 60-second capture found no panic, stack overflow
or CCP title/technical rejection. Known SD timeouts and cadence rejections remain;
USB log loss was six records, with zero formatting/queue/truncation loss. No SD
change was attempted. The exact installed CCP 0.6.8 source was backed up and
updated in place after Windows refused the installer cache rename; all 25 source
files matched, and only the PC broker restarted. Authenticated physical LAN
presence, retained pairing and a current real native title were observed afterward.
Host pixels and authenticated metadata do not prove physical display, audio,
classifier accuracy or task stack watermarks. Secret checking is now confined
to GitHub publication of changed sources, per the user's explicit instruction.

## Question-flow design proposal - 2026-10-14

Eight illustrative 466 x 466 screens and exact 2x PNG copies are generated by
`device.ps1 generate -Generator cody-question-flow-mockups` under
`docs/cody/ui-proposals/20261014-questions/`. The contact sheet and offline HTML
prototype cover a background reply, question attention, pending chats, two
original numbered choices, further choices, full-text review, explicit Send,
and recipient receipt. The viewport replaces the visible pair when scrolled;
there is no free-text option. The HTML retains original sample labels and
descriptions and exposes a scrollable review while Send stays fixed.

The proposal uses existing Cody artwork and the licensed Telex font. Its source
crosswalk retains original screen IDs 26/27/31 and verified PDF pages in flow.json;
existing PDFs remain untouched. Individual control/text bounds are admitted by
the generator against circular chords. The rendered eight-screen sheet was
visually reviewed. This is design evidence: no native-question integration,
firmware changes, build, flash, hardware tests or browser execution is claimed.

## Persistent Home dashboard and integer Studio pixels - 2026-10-14

The selected context-arc proposal is implemented by the shared native renderer.
The character, status title and model row remain in place while three lower
panels rotate every eight visible seconds: tokens and IMU activity; price class and
thinking effort; connection, abbreviated chat ID and the device's own IP.
A passive tap advances immediately and restarts the interval; shake selects
Tokens. One shared native policy preserves remaining dwell across hidden views,
pauses for active gestures and gives compaction priority on Tokens, followed by
a fresh eight-second interval. No Python timer duplicates the firmware policy.
The old full-screen technical pages, firmware row and page counter are removed
from normal navigation. Supported chat requests scroll continuously below the
panel. Compaction retains orange priority, its animation and admitted sound.
Canonical timestamps, validity and operation errors govern every displayed value.
The first installed dashboard exposed a personality/status collision at the
compact Home anchor. A Home-only twelve-pixel symbol translation preserves
size and collision admission, re-primes position on layout changes even while
paused, and shares the real status footprint with Studio. Studio also uses
Home's nominal scale 73 when preparing the cue. These changes leave the cloud,
cue hold/fade clocks and non-Home layouts unchanged.

The antialiased arc uses bounded persistent paths and the existing native
coverage rasterizer. Generated Telex-derived fonts retain their OFL provenance.
No sensor reader, polling task or enlarged task stack was introduced. Hidden
Home layers retire completely before setup, questions, recording or Debug take
ownership; source freshness remains independent of the paused presentation clock.

Studio enlarges the actual 466 x 466 native framebuffer by integer replication:
2x means 932 x 932 with uniform 2 x 2 pixel blocks. Resize selects 1x, 2x, 3x, 4x
or larger integral scales; smaller viewports clip at 1x instead of interpolating.
Mouse admission maps every enlarged block to its original pixel in all rotations.
The actual Windows capture on the 3840 x 2160 desktop verified 2x at startup,
a 956 x 956 canvas and a 1631 x 1421 client area, with exact framebuffer colors.
These are explicitly synthetic Studio observations, not physical display evidence.

Native raster and runtime contracts, 18 Studio tests, 40 Alpha tests, 36
authenticated simulator tests, selected wake-word profile/dependency checks,
diff whitespace and the changed-file secret scan passed. Stale Alpha fixtures
were aligned with authored QR/navigation state, native Q8 opacity, production
DPI initialization, integer layout and boot bitmap publication; initial failed
attempts remain in the evidence logs. The final carousel rerun passed native,
runtime, Studio, profile and secret checks. The first target compilation caught
a potentially truncated secondary-label join; exact bounded copies fix it
without suppressing warnings, changing values or expanding a buffer.
The final personality regression passed 27,831 native assertions and 128
native/Python trace frames. It covers every cue at the Home anchor, animated
poses, paused layout handoff and genuine exclusion failures. Studio additionally
checks actual Wi-Fi/Confusion pixels and the shared status footprint. The host
personality build now uses C++20 for canonical snapshot types; an initial C++17
compile failure and a deliberately rejected stale Studio DLL remain recorded.
Release cody-169-g6dba911a-dirty was built and installed with the Home symbol
collision correction: 7,122,736 image bytes, 151,760 bytes of OTA headroom,
compiled frame inspection passed, and a sixty-second passive USB capture
showed no Home personality collision, panic or CCP technical rejection.
Authenticated LAN presence was observed afterwards. The known SD/PCB limitation,
IMU timing invalidations and a reduced display-DMA row warning remain explicit;
this is not physical audio, motion-accuracy or task-watermark validation.
Evidence and native/actual-window captures are retained in
`artifacts/build-cody-companion-wake-word-evidence/home-tokens-20261014/`.

## One-time expensive-model notice and PC question audit - 2026-10-14

The lower Model panel now shows the price capsule and thinking effort without
repeating the shared header's model name. A complete visible Expensive price
claims one five-plink audio event per model change. The bounded shared native
latch survives carousel changes, stale observations, reconnects and standby;
unseen notices are superseded by a newer model. Audio admission, cancellation,
quiet hours, volume and voice-recording gates remain with the existing owner.
A CC0 Kenney plink contributes 3,186 flash bytes, reused five times with four
100-ms gaps, without a new firmware task or PCM sequence allocation.
Studio uses the same native latch after canvas presentation and its existing
host audio queue. This parity does not establish physical speaker playback.

Final portable native and runtime contracts, all 20 Studio cases, the selected
wake-word profile/dependency contract, diff whitespace and changed-file secret
scan passed. Regressions cover delayed prices, stale/error sources, model
replacement and return, carousel non-replay, multi-rate five-clip timeline,
source/hash/WAV/PCM/C++ equivalence, and one Studio queue event containing exactly
five notes. Studio also saved current native 2x/3x and actual Windows captures.
Release `cody-170-gfbb0bee7` was built, sized and flashed on COM10 with esptool
write-hash verification. The image is 7,127,008 bytes with 147,488 OTA bytes free;
SHA-256 is `accc8736df7e326e4ba3274252b849e24bfddcf70a6bb5a0bcea959077e724c2`.
Relative to cody-169, this increment adds 4,272 image bytes and 128 external-BSS
bytes; static DIRAM remains 236,796 bytes, external RAM is 86,180 bytes.
Compiler sidecars passed the task-frame check, including the existing audio
worker at 256/4,096 bytes and companion worker at 352/12,288 bytes. These are
individual compiled frames, not physical high-watermarks; the Home raster
frames and previously reviewed bounded nested raster path remain unchanged.

The subsequent sixty-second passive Release capture recorded no panic,
Home personality collision or CCP technical rejection. It retained known SD
timeouts, IMU cadence invalidations and the reduced fourteen-row display-DMA
warning. USB loss was six messages, with zero format/queue/truncation losses;
the capture is not lossless. Authenticated physical LAN presence was observed
after flashing. Release suppresses informational playback logs, so neither
audible delivery of the new cue nor physical speaker level is claimed.
The `plin-final-*` logs/reports, native/Windows screenshots and five-note preview
are preserved under the same protected evidence directory above.

A separate read-only PC audit found no cross-chat pending-question registry;
native question tools remain Working and permission notices follow one chat.
An actual asynchronous return preceded its user answer, and two rejected broker
turns were correlated with child agents. The analysis and proposed event-driven
registry are in [runtime.md](docs/cody/runtime.md#pending-native-questions-pc-audit-2026-10-14).
No PC bridge correction or native-question delivery is claimed by this increment.

GitHub repository visibility was verified public. Active main ruleset 25049727
requires pull requests with sole direct-push bypass for owner user 12001165.
Its reproducible definition is `.github/rulesets/main-owner-direct-push.json`.

## Compact Home tokens and three visual proposals - 2026-10-14

Home keeps four separate Last, Window, Free and Total pages with short labels,
the uppercase `TT` unit, decimal `K` for whole thousands and periods every three
displayed digits: `Free 319K TT`, `Total 21.188K TT`. Counts below 1000 stay exact.
The three discarded digits are intentional presentation truncation; technical
panels and canonical/wire counters retain their exact values. Free subtracts
the coherent original counters before abbreviation. Missing, stale, over-limit
and revision-mismatched observations keep their explicit nonnumeric state.
An unchanged compact caption avoids an unnecessary visible revision. No polling
task, font, source cadence or stack size was added.

Focused native/raster and actual Home/runtime contracts passed, including unit
boundaries, zero, `UINT64_MAX`, subtraction-before-abbreviation and old/stale
data. The rebuilt production Studio binding passed 12 tests; retained native
captures show all four compact pages and unchanged character/timer pixels.
The native capture bridge covers the footer, not the complete onboarding title
or command-banner composition. Diff and changed-secret checks passed before
the implementation checkpoint. Evidence is retained under
`artifacts/build-cody-companion-wake-word-evidence/home-tokens-20261014/`.

Three built-in ImageGen proposals and their exact prompts are saved in
[the proposal register](docs/cody/ui-proposals/20261014/home-token-proposals.md):
compact token card, context arc and two metric tiles. The existing character
was the reference; generated PNG copies match the original files byte for byte.
These are illustrative concepts with synthetic values, not installed graphical
changes, native captures or physical-display evidence.
Release build/size/flash are pending at this implementation checkpoint.

## Native launch, event delivery and visible token pages - 2026-10-14

The effective native MCP configuration contained a literal `${PLUGIN_ROOT}`
directory and failed with Windows error 267. Both packaged manifests now use
the installed plugin directory and a relative stdio entrypoint. Genuine MCP
transport leases retain the existing lifecycle; no fake lease, idle hook, chat
selection or secondary Codex host was introduced. The native plugin manager
installed the corrected 0.6.7 package; source/cache hashes match and control,
pending-pairing and active-pairing files remained byte-identical. Configuration
resolution is verified separately from an actual Desktop MCP handshake.

The bounded native reader now accepts resolved Windows extended-path spelling,
the optional native ordinal and known irrelevant record types without decoding
their bodies. Exact chat/checkout/turn admission, source timestamps, confinement
and 128-KiB poll limits remain. A read-only observation of the genuine followed
turn returned Last call 282,297 / Window 475,000, Input 281,496, Output 801 and
Total 9,051,637 at source age 12.313 seconds. Four bounded polls read 417,792 bytes
in 3.72 ms. This proves PC-source compatibility, not device pixels. Accepted
prompt/completion/compaction lifecycle logs contain hashed identities only.

Home now presents retained discovery as Connecting, retains token/model rotation
in Ready, shows Reply ready for five seconds, then three rotating supported
requests. After another 25 seconds its idle title becomes Ready to team up;
healthy presence is never relabeled as Connecting. The first passive tap opens
Model & Context. Its eight-second Token details page reserves Input, Output and
Total rows even with long model names and compaction. Active compaction keeps
orange priority and suppresses command suggestions. Observation and presentation
clocks are explicit separate parameters in firmware and Studio.

Final host evidence: 106 CCP tests, native raster/geometry, actual Home/event
presenter, runtime and snapshot contracts, 12 Studio tests and 36 authenticated
CCP simulator tests passed. Synthetic event-chain captures cover working tokens,
Input/Output, active compaction, refreshed counters pending and reply ready;
the numeric-page title is checked against actual native glyph pixels. Earlier
failed compilation, excluded-fixture boundary, simulator caption width and
missing fixture contour phase were corrected; attempts are retained. Visual
inspection exposed the separate Studio page-clock bug before delivery.
Selected profile/dependency and changed-secret checks passed. Artifacts are
retained under `artifacts/build-cody-companion-wake-word-evidence/events-reconnect-20261014/`.

Release checkpoint `4d811054` built with IDF 6.0.2 / eight jobs and was flashed
to COM10 with image hash verification: `cody-164-g4d811054-dirty`, 7,058,064 bytes,
216,432 bytes free in the unchanged OTA slot, static DIRAM 236,812 and external
RAM 84,484 bytes. SHA256:
`9719e0a027f69b078c78c136987f1043b5f493bf1aa58b16b3548be3933b2732`.
The dirty marker includes the preserved user-owned AGENTS.md change. Against the
previous Release, image growth is 1,728 bytes and static DIRAM growth 192 bytes.
Compiled task-frame checks passed; the known worker/draw/geometry/rig subtotal
remains 7,072 / 12,288 bytes, excluding deeper/library calls. No stack was enlarged.

Native Desktop stdio transports started with the corrected installed configuration.
The verified pre-update broker was stopped once so its existing genuine MCP
transports could recover using the updated package; no helper created a lease.
The replacement broker retained authenticated device presence beyond its 60-second
hook-only grace, with zero observed/followed chats at the 103-second check. A later
genuine prompt selected a chat normally. The installed broker then reported current
Last call 155,173 / Window 475,000, Input 154,427, Output 746 and Total 21,188,756,
with source age 2,497 ms and authenticated Cody presence after the firmware flash.
This was the automatically followed chat, not this repository chat; no background
agent changed selection or reported progress for it.

A 60-second passive COM10 capture completed without an additional reset. Captured
output contains no panic/backtrace/reset marker; it retains the accepted SD timeout,
HAR cadence-window rejections and an explicit USB loss counter of 15, so it is
not a lossless whole-startup trace. Physical pixel/audio confirmation during genuine
compaction and physical stack watermarks remain in CCP-USAGE-01 and the existing
target-verification register. Synthetic captures, source telemetry, authenticated
presence and physical rendering remain distinct evidence.

## Native chat tokens and Home presentation - 2026-10-14

Checkpoint `49e2fb6` integrates genuine followed-chat metadata with strict
session/checkout/turn identity, original source timestamps and bounded tail
bootstrap (128 KiB per poll). Native response bodies do not provide telemetry.
SQLite is read-only and explicitly closed. Native last-call/input/output and
cumulative counters remain distinct; capacity is supplied explicitly and the
unobserved compaction threshold remains unavailable. Free subtracts Last call
from the same observed Window, not a guaranteed future-request budget.
PC-only checkpoint `a0a2a9c` lets fresh genuine hook model metadata replace expired
native model metadata without refreshing the old reasoning effort. Read-only CCP status returns one
coherent activity/technical bundle per paired device.

Home cycles four exact token pages, preserves carousel timing under continuous
updates and briefly displays four supported chat requests on connection/chat
change. The Activity trial badge is removed without changing sensor validity.
Orange compaction overrides ordinary working captions/cues; sound is admitted
once per genuine chat/turn/sequence after a complete visible frame. Completion
requires the same turn, and counters older than PostCompact remain unavailable
until a new native sample. No new polling task, training or SD repair is added.

Final checks passed: 103 CCP tests (including actual firmware count/age parsers,
bounded native reader and original-age cases), native raster/geometry, runtime,
snapshot integrity, 27,154 personality assertions / 128 parity frames, 12 Studio
tests and 35 simulated CCP tests. First failures exposed an unclosed SQLite
handle and obsolete stale-caption expectations; fixes and failed logs are retained.
Selected profile/dependency, legacy-pattern, diff and changed-secret checks passed.
Five visibly SYNTHETIC Home captures are retained; the Studio bridge covers token
pages and compaction actor/overlay/timer, not the onboarding title/command banner.
The final 12-test Studio rerun also verifies persistent character/timer pixels
and exact native regions after PNG serialization; no firmware rendering change
was needed for an incomplete tool preview.

Both IDF 6.0.2 builds used eight jobs and identify `cody-160-g49e2fb63-dirty`:
Debug 7,222,720 bytes / 51,776 free, static DIRAM 245,332, external RAM 85,172;
Release 7,056,336 bytes / 218,160 free, static DIRAM 236,620, external RAM 84,484.
SHA256 Debug `08e9b46220e337fd08f22475846a9396388c3a287c8bb20cc2691ce508dba0bc`;
Release `2fc3bbf646c2c08d0a07968a8d164b224650eeac13e7d1cc85d244f32e4188ec`.
Individual compiled task-frame checks passed. The known worker/draw/geometry/rig
subtotals are 7,120 Debug and 7,072 Release against a 12,288-byte worker stack;
they exclude deeper/library calls and do not establish runtime watermarks.

Release was flashed to COM10 with image hash verification. A 60-second passive
capture retained the accepted SD failure and explicit rejected HAR cadence windows,
with no panic/backtrace/reset marker in that observation window. The later CRC
endurance read succeeded: original session 1 remains interrupted_unknown at
24,942,870 ms, owner error 0, source_current false and unknown final UTC retained.
An earlier immediate read after the preliminary firmware install returned
ESP_ERR_NOT_SUPPORTED; it was not interpreted as an empty or successful journal.

The native manager installed plugin 0.6.7; reviewed cache files match source and
the pairing/control-state hashes were preserved. A live pre-reload read observed
one followed working chat/turn; the later plugin reload correctly returned no
followed chat until a genuine user prompt. No artificial hook or MCP lease was
created. Physical compaction audio/display behavior and classifier/voice trials
remain separately unverified. Evidence is under
`artifacts/build-cody-companion-wake-word-evidence/telemetry-home-20261014/`.

## Active backlog cleanup - 2026-10-14

At the user's request, TODO.md now contains only four autonomous work items:
firmware footprint, PC plugin alignment, selected Debug/Release delivery and
explicit Release endurance readback. Previous unresolved checks are preserved
in planned-todo.md as a deferred verification register, without claiming they
passed. The duplicate peripheral FAULT-04 is named FAULT-PERIPHERAL-01 with its
historical alias retained. Physical voice and walking/classifier trials remain
postponed at the user's request. The accepted SD/PCB constraint is unchanged.

## Autonomous maintenance and Release preparation - 2026-10-14

Three agents covered transparent asset packing, canonical endurance readback and
independent backlog/CLI integration review. Only the exactly transparent two-pixel
border of the fourteen personality sprites is omitted from embedded planes. The
logical 256-pixel canvas, original PNG/raw assets, visible alpha and palette remain
unchanged. Payload savings are 56,896 bytes. Native/Python checks passed 27,154
native assertions and 128 controller parity frames, including exact packed pixel
and native/swapped RGB565 comparisons. Obsolete fixtures now verify the authored
quiet battery policy and its separate fades instead of expecting interruption.

The explicit `scripts/device.ps1 endurance-read -Port COM10` command reads a
versioned 128-byte canonical snapshot through the existing token/CRC USB transport.
Its owner callback does not read sensors or NVS, tick the recorder, or write device
storage. Existing journal persistence remains unchanged. The host preserves
unknown termination, source gaps and recorder errors; a verified owner failure
is saved before the command returns failure. Seventeen real-service scenarios,
PowerShell decoder/wrapper cases and real USB command-parser cases passed.
Device console, power policy, liveness and all 87 CCP regressions also passed.

The native plugin manager installed and enabled Cody plugin 0.6.6. Cached manifest,
server, receiver, runtime and hooks match the reviewed source; pairing/control
state hashes were preserved. Installed discovery reports ten enabled, trusted
authored hooks, with exact event/label identity checks. The configured Python 3.13
interpreter now has cryptography 50.0.2 and websocket-client 1.9.2. Native audio
endpoint/token-file configuration remains absent; no audio was submitted. Hook
discovery does not prove execution or adoption by the current Desktop chat.

The selected IDF 6.0.2 Cody Debug wake-word/activity image built at eight jobs:
7,218,336 bytes, 56,160 bytes free in the unchanged 7,274,496-byte OTA slot,
56,112 bytes smaller than the preceding image. Static DIRAM is 245,044 bytes
(16 more); this is flash recovery, not measured RAM or energy savings. Individual
compiled frames passed, including the USB command task at 1,024/4,096 bytes and
canonical readback callback at 160 bytes. Nested call-chain headroom remains
separate. Release configuration uses the existing canonical dependency lock.
The first unconfigured Release build stopped at its lock guard, before compilation;
the explicit `set-target` workflow prepared the separate Release directory.

The selected Release image then built successfully: 7,052,768 bytes and 221,728
bytes free in the same OTA slot. Static DIRAM is 236,316 bytes, 8,728 fewer than
Debug; external static RAM is 84,228 bytes. Both images identify source revision
`cody-156-g4c6c1707-dirty`; the user-owned AGENTS.md worktree edit was preserved.
SHA256: Debug `b04bf8a0f190ece6b899263c8a6a6ebe094fcc1d41ea06147d432fe6149e7289`;
Release `05f939074c174618e988694cea7afae183a9f6210361093979d95cbe8a9b2f4c`.
Release individual task-frame checks passed, including USB 1,024/4,096 bytes and
readback callback 160 bytes. Selected Cody profile/dependency contracts passed;
this does not validate missing locks for other profiles or a complete matrix.

The first simulator regeneration exposed real host discovery ambiguity: ESP
Clang on the firmware PATH was selected as a Windows compiler. Host discovery
now prefers available MSVC on Windows and verifies GCC/Clang host OS/architecture,
rejecting cross compilers with their actual target. Regeneration passed with ESP
Clang still on PATH and the manifest identifies x64 MSVC. All ten character Studio
tests and a visibly SYNTHETIC ready capture passed using the regenerated production
C++ binding. This proves host integration, not device pixels or physical FPS.

An initial Release capture exposed an attempted PCM lookup for `WifiConnected`
(cue 15). Four authored technical cues explicitly disable sound and have no PCM
asset. Completed-frame admission now consults that policy and cancels old retries
for those silent cues; unknown identities and missing audible clips retain the
audio owner's real error. The native fixture verifies all authored rules, four
silent identities, existing audible battery cues and unknown/None rejection paths.
Both selected images and compiled-frame reports were regenerated after this fix.
No physical voice recording or classifier accuracy trial was performed.

A cached 0.6.6 process was observed owning the UDP listener, but the later read-only
authenticated loopback health query timed out. Current Desktop chat adoption and
presence remain unverified; no MCP lease or artificial hook/event was created.

The final Release image was flashed to COM10 and esptool verified the transferred
data hash. NVS was not erased. A subsequent passive 60-second capture completed,
followed by a successful explicit USB endurance readback: initialized recorder,
owner error zero and a CRC-checked 128-byte response. Session 1 still retains
exactly 24,942,870 ms, its original start/first-wake UTC, 874,343-ms first wake,
unknown interruption, unavailable last-alive UTC and source-unverified flag;
the previous record remains empty. USB-time `source_current=false` is preserved,
not relabeled as fresh battery telemetry. This is retained-journal verification,
not a new battery-life measurement.

The final passive log has no unknown-sound/optional-personality-audio error,
panic, stack-canary, brownout, reboot or frame-failure signature in the captured
interval. It still reports the accepted SD initialization timeout; no SD repair
or formatting was attempted. Host tests, firmware transfer and this bounded
capture do not prove physical voice, wake timing, whole-board shutdown, display
quality, classifier accuracy or energy savings. TODO.md now has no remaining
autonomous implementation/delivery item; those deferred gates stay in planned-todo.md.

Final repository review passed `git diff --check 4e71fbb` and the public
`changed-secrets --base-ref 4e71fbb` check: 38 changed/untracked files and eight
signature families, with no finding. All session changes have local checkpoints;
the only remaining versioned worktree change is the user's AGENTS.md edit.

Current logs/reports and the protected original battery readback are retained
under `artifacts/build-cody-companion-wake-word-evidence/`, including
`autonomous-20261014/` and `battery-readback-20261013/`. Physical voice and walking
trials remain explicitly postponed; no training or SD repair was introduced.

## First 1,000 mAh battery run readback - 2026-10-13

The user reported a 1,000 mAh battery and operation with USB disconnected. A
passive 130-second COM10 capture on reconnection recovered session 1 from the
installed `cody-142-g869e45e2-dirty` firmware. Its Debug configuration retained
Sophia wake-word listening and the pretrained activity observer, with automatic
Light-sleep disabled. Three periodic reports agree on the exact saved fields:

| Field | Retained value |
| --- | --- |
| Phase | `INTERRUPTED_UNKNOWN` |
| Last-alive elapsed | `24942870 ms`, 6 h 55 min 42.870 s (415.7145 minutes) |
| Start UTC | `1791816011`, 12 October 2026 21:40:11 Asia/Saigon |
| First wake | `874343 ms` after start; UTC `1791816885`, 21:54:45 Asia/Saigon |
| Source observations | `source_gaps=1` |
| Last-alive UTC | `UNAVAILABLE` |
| Previous session | Empty, id 0 |

README reports the practical result as **about seven hours on 1,000 mAh in this
configuration**, including standby, with a link to the evidence limits. The
counter establishes last saved liveness, not the exact end of battery power or
continuous Wi-Fi/Codex presence. Incomplete source observations cannot certify
uninterrupted battery-only operation from telemetry alone. Capacity is reported
by the user; no capacity, current, display duty-cycle or Release measurement was
performed. Later polling/UI/reconnect source improvements were not installed.

The background collector had stopped after a failed port enumeration; the
explicit passive readback succeeded and did not flash or request a reset.
Raw/parsed evidence is retained locally as
`artifacts/battery-endurance-watch/readback-20261013.log` and
`artifacts/battery-endurance-watch/readback-20261013.json`. The user-requested
README and power guide now record the result; continuous-source validation
remains in `BATTERY-ENDURANCE-01`.

## Saved-network startup and wake reconnect - 2026-10-13

Three agents covered Wi-Fi retry ownership, CCP/startup integration and host
verification. A saved Wi-Fi profile receives a passive `Please wait` /
`Connecting to Wi-Fi` view for up to 15 seconds from its first eligible offline
evaluation. Boot branding does not consume the interval. A fresh healthy Wi-Fi
observation with association and IP advances immediately; genuinely absent
credentials still enter first setup. The existing recovery choices return after
timeout. Hidden actions have no admitted targets, repeated frames do not restart
the deadline, and failed content preparation cannot be cached as successful.
Original observation errors remain logged and canonical during the grace.

Completed wake shortens only an existing distant Wi-Fi retry to one second,
without resetting the ordinary exponential backoff, initiating a new connection
or postponing an imminent retry. The accepted timer command retains its deadline
against stale callbacks. Wake and acquired IP prompt the existing CCP worker's
next FIND; canonical LAN readiness remains usable during power/display
transitions. Hints coalesce within their original attempt and preserve explicit
command priority, authentication and eight-second READY expiry. A new
authenticated-peer log supports later wake-to-PC timing. No additional polling
task, periodic timer, large array or snapshot is introduced.

Final host checks passed: real extracted Wi-Fi scheduling/callback and CCP
admission helpers, real startup selector/presenter, all 87 CCP tests, power-policy
contracts and liveness contracts. The first CCP run exposed a nondeterministic
pricing fixture that reused a host prompt timestamp; the corrected fixture uses
coherent explicit synthetic clocks without changing production admission. Two
host-fixture shadow warnings were fixed by renaming fake state. Final fixture
compilation is warning-free. These tests use fake RTOS/renderer boundaries; they
do not measure pixels, radio timing, current or physical wake behavior.

The first linked Debug image exceeded its OTA slot by 416 bytes. Direct passive
content preparation and shared error reporting removed duplicated work; a second
image still exceeded by 32 bytes. Consolidated diagnostic strings retained the
real operation/component causes and produced a passing IDF 6.0.2 wake-word/activity
build at eight jobs. No feature, partition or warning gate was removed. Final app
size is 7,274,448 bytes, only 48 bytes free in each unchanged OTA slot; further
Debug growth requires a footprint reduction. Static DIRAM is 245,028 bytes
(16 more than the previous image). Binary SHA256 is
`7e04efe769e315a6eae6879e832432003e0cc2420af140786a1d98d20c634335`.

Size and individual compiled-frame checks passed. The two new owner hint APIs
each use a 32-byte frame; the grace gate uses 32, network presenter 64 and startup
selector 976 bytes. These are individual records, not nested stack bounds or
device watermarks. Build/size logs, first failures and final host fixtures are
retained in `artifacts/build-cody-companion-wake-word-evidence/wake-reconnect-20261013/`.
Target timing and installed-device UI remain open in `WAKE-RECONNECT-01`; no
flash or reset was performed, preserving the ongoing battery image and NVS journal.

## Voice recording controls, rendering and polling - 2026-10-12

Three agents reviewed acquisition lifecycle, voice ownership/transport and host
verification. AFE startup and paused workers now block on explicit notifications;
owner acknowledgments use a static semaphore with the original bounded deadlines.
Both workers receive resume/rollback/terminal notifications. Independent handle
lifetime locking prevents a standby waiter from blocking failure notification.
Rollback retains an uncertain audio route and never resets an AFE that did not
complete suspension. Existing task stacks and active PCM/inference timing remain.
Disconnected voice transport drops from 5-ms to 100-ms service away from deadlines;
Debug power-stack scans run every two seconds instead of every 100-ms policy tick.
BOOT polling is retained because no interrupt currently protects short presses.

The recording screen has persistent send/silence/discard instructions, one-tap
send with 350-ms double-tap arbitration, shake/double-tap discard and automatic
send after more than two seconds of acquired VAD silence following speech.
Guarded owner APIs reject stale-session and post-END intents without claiming
remote discard. Filled procedural RGB565 alpha layers target 30 FPS; canonical
fresh microphone levels control only visual gain. The 784-byte knot buffer belongs
to the display owner, and the renderer uses existing DMA slot capacity. Static
instructions do not repaint each frame; metadata and timer refresh at 5 Hz.
No video, SD audio storage, new microphone reader or per-frame allocation is added.

Initial host execution passed the ten real extracted AFE lifecycle helpers,
twelve voice-owner/receiver tests and native raster/gesture contracts. The UI
fixture encodes actual native pixels and font glyphs into a visibly SIMULATED
preview; its gain is synthetic. The first default-Python receiver invocation
could not import cryptography; the existing activity environment passed it.
The first power source scan exposed an obsolete inline-wait assertion, updated
to verify both worker calls and the shared gate. The final AFE, twelve voice-input,
native UI and power-policy suites all passed; new host fixtures compile without
warnings. The native 90-frame GIF/PNG preview was generated and inspected.
Final diff whitespace validation and the changed-files secret scan passed.

The selected IDF 6.0.2 Debug wake-word/activity build and size passed at eight jobs:
app 7,273,008 bytes, 1,488 bytes free in the unchanged OTA slot, static DIRAM
245,012 bytes. Binary SHA256 is
`3612828e65d32a00023d471513101ef4148e88b050ae5c7bdf1222d2d2d09720`.
The app grew 880 bytes relative to the preserved installed-image baseline.
Individual compiled frames passed: feed 64/4096, fetch 48/3072, companion worker
352/12288 and power task 1152/8192 bytes. Recording-row and ribbon raster helpers
use 208 and 128 bytes respectively. These are individual frames, not complete
nested call-chain bounds or measured stack watermarks. SDK/managed-component
Kconfig notices remain visible in the retained build log; no warnings were hidden.

Build, size and the installed-image backup are retained under
`artifacts/build-cody-companion-wake-word-evidence/voice-poll-ui-20261012/`.
Host logs are under `artifacts/checks/voice-poll-review/`, with native previews in
`artifacts/checks/cody-voice-ui/`. No flash, reset, physical gesture, FPS, current
or battery-duration claim follows from these results. The installed battery-run
image and collector deadline are preserved.

## Energy, memory and maintenance review - 2026-10-12

Three independent review scopes covered power/endurance, memory/performance and
architecture/documentation. The installed battery-run firmware is unchanged;
the following corrections are source and host evidence, not target validation.

| Area | Implemented correction | Scope and limit |
| --- | --- | --- |
| Endurance publication | Coalesce unchanged metadata instead of republishing every second; retain original timestamps, real errors and failed-publication retries. Durable record changes always publish. | Keeps the same 60-second NVS commit cadence. No measured current reduction is claimed. |
| Luna shell memory | Read a bounded power-state projection in the 200-ms timer and asynchronous drain callback; propagate real read errors. | Avoids full snapshot copies in those two paths. This is Luna shell code, not the installed native Cody renderer. |
| HAR failure lifetime | Delete a rejected model outside the acquisition mutex and stop admitting work. | Frees model-owned allocations; deliberately retains the parked 8192-byte worker because IDF capabilities-task cleanup needs another allocation. Target failure injection remains open. |
| Stack telemetry | Rename canonical, HTTP and simulator fields to stack_high_water_bytes. | JSON consumers must update the field name. IDF 6.0.2 Xtensa numeric values are unchanged. |
| Incremental builds | Track model bytes, preprocessing and provenance as CMake configure dependencies. | Existing configure-time rejection runs again when those inputs change; target incremental/tamper execution remains open. |
| Check evidence | Atomically replace endurance reports with running, passed or failed status and the actual failing stage. | An intentionally missing compiler replaced an existing passed report with failed/compiler-discovery and a nonzero exit. |
| Documentation | Align battery hysteresis, PWR/BOOT controls, Cody software standby and Release readback limitations with source. | The user-confirmed PCB/SD limitation and standby/wake workaround are accepted operating context, not a new SD defect task. |

The real endurance service passed all 15 host scenarios, including coalescing,
publication recovery and publication while persistence is disabled. The shell
lifecycle passed with PMU enabled and disabled, including timer/drain read errors;
the same public suite passed mocked build-clean boundary/error checks. HTTP and
snapshot integrity contracts passed. The native simulator graphics DLL rebuilt,
its visibly synthetic boot-ready preview was inspected, and all seven simulator
groups passed, including 22 loopback HTTP tests. Logs are retained under
`artifacts/checks/energy-memory-review/`; the endurance report remains under
`artifacts/checks/battery-endurance/`. The deliberately failed report is isolated
in `energy-memory-review/report-failure/`, not presented as a firmware failure.

The review found sound canonical acquisition ownership, explicit PSRAM/internal
memory placement and conservative DMA retirement. Remaining opportunities are
the then-existing AFE fetch worker's 20-ms paused polling, other field-specific Luna consumers
that still copy full snapshots, and stronger real HAR window-boundary coverage.
The later voice/polling increment above implements AFE notification/stop ownership
and its host coverage. No training or classifier-driven shutdown was added.

Historical USB evidence for the installed image reports only 2368 bytes of OTA
headroom, internal heap minimum 12063 bytes and largest free internal block
12800 bytes at 65.113 seconds (the retained endurance monitor log). These are
baseline constraints, not edited-image measurements or current battery results.
No firmware configure/build/size/flash, physical interaction or current measurement
was performed in this review. TODO.md retains target validation and a bounded
explicit Release endurance readback; planned-todo.md retains measured optimization
follow-up. The battery-only experiment continues independently on the installed image.

FAULT-01 is retired from the active software investigation at the user's direction,
not claimed fixed: historical ACMD41 busy-before-FAT and recovery after reinsertion
remain evidence; memory-only SDIO exclusion and explicit 400-kHz probing did not
resolve it. The official one-bit GPIO2/1/3 baseline, external pull-ups, no-card-detect
configuration and real errors remain unchanged. The accepted operating workaround
is standby/wake. No extra reset loops, formatting or shared-rail cycling were added;
storage-dependent TTS still requires observed availability and audible evidence.

## Battery endurance recorder implementation - 2026-10-12

Added current/previous battery-start, last-alive, first-wake and shutdown-request
records in one versioned CRC-protected 112-byte NVS journal. The canonical power
loop schedules 60-second commits without a new task, timer or wake lease. PMU
sampling remains with its owner. Any observed USB ends the run; stale/unknown
source intervals stay flagged. Boot retains an interrupted run's last durable
liveness lower bound with unknown cause. UTC is optional original observation;
missing time is shown as unavailable. Storage/type/size/schema/CRC failures preserve
the journal. Recorder failures remain real optional component errors and do not
undo a completed wake or bypass mandatory shutdown owners.

The real service passed twelve host scenarios, including USB reconnect/unplug,
intervening source gaps, standby elapsed time, immediate first wake, shutdown abort,
reboot retention, unavailable UTC and failed/corrupt/oversized storage. Real NVS
owner tests reject invalid sizes before allocation/data read, preserve destinations
on failure and honor shutdown sealing. Updated Power-owner and runtime checks and
snapshot integrity passed. Firmware size/flash, compiled stack reports, device
watermarks and actual battery-only duration are not established by these host tests.
The [power guide](docs/bsp/services/power-management.md#battery-only-runtime-recording)
documents the physical run and interpretation.

Final target source `869e45e` built and flashed on COM10 as
`cody-142-g869e45e2-dirty`, with five image hashes verified and NVS excluded.
App binary: 7,272,128 bytes; each unchanged 7,274,496-byte OTA slot retains
2,368 bytes. The size report also passed. The first implementation exceeded both
slots by 3,856 bytes; GCC `-Oz` did not reduce this target. Scoped `-Os` on NVS,
HTTP, pairing, OTA and provisioning source libraries resolved the overflow without
changing render kernels, Sophia, HAR weights or the partition table. Failed-attempt
logs and final ELF/binary/capture are retained under this build's `evidence/`.

A 180-second passive USB capture retained forty real HAR inferences: 7.132-125.808
ms, original sample spans 3.953299-4.004895 seconds, ten rejected partial windows.
No fatal markers were captured; accuracy remains unvalidated. At three endurance
checkpoints the PMU reported battery present, 100%, USB present/good, and the journal
correctly stayed `EMPTY` with unavailable UTC instead of inventing a battery run.
One optional snapshot-publication timeout was logged with recording not disabled;
later checkpoints continued. A transient PKEY read timeout recovered. The existing
SD mount timeout remains. This is USB-connected operation, not battery endurance.

The compiler-frame check covered 4,735 records. Task frames: Power 1,136/8,192,
hardware monitor 208/3,072, HAR worker 336/8,192, Cody worker 352/12,288 bytes.
The endurance tick uses 192 bytes; its bounded clock/battery projections and
48-byte settings entry frames avoid a full canonical snapshot on the Power stack.
These are individual frames, not a complete nested-call bound. Cody's captured
worker minimum free was 2,504/12,288 bytes. Power/HAR headroom during physical
standby/wake/off, actual battery-only duration and whole-board shutdown remain open.
Correction from the subsequent source review: IDF 6.0.2 Xtensa defines
StackType_t as uint8_t, so the monitor's multiplication by sizeof(StackType_t)
leaves the IDF byte count unchanged. The misleading canonical field name was
corrected to stack_high_water_bytes; no fourfold numeric error was established.

Final checks passed: twelve endurance service scenarios, real runtime/settings
contracts, production Power owner with fake peripherals, native raster/lifecycle,
seven simulator groups, snapshot integrity and the selected Cody dependency closure.
The simulator first rejected a stale DLL after the canonical schema grew; the
public simulator command regenerated it and the repeated check passed. Its native
boot-ready preview was also inspected, visibly labeled synthetic. The frame checker
was corrected to exclude GCC lambda-helper names from the enclosing worker match.
Changed-file secret scanning covered seventy current files with eight signatures;
`git diff --check 3240a4b2` passed. Whole-catalog dependency verification remains
blocked by missing unrelated display-touch/full-wake-word locks, as recorded below.
No physical key actuation, USB disconnection, battery depletion or rail-power
measurement was performed during that capture. The user subsequently started
the unplugged run; its saved records are still awaiting USB reconnection/readback.

## Cody power and pretrained observer implementation - 2026-10-12

Follow-up validation was explicitly authorized on 12 October, including repeated
tests and simulator runs without per-suite prompts. Power specifications/source
contracts and the compiled production owner functions passed, including USB/full/
absent-battery/stale/error gating, manual short-Power isolation, three-second hold,
shutdown ordering, optional USB delivery loss, mandatory owner failures, rollback
and PMU-accepted-but-still-running timeout. Peripheral results in this fixture are
synthetic; no physical key press or rail shutdown is claimed.

Also passed: 14 console contracts; native raster/lifecycle contracts including
farewell hold and two-row versus sixteen-row parity; runtime/persistence/shared
standby owner contracts; 10 Character Studio tests; 35 CCP simulator tests; seven
reference simulator groups including 22 loopback HTTP tests; Cody wake-word profile
closure; snapshot integrity. The native simulator generated and visually reviewed
`artifacts/simulator/power-validation/boot-ready.png`. Corrected stale test fixtures,
C++20 test compilation and the graphics driver's missing expression-catalog source.
The whole-profile catalog check still reports missing `display-touch.lock` and the
unselected `full-wake-word.lock`; the selected Cody profile passes. These unrelated
profile gates are not silently treated as passed.

Latest installed firmware: `cody-135-gfda9e7e3-dirty`, source checkpoint `fda9e7e`
on `codex/activity-classifier-poweroff`. The public Debug flash command used
`-Profile cody-companion -Feature wake-word -ActivityClassifier -Jobs 8 -Port COM10`.
ESP-IDF 6.0.2 compilation and all five flashed-image hashes succeeded. The app is
7272544 bytes, leaving 1952 bytes in each unchanged 7274496-byte OTA slot. NVS was
not included in the flash write set; no push was performed.

The final 120-second passive serial capture observed **22 real HAR inferences**:
window spans 3956664..4020948 us and inference times 7639..63240 us. All observed
top labels were `sitting`; this is an unvalidated model output, not ground truth.
There were 15 rejected partial windows and no captured panic/backtrace markers;
uptime reached 142.475 seconds. Delayed-attempt logs confirmed acquisition on CPU1.
Canonical snapshots reported display, touch, PMU, IMU, Wi-Fi and BLE ready, and zero
allocation failures. Existing SD initialization timeouts and counted boot/USB log
losses remained visible, so the capture is not an exhaustive error-free boot trace.
Evidence under `artifacts/build-cody-companion-wake-word-activity-debug/evidence/`:
`activity-inference-fda9e7e.log`, matching `.elf`/`.bin`, and `flash-fda9e7e.log`.

Physical Power presses, full-board shutdown/restart, touch/shake/Sophia wake,
USB/full/absent-battery transitions, classifier accuracy, energy cost and stack
headroom remain unverified. At this first inference checkpoint, separate tests,
simulator, size and scans had not run; the later authorized host/simulator results
are recorded above. Hardware rail loss and measured battery endurance remain open.

- Added canonical-power inactivity gating, Power-only short standby/wake,
  three-second valid-hold farewell, retired-frame acknowledgment, gradual fade,
  settings/speech/storage/logger shutdown ownership and checked AXP2101 REG10H[0]
  command with visible failures. BOOT remains separate. Cody retains software
  standby for short-Power wake; ordinary radio/voice retention is unchanged.
- Added opt-in ESP-DL 3.3.13 inference using the original Espressif-example ONNX
  weights, bounded canonical 80-sample/20-Hz acquisition, original class scores,
  rejection/stability metadata, experimental Activity-panel visibility and a
  host sample-export path without SD. No automatic energy decision uses results.
- Successfully downloaded and converted the fixed model with ESP-PPQ 1.3.11 in
  an isolated artifact environment, without training. Recovered fixed standardization
  and axis-major input order reproduce all 107 supplied calibration tensors exactly;
  this corrects the old example deployment's raw-g input mismatch. Weights remain local ignored
  artifacts because their redistribution license is unresolved. See
  [model provenance and contract](docs/cody/activity-observer.md).
- The initial checkpoint completed static source/integration review only. Later
  build, flash and passive-monitor evidence is recorded here; physical power
  behavior and separate test suites remain open. `scripts/device.ps1 ports`
  identified COM10 with Espressif
  VID 303A/PID 1001; enumeration did not open serial or validate firmware.
- Authorized flash preparation exposed an application-size blocker before any
  device write: the ESP-DL 3.3.11 image linked at `0x81bdf0` bytes against
  `0x6f0000` OTA slots. Official ESP-DL 3.3.13 operator/kernel selection reduced
  the linked image to 7,580,944 bytes, still 306,448 bytes too large. The intervening
  missing `RequantizeLinear` link dependency was corrected in the requirement set.
  These failed flash attempts did not install firmware or establish runtime behavior.
- W8A16 conversion, the pinned official operator/kernel requirement set and scoped
  size optimization reduced the model to 172304 bytes without training or changing
  the OTA partition table. Model SHA-256:
  `b5333dafe62835cc9999fe27ea652802fcdcaa5250604c5136087d1c1044d4e0`.
- The first physical flash (`98b9485`) verified all five images, but passive serial
  observation exposed a ready-frame abort: the one-row DMA emergency lease violated
  Cody's paired-row contract. Checkpoint `3e1a1f4` moved the three 1600-byte activity
  windows to PSRAM, aligned embedded model data to 16 bytes and supplied a two-row
  Cody emergency lease. Its 7271024-byte image fitted with 3472 bytes remaining and
  flashed successfully. The next capture passed the size contract but still aborted
  on the 500-ms frame deadline with only 4943 DMA bytes free. The logger's 32768-byte
  internal boot backlog was still awaiting archival after the failing ready frame.
  Both failure captures and matching ELFs are retained under the activity build's
  `evidence/` directory; neither flash establishes a healthy startup.
- Checkpoint `8a3f060` added bounded logger-owner archival completion before the
  ready frame; live cache-sensitive log buffers remain internal. The 7271616-byte
  firmware flashed with all five hashes verified. Its 120-second passive COM10
  capture reached 142.945 seconds uptime without the earlier display abort, but
  activity windows repeatedly rejected real 75-84-ms sampling gaps. No HAR output
  was observed. Existing SD card initialization timeouts remained visible. The
  capture and ELF are retained as `evidence/activity-cadence-8a3f060.*`.
- Checkpoint `c12adaa` makes the acquisition owner wait only the remaining time to
  its 50-ms deadline, including acquisition/commit work. It preserves the actual
  timestamp guards and UI cadence. A worker allocation failure now stops the extra
  activity request instead of retrying it continuously.
- The 7271680-byte `c12adaa` firmware flashed successfully, but its next 120-second
  capture still contained repeated rejected windows and no HAR results. Static
  review identified unrelated successful IMU acquisitions shifting greedy model
  admission, and the deadline starting before owner-lock delay. Checkpoint `327b820`
  admits only explicitly scheduled activity acquisitions and starts their deadline
  after those locks. Real read errors still invalidate the observer; no synthetic
  samples or relaxed cadence limits were introduced. The prior capture/ELF remain
  in `evidence/activity-phase-c12adaa.*`.
- The 7271744-byte `327b820` image also flashed successfully and remained active
  through a 120-second passive capture (144.865 seconds uptime), but real 75-79-ms
  gaps still repeatedly invalidated windows. No inference success is claimed from
  these scheduling corrections alone. Evidence: `evidence/activity-gap-327b820.*`.
- `521e43d` added bounded acquisition-owner timing. Its 7272432-byte image flashed
  and the 60-second capture measured delayed attempts with refresh-lock waits of
  6-24 us, stage waits around 300 us and IMU calls around 1.5 ms. Rejections often
  contained 15 samples over 960 ms, showing roughly 64-ms dispatch instead of the
  requested 50 ms. These observations do not support the full-refresh mutex as the
  dominant cause. The unpinned floating-point acquisition task can acquire CPU0
  affinity under IDF, where higher-priority voice tasks run. Timing evidence and ELF:
  `evidence/activity-timing-521e43d.*`.
- `fda9e7e` pins only the activity-enabled acquisition owner to CPU1 at unchanged
  priority 3; voice stays on CPU0 and renderer priorities are unchanged. Other
  profiles remain unpinned. This produced the real inference results recorded
  above. Remaining occasional stage/refresh contention still rejects windows
  rather than filling gaps or silently weakening the model's input contract.

## PDF covers and contents - 2026-10-12

- Updated the five canonical Cody documentation PDFs with coordinated VibeWare
  covers, Umberto Giacobbi copyright, Draft / Confidential / Internal use only,
  clickable major-section contents, bookmarks and current PDF page labels.
  Section headings, rather than historical offsets, determine destinations.
  Existing content, dates, screen IDs and security records are preserved.
- The explicit all-document request adds cover and contents to the A5 quickstart;
  its four instructional pages remain together without a technical appendix.
  The Luna logo PDF is a graphic source asset and is not a documentation guide.
- Publication uses the public generator, refreshes front matter after document
  generation, and checks byte-identical Obsidian mirrors. All pages rendered for
  contact-sheet review; covers and contents reviewed at readable resolution.
  No firmware builds, tests, simulator or device operations were part of this task.

## Connection, activity and CCP operator reference - 2026-10-12

- Follow-up: the middle price band now reads `Reasonable price` in full.
  Expanded the Model-only chip to 196 pixels and kept its independent timer
  inside the same 320-pixel row. The price chip fades with Model and disappears
  during Project/Branch. Static review only; no additional checks or flash.

- Implemented a silent active-time discovery cycle: connected Wi-Fi symbol,
  question mark, moving lens, worry/fear and recovery. Search standby is ten
  minutes; Working is three minutes. A real software-standby wake into Working
  can show a four-visible-second red/unhappy or green/happy battery cue;
  charging, absent or non-current samples skip it. Backend work continues.
- Clarified Project/Branch/Model labels, orange Model text and a Model-only
  Cheap/Reasonable/Expensive price chip. Added bounded current tool labels
  without transmitting arguments/commands. Expanded technical labels and
  canonical PC IP/running firmware identity. Missing token/context/reasoning
  data remains unavailable; no Desktop usage or billing bridge was implemented.
  Native host ABI and preview source match the changed interfaces.
- Added four 64 x 64 compile-time native alpha planes (16 KiB flash), typed IDs
  15-18, source/generated declarations and generator contract. Static review
  only: no tests, scans, firmware builds, simulator execution or hardware
  operations. Compiled frames, stack headroom and runtime rendering remain open.
- Revised the CCP Communication Atlas to 43 pages. Operator pages 27-34 cover
  configuration, Debug/Release capabilities, 17 tools, three skills, ten hooks,
  audio and reset boundaries. Final register pages 40-43 preserve earlier
  corrections and Security history. The PDF agent rendered all pages and
  visually inspected each new/operator/register page. This is document-render
  evidence, not execution of CCP commands or firmware validation.

## VibeWare repository presentation and license - 2026-10-12

- Aligned the root README with the VibeWare initiative: `This is VibeWare`
  heading, approved logo, canonical manifesto, both GitHub repositories,
  personal site, Terms and Privacy. Removed the generic software-with-AI
  disclosure; website links carry GitHub referral UTM fields with distinct
  placements. Added the project MIT license from the owner's brand template
  and expanded existing third-party notices for speech, wake-word and branding
  license boundaries. Recorded the footer convention in AGENTS.md. Static
  document review only; no website availability, build or runtime checks.

## Voice presentation and unified communication reference - 2026-10-12

- Replaced the three working dots with three rotating gears, with the middle
  gear turning in the opposite direction. Waiting and compaction use a fast
  two-hand analog clock instead of pause bars/hourglass. Antialiased polygons
  remain in the existing `[324,374) x [118,179)` damage region, with active-time
  animation and unchanged hidden/ready behavior. Gear and hand pose tables use
  5,120 compile-time bytes; rendering borrows flash-owned vertices without a
  new task, heap allocation or per-frame trigonometry. Static review only;
  this icon source has not been compiled, simulated or flashed.
- Acquired two requested passive COM10 captures, 30 and 60 seconds, without
  resetting or changing firmware. The first retained reset reason 4 (panic)
  from the preceding boot; neither window contains a new panic/backtrace.
  The later window reached 277.954 seconds uptime in standby. Companion
  stack minimum free was 2,736/12,288 bytes in the first window. This confirms
  a preceding panic but neither reproduces the reported loop nor identifies
  the trigger; FAULT-04 retains the open investigation and matching artifacts.

- Built and USB-flashed clean source `33377507` on COM10 with ESP-IDF 6.0.2,
  Cody Debug/wake-word, eight compiler jobs and the shared transient cleanup.
  Firmware identity: `cody-114-g33377507`. The app is 7,011,312 bytes
  (`0x6afbf0`), leaving 263,184 bytes (`0x40410`, 257.015625 KiB) in each
  7,274,496-byte OTA slot. The build's partition check passed; no separate size
  command was run. App SHA256:
  `457265df464e74ea92cfbea6d6c73a905542850dafabc7af7f275d340e739e7d`.
  Esptool identified the ESP32-S3 on COM10 and verified all five image hashes:
  bootloader at 0, partition table at 0x8000, initial otadata at 0x10000,
  app at 0x20000 and the 873,368-byte model pack at 0xe00000. NVS was not a
  write target; no full erase or storage format was requested. Flash exited zero
  and issued the normal RTS reset. Log retained under
  `artifacts/build-cody-companion-wake-word-evidence/firmware-20261012-33377507-flash.log`.
  Post-reset startup, two-slot boot selection, battery/gesture/audio behavior,
  native PC delivery and physical stack/CPU/PSRAM headroom remain unverified.
  This operation did not run host suites, simulator checks, monitor or capture.

- Hardened audio source delivery with early native reachability admission,
  four-packet cumulative ACK transport, bounded reconnect grace, END-aware
  uncertainty and current same-chat steering preconditions. Added recording/native
  turn/artifact correlation and fixed independent JSON-RPC request-ID handling.
  Prepared focused host regressions; static review only, no tests/build/flash.

- Bounded capture and host PCM to sixty seconds / 1,920,000 bytes, retaining the
  independent ten-second PSRAM acknowledgement ring. Removed the voice Cancel
  capsule and touch action: the retired voice view owns deliberate shake. Added
  elapsed/remaining seconds and one measured fade/scroll carousel ordered model,
  catalog cost band, thinking effort, branch, project. Unavailable captions disappear
  and no price is invented. The character remains hidden.
- Changed the lower hint to alternate `Shake to fade` and
  `We will send the audio to Codex for transcription`, with measured scrolling
  inside the existing clipped row. Deliberate shake retains cancellation semantics.
- Added sixteen synthetic waveform poses (1,024 bytes), a 440 x 40 logical-pixel
  ribbon with four phase-shifted curves, lightweight Q8 anti-aliasing and alpha
  composition, plus partial changing-band transfers after the first retirement.
  Ten code-authored visual proposals share the waveform data; their metadata and
  fonts are illustrative, not device captures or simulator evidence. Source
  review only: no tests, builds, size checks, simulator run or device flash.
- Consolidated the technical pairing PDFs into the 34-page CCP Communication
  Atlas, preserving fourteen dated revision entries, three Security entries and
  original page/screen crosswalks. All pages were rendered and independently
  reviewed. The corrected quickstart remains four A5 pages, also visually reviewed.
  Retired duplicate technical PDFs remain in Git history; current generators and
  document links use one canonical technical reference. The context checker was
  updated but not executed.

## One-way voice input source integration - 2026-10-11

- Added a derived Cody/Wi-Fi/Codex capture consumer to Sophia events and acquired
  double taps. The canonical voice projection publishes typed capture/delivery
  state; the existing AFE supplies enhanced mono PCM. A separate priority view
  retains the interrupted route, suspends character evaluation and shows four
  precomputed waveform poses at 8 FPS with a local Cancel capsule.
- Added the 320,000-byte PSRAM acknowledgement ring, VAD end-of-speech and explicit
  bounds/failures. The existing CCP worker owns independent UDP 42119 delivery,
  with AES-256-GCM, fresh-discovery session binding, nonce-separated ACKs, exact
  ciphertext retransmission and explicit cancellation. No device SD/flash audio
  storage or new firmware task/stack enlargement was introduced.
- Explicit sleep and acquisition pauses retire active audio even when standby
  retains the wake-word microphone. An already transmitted input stays unconfirmed
  instead of claiming cancellation; a held or rotated second tap does not advance
  the technical panels. Native submission uses one bounded 15-second deadline.
- Plugin 0.6.5 adds bounded receive/delivery workers, private PCM WAV artifacts,
  exact followed-chat binding and native localAudio submission. The installed
  Codex schema was exported through `device.ps1 generate -Generator cody-codex-audio-schema`
  and confirms localAudio; desktop endpoint/account/model/approval integration is
  still unverified. Missing dependencies/endpoints and native rejection remain
  explicit failures. Prepared host boundary cases and updated MCP catalog assertion
  were not run. Static source integration only; VOICE-01 retains build, size,
  installation, wire/gesture/stack/CPU and physical gates. No new firmware flash.

## OTA application slot layout - 2026-10-11

- Replaced the single factory application with otadata and two aligned application
  slots in both selected 16 MiB layouts. Wake-word slots are 7,274,496 bytes each
  (6.9375 MiB); ordinary slots are 7,798,784 bytes each (7.4375 MiB). Model and
  LittleFS remain independent shared partitions, with no SD use.
- Against the recorded 6,990,864-byte Debug wake-word image, each new wake-word
  slot has 283,632 bytes of arithmetic headroom. This is not a new linked size:
  at this checkpoint subsequent battery/audio edits, boot selection and OTA
  transfer remained unbuilt and unverified. The deliberate 12 October USB
  installation and newly linked size are recorded above; OTA transfer and
  physical boot selection still require separate evidence.

## Quiet battery notice implementation - 2026-10-11

- Added display-owned event state for measured 20/10-percent warnings, actual
  charging entry and measured 100-percent charge. Free Home admits caption and
  existing manga together after ten seconds, with four seconds of retired-frame
  dwell. Source expiry clears notices; conversations, touch, setup and Sleep retain
  their routes and standby timing. Ongoing charging remains silent.
- Updated the authored low-battery latch to 20/25-percent hysteresis and regenerated
  its immutable rule headers through `scripts/device.ps1 generate`. State and text
  preparation remain in the existing display owner; no new sampler, task or stack
  enlargement. Static integration review only: builds, tests, Studio automatic
  notice parity and physical validation remain open in BATTERY-01.

## Illustrated Cody atlases - 2026-10-11

- Re-authored Planning and Navigation from the earlier cyan-character workbook,
  retaining stable screen IDs, writable notes, geometry and expression references.
  Restored explicit boot/setup/pairing/question flows and the planned one-way voice
  journey. PR states remain ordinary activity information; battery examples use
  manga notices and the ten-second free-Home policy without numeric captions.
- Consolidated historical errata, addenda and changes into each document's final
  dated revision grid, preserving original page/screen references and a clearly
  labelled Security section. Each final atlas has 37 pages; Planning retains 24
  main reference pages and Navigation has 15 illustrated journey pages. All
  rendered pages received visual review, including an independent PDF reviewer.
  PDF generation and rendered-page visual review cover
  the documents only; audio implementation, firmware builds and physical behavior
  are not established by these illustrations.

## Current firmware installation - 2026-10-11

- Built and flashed clean source checkpoint `8cfb6ce3` through the public
  `scripts/device.ps1 flash -Profile cody-companion -Feature wake-word
  -BuildMode Debug -Port COM10 -Jobs 8` workflow with shared transient cleanup.
  ESP-IDF 6.0.2 produced firmware identity `cody-99-g8cfb6ce3`.
- The application is 6,990,864 bytes (`0x6aac10`); the build's partition check
  reported 49% free in the `0xd23000` application partition. No separate size
  command was run.
- COM10 was the present Espressif USB device; esptool identified ESP32-S3
  revision v0.2. Bootloader, partition table, wake-word models and application
  each completed write/hash verification, followed by the standard RTS reset.
  NVS was not a flash target, and no erase-flash command was issued.
- Operation exit code was zero. The complete build/flash log is
  `artifacts/build-cody-companion-wake-word-evidence/firmware-20261011-8cfb6ce3-flash.log`.
  No test suite, simulator, serial capture, post-reset UI observation or device
  stack-headroom measurement accompanied this operation. CCP-01/06/07 remain
  open for their remaining named checks and runtime evidence.

## Earlier verification - 2026-10-11

The results below precede the later 2026-10-11 Codex acting/power-cue increment.
Its later firmware build/flash is recorded above; these earlier test results
do not cover that increment. Its remaining execution gate is CCP-06 in TODO.md.

- Cody Debug wake-word built with ESP-IDF 6.0.2, eight jobs and shared cleanup.
  Identity `cody-86-ge2ae6191-dirty`; application `0x6a9800` bytes, with 49% of
  its `0xd23000` partition free. This operation did not flash/access hardware.
- Personality passed 1,142 native assertions, 122 parity frames and 118 host
  assertions. Runtime passed 500 response ticks, character contracts, shared
  source groups, event UI and settings. Studio's two native drivers compiled
  after personality/runtime provenance generation.
- CCP simulator passed 34 isolated cases; character Studio passed five cases
  exercising the real ctypes/C++ director and scheduled renderer in a withdrawn
  Tk window. Coverage includes gaze, recognized caress, coherent compaction end,
  source gaps and Debug isolation. Native captures are labelled synthetic.
- Stale fixture expectations were corrected for the project identity row,
  capsule icon/palette and presentation counting. The excluded technical-owner
  fixture still aborts if invoked; no firmware fix was required.
- Preserved results: `artifacts/build-cody-companion-wake-word-evidence/character-20261011/`,
  including `runtime-check-5.log`, `personality-check.log`, `ccp-simulator-check.log`
  and `character-studio-check-final.log`. Earlier failed attempts are retained.
  Physical gestures/rotations, complete panels and device stack minima remain open.

## Documents

| Date | Completed work | Evidence and limits |
| --- | --- | --- |
| 2026-10-11 | Consolidated Markdown into `docs/cody`, `docs/luna-shell`, and `docs/bsp` core/services/board references. Merged idle/behavior, power and build-mode fragments; rebased links and literal generator/check documentation paths. Rewrote task indices and compacted root records. | Static content/source review only. Source preservation `9956446`; no PDF content revision, test, scan, build, generator, simulator or hardware operation in this task. Unrelated code/asset changes remain separate. |
| 2026-10-11 | English correction of Italian historical titles, notes, captions and footers in Navigation, Planning, historical Service and Codex Connection PDFs, preserving original dates/screen IDs/artwork/evidence. Added protected translation mappings and public `cody-pdf-english`. | Navigation/Planning 53 pages; historical Service/Connection 33/37. Current 22-page Connection, 31-page Manga and four-page flyer unchanged. Rendered/visually reviewed; removed obsolete dual-UART ToF guide/assertions without changing sensor code. Document evidence only. |
| 2026-10-10 | Combined service/connection sources into the English 22-page CCP Connection Atlas with crosswalks; established paginated Date/Type revision grids and Security sections. Reauthored pairing quickstart as exactly four A5 pages. | Original flyer in Git was two pages; technical appendices had expanded it to 25. Current flyer has no appendices. Public generators plus Poppler visual review; no accompanying firmware/security execution. |
| 2026-10-10 | Updated 22 current pages, addenda and final corrections across six PDFs for boot, provisioning, retained pairing, Setup and preferences; public `cody-screen-revision`. | Reviewed 46 changed/added pages; 178 retained page streams/text unchanged. Historical lengths 50/50/25/32/36/31, superseded by later consolidation/correction. Sources in `artifacts/docs/cody-screens-20261010/`. |
| 2026-10-10 | Notification/decision atlases and retained-pairing/Options atlases revised with original-page preservation and dated errata. Planning notes folded into navigation. | 173 and 153-page visual reviews respectively; checkpoints `0834a496`, `eab8bf3e`. A QA parser was fixed to admit valid 40-42 ranges without altering PDFs. These counts are historical revisions. |
| 2026-10-10 | Automatic-context revision corrected earlier manual selection and retained errata. | Navigation/Planning reached 38 pages at that checkpoint; later revisions supersede it. Original documents retained in `2f7a5f53` after local cleanup. |
| 2026-10-06 | Manga/Navigation appendices captured mouth, Whistle, icons, gestures, idle and host audio; later VibeWare B01/B02 startup pages added. | Initial lengths 24/23, then 27/26; all prior 18/19 and later 24/23 streams preserved, added pages rendered/reviewed. Document review only. |
| 2026-10-04 | Ten-page English CCP Service Atlas, identity/security diagrams and creator/contact footer across five PDFs. | Ten new pages and 58 historical footers reviewed; retained bodies pixel-identical at 96 DPI. Later Connection Atlas is canonical. No LAN/device verification. |

## Companion and native character

| Date | Implementation/checkpoint | Evidence and remaining scope |
| --- | --- | --- |
| 2026-10-11 | Codex-only Thinking/Editing/Running accents, one attention double blink, exact-turn reply/interruption acting, and automatic canonical Cody low-battery/charging cues. Studio transports the same native direction/power owners through Python; no new host OS observer/service. | Static source integration only. Prepared native/Studio cases remain unexecuted; regenerate the driver and obtain fresh final build/check authorization. Compiled frames, physical timings/stack headroom and existing plugin Linux/macOS credential/launch support remain open. |
| 2026-10-11 | Consolidated the shared working-tree changes and character checkpoint history onto the single local `codex/cody` work branch. Restored the two approved action-layout PNG references still required by AGENTS.md and three document generators. Recorded a relative-cost badge proposal without implementing pricing acquisition or session-cost estimates. | Local Git/source work only; no push, PR, test, generator, build, simulator or physical operation in this consolidation. Existing standalone display/readiness behavior was retained rather than reverting to its older branch tree. |
| 2026-10-11 | Implemented the portable daily Codex Standard catalog cache, exact-model Economy/Balanced/Expensive/Unknown policy, original catalog age/local-midnight lease, CCP 0.6.4 authenticated TECH v2, canonical independent cost aging and the Model & Context badge with Studio transport parity. Added isolated cache/protocol regression cases. | Static source integration only; new cases, native compilation, firmware builds, simulation, package installation and physical behavior are unverified. Final validation remains CCP-07 in TODO.md. |
| 2026-10-11 | Added the persistent Home green/orange/red/gray Economy/Balanced/Expensive!/Unknown chip and current-model carousel slot. Shared canonical model/catalog admission and color/label definitions across Home, Model & Context and native Studio. Preserved attention/compaction priority, actual elapsed time and partial-frame damage; added labeled preview scenarios and expiry/raster/Studio regression cases. | Static source review only; prepared cases and compilation have not run. Native library regeneration, firmware/Studio verification, installation, stack frames/headroom and physical behavior remain pending in CCP-07; earlier test results do not cover this revision. |
| 2026-10-10 | Minimal native director: acquired-target gaze, positive-only happiness/30-s caress warmth, coherent compaction concentration and exact-end two-second release. Studio binds the same native owner. | Source review initially; generation/build/host cases completed 2026-10-11 above. No new sensor reads, inferred compaction, hook transport or screen-38 implementation. Physical timing/stack headroom open. |
| 2026-10-10 | Companion Home and Activity/Model & Context/Session & Link with persistent released-tap selection, deliberate-shake Home, attention arbitration and revision-qualified technical projections. Desk/Away strict persistence/Usage options. | Refined details place Cody at (233,115), scale 50, with four curated facts; Options retains five rows. Measured gesture projection precedes animation smoothing. Source review and later named host cases do not prove full physical panel interaction. |
| 2026-10-10 | Genuine compaction lifecycle and telemetry with unavailable/STALE states, current chat/revision coherence and approval priority. | Source integration; genuine reviewed hook delivery/turn correlation remains open. No transcripts, guessed context capacity, inferred CPU or fabricated source metrics. |
| 2026-10-10 | Sixteen Codex sounds and BootRise through the existing optional audio owner; `76f4114b`. | Authorized audio/runtime/definition generators completed: action PCM 140,800 bytes at 16 kHz, peaks -14 to -20 dBFS. Included in subsequent integrated flash; speaker audibility unverified. |
| 2026-10-10 | Native polish `db9047e`: vector action masks, rounded coverage, semantic palettes, whole-row Look and 4bpp-capable licensed-font generator. Cody boot title/subtitle and complete 42-frame floppy placement. | Driver compilation and boot-preview startup observed after rig provenance regeneration; 2bpp remains committed. Later Debug flash below. New 4bpp assets/quality and physical controls unverified. |
| 2026-10-10 | ABI-2 host batching `0973e0b`: one action raster pass/conversion pair; atomic bounded snapshot import/metadata projections. Production C++ CCP/presentation/standby/RTC/aging/JSON shared in `b60e38c`. | Initial static review; later runtime/drivers/CCP simulator executed within named scopes. Host performance/memory gain, broader simulator suite and physical rollback remain unmeasured. |
| 2026-10-10 | Facial catalog extended from ten to 34 independent typed controls; rounded lips/eyes, tongue clipping and canonical event reactions. `12d68bb`, `fcf68a5`. | Source integration and production host driver path; physical geometry/audio not established. |
| 2026-10-10 | English copy across 38 scenes: 20 titles plus fixed setup/event labels; screen 15 Ready to team up / Start a chat in Codex. | Authorized definitions-only generation retained glyph binaries; body widths 177/232 px in a 232-px slot. Physical copy review pending. |
| 2026-10-07 | Fourteen native manga masters repackaged as 256 x 256 indexed-alpha, payload 1,835,520 bytes, shared palette/host extent. | Personality passed 1,142 native assertions, 122 parity frames, 118 host checks and icon/audio contracts. Debug `2bd0626` built/flashed; physical artwork remains pending. |
| 2026-10-06 | Physical microtap, 20% larger symbols, 400-ms fades and bounded visible-symbol retries. Native host fake-audio owner plus unchanged logo/provenance/user-click links. | `0ea2613` built; `de9b8b18` built/flashed (4,027,456-byte app). DLL compiled; `03c1fa3` preview showed first frame after text-provenance normalization. No listening/physical review. |
| 2026-10-06 | Whistle target corrected from invalid 0.42 to authored 0.5 minimum; stronger 75% mouth closure/18% width breathing and thin lip/tongue alignment. | Rig regenerated; later `6ae36bda` firmware includes it. Initial formal/runtime checks failed before corrections; latest named runtime/personality results above supersede only their exercised scope. |
| 2026-10-05 | Session-only happiness, stable catalog IDs and eligible-zone caress; dormant disabled wellbeing model. Native/host petting and three-scene idle bag with 20-40-s waits, original bounded whistle. | Merged `355c7f04` build; focused personality passed 1,041 native assertions, 122 parity frames and 118 host assertions (32 petting/32 happiness/54 wellbeing). Physical petting/audio and dormant-model integration remain open. |
| 2026-10-05 | 300-ms caress thresholds, eye-tap cue variation, returning-device Affection/Joy and stronger mouth/idle behavior; `09ea3b70`. | Eight-job Debug build/COM10 flash, app 4,027,072 bytes, 73% partition free; three hashes verified, NVS pairing preserved. No host/device behavior observation accompanied this flash. |
| 2026-10-02 to 2026-10-05 | Cody rig/response/contour/sizing/rotation, asynchronous Look persistence, onboarding/teletype, clock/audio policy, screenshot/status and connectivity integration. | Dated original milestones/cause/evidence retained in `9956446`; current contract guides retain API/geometry. Host success/builds do not close race, freeze, audio or physical Alpha gates. |

## CCP and provisioning

| Date | Implementation/checkpoint | Evidence and limits |
| --- | --- | --- |
| 2026-10-10 | Dedicated security checkpoint `bbb11e10`: CCP 0.6.3 endpoint-bound challenge/COMMITTED, bounded firmware receive passes/ingress, PC discovery/PBKDF2/control work and rotating rejection logs. Retained in the current CCP 0.6.4 source. | Static source review. Update PC/firmware together; physical rollout/security tuning pending. Short-code offline guessing and unencrypted payloads remain; PAKE/encryption deferred. |
| 2026-10-10 | Authenticated REPLY nonce retained only after successful send; Studio separates FIND/READY/REPLY admission. | CCP passed 59 cases after correcting stale tool-catalog expectation. Regenerated driver plus CCP simulator passed 31 cases including nonce order, failed send and association retirement. Broker/loopback evidence only. |
| 2026-10-10 | Broker-restart unknown-question retirement, deadline-preserving Studio admission and guarded HTTP PSRAM response/read errors; `31665ca`. | Initial static review only. Current named CCP suites cover their admitted cases; actual HTTP contention and compiled stacks remain open. |
| 2026-10-10 | Review fixes `667242c`: truthful standby reassertion, separate resource transitions, terminal-question EVENTACK retention, stale RTC and SDK-owner recovery. | Source/component review; Release services/notification wake need current-image physical evidence. |
| 2026-10-10 | Semantic questions, read-only PR notices, recipient-confirmed delivery and distinct restart/factory-reset operations; integrated `e3f72b3c`. | 51 host CCP and 21 SIMULATED cases; runtime 500 ticks/26 vectors/settings faults; native/snapshot/power passed. Maintenance tested on PC/SIMULATED only. 173-page document review; whitespace and secrets passed for that increment (73 files/eight signatures). |
| 2026-10-10 | Recoverable pairing, independent pending/active stores, Cancel/Forget, strict Options and loopback SIMULATED endpoint. `147c7273`, `959931f3`, `3a76053d`, `f34056d`. | 36 CCP/11 simulator cases, runtime/settings, native/snapshot/power and 13 console cases passed. `ea0e3759` Debug wake-word app 6,819,200 bytes flashed; DIRAM 218,408/341,760 (63.91%). Physical Options/touch/audio open. |
| 2026-10-10 | Automatic latest-genuine-prompt context in CCP 0.4; no manual selection/tools/transcripts. `51b5d532`. | Installed 0.4.0 and retained broker/transport; Debug app 6,802,848 bytes flashed. No native hook trust/genuine chat proof or subsequent serial capture. |
| 2026-10-10 | Initial metadata broker/activity/audio milestone `da37aaac`, installed CCP 0.3.3 and native-error-preserving startup. | 22 final CCP cases, native/runtime checks and whitespace passed after using existing MSVC environment. App 6,802,464 bytes, 51% free, four verified flash images. Historic first status had no presence/chats; not current installation state. |
| 2026-10-10 | Hook parser diagnosis and packaging repair: canonical `.codex-plugin/plugin.json`; portable root manifest removed in source and installed 0.6.2. Native labels use statusMessage. | Twenty isolated packages compared on Codex 0.161.0 and 0.162.0-alpha.17.2; installed plugin/read and hooks/list return eight labelled hooks, no warnings. Config/hooks unchanged; hooks remained untrusted/modified. Discovery is not execution. |
| 2026-10-09 | Reversible setup, first-boot/retained CCP gates, Welcome escape, typed Options/Look, semantic cards/capsule and whole-control reveal. Python CCP preparation. | Source checkpoints and original branch starting points retained in `9956446`; multiple Debug build/flashes below. Physical navigation and policy gates remain open. |

## Device observations and failure causes

| Date / identity | Observed result | Qualification |
| --- | --- | --- |
| 2026-10-10, `ede5d53f` / `fb82f704` | Seven component-owned acquisition units compiled; image `cody-75-gede5d53f-dirty`, 6,879,520 bytes. Subsequent COM10 flash `cody-76-gfb82f704-dirty` verified writes. | GNSS-enabled branch excluded. User reports fast operation, without a measured benchmark. Matching logs under `artifacts/build-cody-companion-wake-word-debug/diagnostics/`. |
| 2026-10-10, `ab98f2c1` | Black screen traced to borrowed PCM addresses `0x3c6179dd`/`0x3c6198e3` lacking int16 alignment. Build-local two-byte assembly alignment corrected starts to `0x3c6179fe`/`0x3c619900`; app 6,881,200 bytes built/flashed. | Thirty-second reset capture reached renderer 3473 ms, Ready 4229 ms, completed logo 8507 ms and returning welcome 14036 ms; worker minimum free 3648/12288. Optional SD timeouts/missing pending key remain; no physical pixels/touch review. |
| 2026-10-10, `2b0804b1` | Requested UI wake-word upload, app 6,881,120 bytes, 50% free; four written hashes verified, NVS/LittleFS untouched. | This installation preceded the PCM correction; no separate tests/capture/physical inspection accompanied it. |
| 2026-10-10, `e3f72b3c` | 300-s reset capture: retained link, authenticated presence 14.139 s; standby 75.809 s; genuine diagnostic notice 106.287 s, notification wake 107.313 s, scene resume 110.778 s; expiry 285.045 s, standby 286.760 s. | No panic/new reset in window. Full/power stack minima 9648/12288 and 4940/8192. PC status connected in standby. No gesture/voice/audio/remote reset or physical UI review. App 6,847,456 bytes, 50% free; ELF SHA `475d53e915df60a47d1e878677695a1e227e99f3c9ddd6cb231918e10c488d84`. |
| 2026-10-10, `3cc10fcc` | Full acquisition two-snapshot copies caused a 9920-byte frame; 9120-byte owner scratch moved to PSRAM, frame 320 bytes. GATT snapshot projection reduced 5616 to 1328 bytes. | No stack enlargement; nested BLE/HTTP/Luna/headroom gates remain. Integrated into later observed image. |
| 2026-10-10, `579afee5` | Power-worker full snapshots produced nested 3904/3792-byte frames on an 8192-byte stack. Bounded projections reduced frames to 720/144 (864 combined); projections 552/80 bytes. App 6,803,296 built/flashed. | Capture interrupted by host USB closure at ~16 s after Connected; observed power minimum free 5820/8192. Whitespace passed; short window is not wake/stability proof. Later notification capture above covers its named route. |
| 2026-10-10, `3590f693` | `da37aaac` stopped scene 15 because a 22-character caption occupied 243 px in a 232-px slot; corrected text occupied 221 px, preserved strict layout, and fixed no-selection ACTIVITY parsing. | Ninety-second reset capture reached Connected and DISPLAY/TOUCH ready, then standby at 75.925 s without caption/consumer/panic failure. SD/I2S/logger/FIND diagnostics persisted; selected chat, physical gestures and audible cues not exercised. |
| 2026-10-09, `004d44d6` -> `4766df0b` -> `3458237e` | Yes/Show QR frame timeout; CPU0 priority-1 affinity alone then caused IDLE0 watchdog. Cooperative ten-ms PBKDF2 slices with one-tick waits retained 50,000 iterations and generated the code in 27.790323 s. | Ninety-second capture at `3458237e` had no frame stop/reset. User confirmed code but small text; code-font/180-s window revision `84e95216` built/flashed. Subsequent real pairing authenticated and user confirmed screen 15; later capture hit fifteen-FF touch report. |
| 2026-10-09, `669a33e9` | Fifteen FF bytes at 17.112 s after Yes stopped touch/rendering. Welcome escape and malformed-report isolation implemented. | Cause and renewed physical release/interaction remain open; old frame timeout and touch failure are separate observations. |
| 2026-10-07, `c3dceaf` | Matching `2bd0626` core identified indefinite LCD SPI bus acquisition during branding rows 464-465. Queued-only bounded QSPI and CST9217 ACK/normal-mode fixes built/flashed (6,697,616 bytes, 53% free). | 240 + 90 + 90-s captures, 75 successful periodic frames, no QSPI/watchdog/panic/rejected-touch event in windows. Owner confirmed Sophia wake with matching detection/DMA; physical pixel quality, touch wake and endurance remain open. |
| 2026-10-07, `7408ca5` / `b9cfcb8` | Explicit 400-kHz probing changed host-clock reconfiguration to once per attempt; memory-only SDIO exclusion also tested. ACMD41 still busy/OCR timeout; no mount. | Thirty-/45-second passive captures do not prove electrical waveforms. Official Waveshare baseline pinned at `e4344e70c2fa78a13e8a06566507f1ba8af6672a`; next evidence is VDD/CLK/CMD, not another unchanged reset loop. |
| 2026-10-07, `289dbe3a` | After confirmed reinsertion, mounted attempt 4 survived one full Debug standby/touch-wake cycle: writes 48 -> 96, same mount; owner confirmed wake and SD in Info. | Two 180-s passive captures; standby 550.380 s, wake request 615.405 s, write 49 at 615.788 s, active 616.385 s. No SD/runtime failure in these windows; warm-reset and Release behavior still open. |
| 2026-10-07, `10a7b8f8` / `289dbe3a` | Debug storage heartbeats/probe and truthful storage_resume reporting; storage/logger/snapshot checks and build/flashes passed. Reinsertion produced 126 verified writes; warm reset reproduced OCR before standby. | First 300-s run had no mounted writes; later UI touch/watchdog failures blocked standby in working-card run. No proof of a working-card standby failure or electrical cause. |
| 2026-10-07, `d4fafbca` | Corrected fixed-rate audio/AFE ownership after `cabb5f59` starved inference and rejected boot sample rate. App 4,967,648 bytes, sole Sophia model 291,168 bytes; four verified writes. | 180-s capture: boot/scene PCM at 16 kHz with continuous samples; standby 65.111 s, Sophia 67.990 s, wake 69.000 s physically confirmed. Second UART wake 139.305 -> 140.422 s lacks explicit utterance confirmation. First boot display watchdog and SD timeout persisted. |
| 2026-10-07, `6ae36bda` | VibeWare/non-TTS Cody Debug app 4,661,360 bytes, 69% free, three verified writes on COM10. | No monitor/capture or physical animation/acoustics claim accompanied this flash. |
| 2026-10-05, `160a080a` | Merged main rebuilt/flashed twice; app 4,014,496 bytes, 73% free, verified hashes. SIMD/DMA branches and worktrees removed after integration. | No physical UI observation with these flashes. Local/remote ref state was recorded at that date, not asserted current. |

## TTS and memory

| Date / checkpoint | Result | Evidence limits |
| --- | --- | --- |
| 2026-10-05 | Native pinned eSpeak integration, default-off normal builds, isolated SD four-language/flash-English variants, bounded UTF-8/cancellation and sole audio/storage owners. Diagnostic Italian boot phrase bypasses quiet policy only for that test; CPU0 synthesis/CPU1 rendering. | Source contracts in [TTS](docs/bsp/services/text-to-speech.md). Packages contain 827,224 English or 3,111,550 four-language principal bytes, excluding metadata/code/runtime; not linked/heap evidence. |
| 2026-10-05 | Authorized SD generation copied 12 files plus manifest to G:/LUNASHELL/tts/espeak-ng-data, 3,112,743 bytes, SHA-256 readback verified without formatting/unrelated changes. | Card provisioning does not prove mount or synthesis. No extra automatic speech triggers implemented. |
| 2026-10-05, `57121ca2` -> `58757005` | Initial built/flashed app 4,211,408 bytes rebooted before speech: native internal BSS blocked mandatory 8-KiB power task. Component-local PSRAM BSS and optional-after-mandatory startup corrected it; same app size built/flashed. | Thirty-second matching captures: early free internal RAM 88,703 -> 105,119 (16,416 recovered), mandatory startup/render complete and speech core0. SD times out twice; request 1 returns ESP_ERR_TIMEOUT before engine init. Audible Italian remains unconfirmed. |
| 2026-10-05, `61ab74a` | Debug PWR single-click delayed screenshot versus 500-ms double-click reset; app 4,212,592 bytes built/flashed with verified hashes. | Release excludes screenshot/shutter/software-reset gesture. No gesture/Release/corrected-image boot observation accompanied this flash. |
| 2026-10-02 to 2026-10-05 | Explicit PSRAM-only AFE/external BSS stacks, 64-KiB internal reserve; playback storage move 12,640 authored bytes. Best-effort logger/worker changes estimate ~68-KiB recovery. | Source estimates, not measured linked/heap recovery. TCBs/locks/DMA/NVS/flash-sensitive workers remain internal. Logger losses retain cause/timestamp; optional logger failures do not authorize reset. |

TTS evidence: `artifacts/build-cody-companion-tts-sd-debug/evidence/`, notably
`tts-black-screen-boot.log`, `tts-psram-fixed-boot.log` and
`tts-sd-reinserted-audio-20261005.log`. Reinsertion capture again returned OCR
timeouts at 4.676/23.004 s and speech timeout at 4.688 s; the user reported boot
sound/clicks, without an intelligible Italian phrase.

## Historical raster measurements - recorded by 2026-10-05

Fixed Ready/Pearl Mist, ESP32-S3, 40-s measurement windows unless stated.
These are historical workload/image comparisons, not current normal-UI performance.
Full original conditions, commits and filenames are in `9956446:done.md`.

| Workload | Frames / pixels per frame | Mean / p95 (us) | Result |
| --- | --- | --- | --- |
| C baseline `4ac6c812`, 100% | 684 / 63,000 | 56,922 / 57,611 | Max 58,605; all miss 40 ms. Geometry/raster/transfer 2,781/40,886/12,243; jelly/face 37,080/3,003. About 17-17.2 FPS, worker CPU 85-86%, free stack 3440/12288. |
| Fresh C control, 100% | 684 / 63,000 | 56,950 / 57,634 | Jelly 37,106; all miss 40 ms. |
| SIMD, 100% | 697 / 63,000 | 55,846 / 56,495 | Jelly 35,880; all miss. Versus fresh C: mean -1.94%, p95 -1.98%, jelly -3.30%. |
| SIMD 16-pixel zero skip | 699 / same route | 55,612 / 56,374 | Later coverage selected. |
| SIMD 64-pixel blocks | 652 / same route | 59,794 / 60,519 | Regression from fewer excluded transparent blocks. |
| SIMD row coverage, 100% | 766 / 63,000 | 50,715 / 51,463 | Jelly 30,736; all miss; 18,988 pixels/frame skipped before packing. |
| Exact `/255` shift/add trial | 752 / same route | 51,497 / 52,299 | Pixel parity retained but slower; change removed. |
| SIMD row coverage, 75% | 975 / 35,256 | 33,888 / 34,512 | Max 35,147; zero 40-ms misses. |
| SIMD row coverage, 50% | 975 / 15,600 | 21,834 / 22,335 | Max 23,179; zero misses. SIMD scale tests, not new C/SIMD A/B. |
| Combined source, one DMA slot | 753 / 63,000 | 51,542 / 52,139 | Max 52,726; raster 35,112, transfer 12,508. |
| `a01aed4`, two 12-row DMA slots | 842 / 63,000 | 45,828 / 46,667 | Max 47,607; all miss. 22,400 DMA bytes; raster 35,086, transfer 6,761. Mean/p95 improved 5,714/5,472 us. |

- Actual PIE verifier passed 65,536 weight pairs, 262,144 reference samples and
  30,267,144 live RGBA samples over 489 frames without mismatch in the second
  80-s capture. First capture interrupted by touch at 229 frames; parity is not speed.
- Intrusive 100% profiling across 508 frames raised jelly mean to 57,052 us;
  preparation/interpolation/composition segments 19,673/6,947/19,216 rank work
  only, with unassigned instrumentation time. These are not normal FPS costs.
- Native raster/SIMD arithmetic/actual-BSP DMA ownership/allocator contracts passed.
  `0883de76` linked S3 assembly after a WinError 216 host-toolchain attempt;
  corrected suite passed with MSVC. `e714737` combined Debug/Release builds passed
  without flash/timing. `6261cdb2` normal Debug/Release builds passed; only Debug
  flashed. Its 95-s capture reached ready display/touch/radios and clean standby
  with two 8-row startup slots, without physical pixels/touch/endurance review.
- Former preserved branch reports were later removed by cleanup. Their measured
  values remain here; merged-main normal-route performance is still an active gate.

## Platform and other products

| Date | Completed implementation/evidence | Qualification |
| --- | --- | --- |
| Recorded by 2026-10-10 | 82 owned source/header files reorganized by responsibility, CMake/source contracts aligned, short component READMEs; previous 13 Cody guides consolidated into five. | Static integration record, not universal validation of later modules. Public interfaces/components/vendors preserved. |
| Recorded by 2026-10-10 | Standalone display/touch separated from LVGL; one public device console, deliberate COM choice, native error causes, bounded jobs, inert help/menu and artifact containment/cleanup. | Current console/composition edge cases stay in TODO; ordinary flash success does not cover them. |
| Recorded by 2026-10-10 | Canonical acquisition/validity/CPU/RTC, lazy Brookesia documents, guarded SD formatting, calibration, transactional rotation/standby/Performance Mode, opt-in HTTP and FFT UI. | Source support does not complete official 0.8 integration, other-profile locks, memory/physical gates or product controls. |
| Recorded by 2026-10-10 | Offline boot/previous SD logs and CRC/revision USB readback; frame deadlines/watchdog/DMA leases; 12-s liveness/breadcrumbs and explicit SDK-worker recovery. | Forced-stall, passive serial/readback, ownership and stack matrix remain open. |
| 2026-10-04 | Host snapshot-integrity, cody-native/runtime, cody-response (15) and cody-poc (32) passed at their checkpoints; asynchronous persistence faults covered. | Later edits, UI races, rendered physical output and electrical behavior not covered. `0295963d` later built/flashed 3,550,736-byte Debug app without leak/UI observation. |
| 2026-09-27 | Leg five-page measured IMU UI, saved motion/sound, boot/movement cues. Serial evidence: deliberate level 3, no table-repeat, qualified motion wake, ~25-s display-off. Verified COM8 firmware/LittleFS writes. | Playback ESP_OK is not audibility; latest pixels, levels 1/2, rotation/persistence/PWR and disabled-RTC error summary open. Firmware SHA `69C47732DAC8CD712F71767F16994B52AC2CCCFD0D37A174E66BD7032E7AD492`; LittleFS `8282F023B283A4792C22C17B35287C0B96CE6B9701C3BC2A6C85A6BD0996FCC5`. |

Current task entry points: [Cody](docs/cody/README.md),
[Luna Shell](docs/luna-shell/README.md), [BSP](docs/bsp/README.md),
[public console](scripts/README.md) and [simulator](simulator/windows/README.md).
