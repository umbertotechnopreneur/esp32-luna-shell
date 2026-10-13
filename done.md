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
