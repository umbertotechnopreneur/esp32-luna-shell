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
