# Completed implementation and evidence

This is a compact checkpoint record, not a release certificate. Open validation
and implementation gaps remain in [TODO.md](TODO.md); deferred work is in
[planned-todo.md](planned-todo.md). Historical revision details remain in Git
and referenced architecture records. Paths under ignored `artifacts/` directories
below are historical provenance only: the explicitly requested full local cleanup
on 2026-10-05 removed the earlier untracked evidence before the current build and
flash logs were regenerated.

## Power-task boot/wake crash diagnosis and correction - 2026-10-10

- The requested passive COM10 capture on `cody-32-g51b5d532-dirty` contains three
  `LUNASHELL_power` stack-overflow panics. In the third, standby completed at
  79260 ms, wake began at 131096 ms, and `phase=awake` reported ESP_OK at 132026 ms
  immediately before the panic and software CPU reset. This is a real reset
  following software standby, not intentional deep-sleep restoration.
- The captured ELF prefix `63f852cac` matches the preserved ELF SHA-256
  `63f852cacbb8e4171b16484530fdd7a68f4730da4fb21f7369c65c1102adcada`.
  Offline core decoding confirms the overflowing task. Its unwind stops at the
  corrupted stack, so it does not identify the exact application instruction.
  Object disassembly independently shows 3904 bytes for `power_task` and 3792
  for its nested `active_display_snapshot_diverged`: 7696 bytes before deeper
  service/logging calls on an 8192-byte stack. Each full snapshot is 3728 bytes.
- Replaced those copies with narrow canonical power inputs and applied-display
  observations. The latter is still read after wake, rather than reusing an old
  pre-wake observation. Aging/timestamps, lock failures and the fatal canonical
  read invariant are retained. Compile-time size budgets and bounded Debug
  minimum-free-stack logs make future growth observable. Static review complete.
- Checkpoint `579afee5` built with eight jobs and flashed on COM10 successfully;
  app `cody-34-g579afee5`, 6,803,296 application bytes, four verified image hashes.
  New compiled frames are 720 bytes (`power_task`) and 144 bytes (display helper),
  down to 864 combined. The actual projections occupy 552 and 80 bytes. Matching
  installed ELF SHA-256:
  `348b002bcd61f2132feb6ce844810813eb292e02583169301c1fd486262aabc5`.
- The separately authorized `git diff --check 9122c561 HEAD` passed at `579afee5`.
  The authorized 300-second reset capture was interrupted by Windows closing
  COM10 at about 16 seconds (exit 1). Before the loss it recorded Connected at
  14196 ms, authenticated activity=None, and power minimum-free stack 5820/8192
  bytes. No captured panic/reset followed boot in that short interval. Windows
  subsequently listed COM10 present/OK and CCP reported connected=true. The
  cause of USB closure and post-fix standby/wake remain unverified; no stability
  claim covers the missing capture interval. Raw log:
  `artifacts/logs/cody-power-stack-fixed-20261010.txt`.
- Raw captures, the matching ELF and three core dumps are retained locally in
  `artifacts/build-cody-companion-wake-word-debug/diagnostics/power-20261010/`.
  Core memory is private diagnostic material, not a repository artifact.

## Automatic CCP context - 2026-10-10

- Implemented CCP 0.4 automatic following from the latest genuine UserPromptSubmit
  hook. Background chat events cannot steal context; reordered older prompts do
  not reverse the selection. All devices paired to the PC mirror that context.
  Removed manual select/clear MCP tools and updated manifests, skill, protocol
  and README. Labels are bounded ASCII workspace text; full session digests retain
  identity. No prompts, replies, transcripts or tool arguments are added to storage.
- Changed the shared screen 15/idle/ended caption to `Waiting for Codex`, 17
  characters/188 pixels in the existing 232-pixel slot. Layout guards remain intact.
- Prepared new isolated host regressions. Static source review is complete;
  final host/PDF checks and actual hook/display delivery remain pending.
- Authored the requested Navigation and Planning main-page consolidation, retained
  unrelated pages and previous errata, corrected the old manual-selection addendum,
  and appended integration notes plus final dated errata (38 pages each). PDF
  content/history checks and Poppler visual review are prepared, not yet executed.
- Installed CCP 0.4.0 through `codex plugin add cody-codex-plugin@cody-local --json`.
  Replaced the identified manually started 0.3.3 processes with the installed 0.4.0
  broker and a retained MCP transport. This is process installation/startup evidence,
  not proof of native hook trust/autoload or a genuine observed chat.
- Eight-job Debug wake-word build and COM10 flash succeeded at firmware checkpoint
  `51b5d532`; app version `cody-32-g51b5d532-dirty` reflects the uncommitted PDF
  authoring work only. Application: 6,802,848 bytes; four image hashes verified;
  final RTS reset. Log: `artifacts/logs/flash.log`. No follow-up serial capture
  was authorized or run. The shared pre-flash cleanup removed older raw captures
  and PDF staging; their original documents remain in Git at `2f7a5f53`.

## CCP chat activity and requested SFX - 2026-10-10

- Follow-up on the reported boot crash: passive 90-second COM10 capture and a
  separately authorized 90-second reset capture completed on `da37aaac`.
  The reset log recorded Wi-Fi/IP at 15905 ms, looking-for-pc at 15941 ms,
  ACTIVITY rejection at 16112 ms, `Caption rejected: length=22 width=243 top=327`
  at 16113 ms and `Scene request failed; consumer stopped: ESP_ERR_INVALID_SIZE`
  at 16114 ms. The caption exceeded the 232-pixel slot; background services
  continued without a captured panic/reset. Logs:
  `artifacts/logs/cody-boot-crash-20261010.txt` and
  `artifacts/logs/cody-boot-crash-reset-20261010.txt` (transient cleanup may remove
  them after these decisive observations are retained). The existing SD timeout
  also remains present; it is not the cause of this caption rejection.
- Corrected the shared selection instruction to `Pick a chat in Codex` (20
  characters, 221 pixels in the actual 12-pixel/11-advance body font), including
  session-ended and idle paths. The ACTIVITY no-selection project sentinel now
  has its literal hyphen first in the scanf set; added secret-free parse and
  authenticated-state logs alongside the existing canonical publication. Static review
  completed; subsequent installation and observation are recorded below.
- The first follow-up build stopped before flash because diagnostic code at
  `783bc3b` assigned the void `reportCodexLink` return. Corrected that call to
  match its API, whose owner logs publication failures; acceptance logs claim
  authentication only. The failed attempt did not change device flash.
- Corrected image `3590f693`, `cody-30-g3590f693`, built with eight jobs and was
  flashed on COM10: 6,802,848 application bytes, four verified image hashes and
  final RTS reset. The shared helper performed pre-flash cleanup, removing the
  preceding raw diagnostic captures after their decisive evidence was retained
  above. Flash log: `artifacts/logs/flash.log`.
- The freshly authorized 90-second reset capture on `3590f693` completed with
  exit 0: `artifacts/logs/cody-boot-fixed-20261010.txt`. It recorded authenticated
  ACTIVITY state=0/selected=no at 14193 ms, route Connected at 14203 ms and
  DISPLAY=ready/TOUCH=ready snapshots at 34092 and 64105 ms. Standby requested at
  74248 ms, UI quiesced at 75059 ms and standby entered with ESP_OK at 75925 ms.
  No caption rejection, consumer stop, ACTIVITY rejection, panic or unexpected
  reset appeared in this window. The early retained entry-failed trace belongs
  to the prior boot; this run's standby completed successfully. No extra host
  test suite was run for this narrow follow-up.
- Separate diagnostics remain visible: the existing SD initialization timeouts,
  logger truncation/loss reports, two I2S-disable messages during startup and one
  CCP FIND send errno=118 during radio suspension at 75557 ms. These did not stop
  the observed route or standby transition. Actual gesture/wake behavior, audible
  output and a selected chat's transitions were not exercised by this capture.
- Implemented a single on-demand Windows broker with DPAPI-authenticated loopback
  control, independent MCP leases, explicit observed-chat selection and metadata-only
  Codex hooks. The existing pairing identity is preserved; chat selection is volatile.
- Extended CCP1 with nonce-bound ACTIVITY snapshots, canonical device validity/error
  state, screens 17-20/32 and 18U unavailable. Fast tool details do not restart the
  headline typewriter. Setup re-entry restores the saved-link flow; cues follow
  semantic changes without replaying previously announced status.
- Added a 1.8-second boot cue and ten short Codex cues, with provenance for four
  owner-supplied library samples. The existing audio worker handles independent
  presentation gating, quiet hours, volume, standby and nonqueued admission.
- Revised all five atlases by appending the workflow and a final dated errata page;
  previous pages remain intact. Generated pages were rendered for visual authoring
  review. Scene definitions and audio assets were regenerated through device.ps1.
- Focused checkpoints include f95b982, 66318e8, 603daa0, 4dfe1f7, e7425ff,
  b58a8ee, e987c54 and the integrated firmware source da37aaac. Authorized branch
  pushes through da37aaac succeeded; the first push was also checked remotely.
- Final eight-job IDF 6.0.2 Debug wake-word build and COM10 flash succeeded at
  `da37aaac`, firmware `cody-26-gda37aaac`. Application: 6,802,464 bytes; 51% of
  the app partition remains free. All four written images were hash-verified and
  esptool performed the final RTS reset. Shared transient cleanup ran before both
  operations. Current installation log: `artifacts/logs/flash.log`. Physical UI,
  audio, standby and activity delivery remain unobserved for this image.
- Authorized host checks passed: CCP initially 19 cases and, after the PC startup
  correction and fresh approval, all 22 cases; cody-native portable C++/geometry
  contracts; cody-runtime contracts. The first native/runtime attempts stopped
  before compilation because the host compiler was absent from PATH. Freshly
  authorized repeats passed with the existing Visual Studio Insiders x64 developer
  environment. Reports: `artifacts/checks/cody-native/report.json` and
  `artifacts/checks/cody-runtime/report.json`; final CCP log:
  `artifacts/logs/check.log`. The pre-flash `git diff --check` also passed.
- Local marketplace registration and CCP 0.3.3 installation succeeded. Startup
  through the installed STDIO entrypoint now launches the shared hidden broker.
  Connecting to the unowned loopback control port timed out at both 250 ms and
  two seconds without starting a broker. Startup now probes exclusive port
  ownership; only an occupied-address error is handled as an existing owner,
  and other failures retain their cause. The child re-acquires ownership before
  protected-state access. Three isolated regression cases cover these outcomes.
- The old manually launched 0.2 owner was stopped before starting 0.3.3. MCP
  initialize returned version 0.3.3; status retained one pairing record, with
  connected=false at that observation, and listed zero hook-observed chats.
  Hook trust/autoload must be reviewed in Codex before selecting a real chat;
  no trust records or synthetic live events were written. Native autoload,
  multi-chat lifetime, reconnect and physical behavior remain open. No new
  serial capture was executed for this milestone.

## Cody action language, Options and touch diagnosis - 2026-10-09

- On 2026-10-10, the user's explicit connection request completed real LAN
  commissioning with the installed CCP 0.2.0 service and Cody at 192.168.1.19.
  `ccp_discover_devices` returned one offer; `ccp_pair_device` returned paired=true
  after two-sided confirmation. After the user pressed Let's go and reported
  Connected, `ccp_connection_status` returned connected=true with authenticated
  last-seen evidence. This verifies the cooperative firmware verifier interoperates
  with the PC for this pairing and confirms the observed route to screen 15.
  No display codes, proofs or keys are retained here. The enabled plugin's tools
  were absent from this chat, so its installed STDIO service was launched manually
  through the Codex terminal session. Native plugin autoload/lifetime, reboot
  rediscovery, loss/recovery and multi-device behavior remain unverified.
  This is commissioning/presence evidence, not a selected chat or task transport.
- The presentation/standby follow-up is installed at `84e95216`, firmware version
  `cody-16-g84e95216`: the authorized eight-job Debug wake-word build and COM10 flash
  succeeded, with four verified image hashes and final RTS reset. Application
  bytes: 6,714,640. Source definitions were regenerated through
  `scripts/device.ps1 generate -Generator cody-scenes -ToolArguments --definitions-only`;
  glyph assets were unchanged. Build/flash log: `artifacts/logs/flash.log`.
  Mandatory pre-flash transient cleanup removed the raw successful-generation
  capture after its decisive timestamps/results were retained below. No fresh
  capture/test was run for the larger code, wait message or three-minute override;
  their physical appearance and timed standby remain open.
- On 2026-10-10, `3458237e` / `cody-15-g3458237e` built and flashed on COM10:
  6,714,112-byte application, four verified image hashes and final RTS reset.
  The explicitly authorized 90-second reset/capture completed with exit 0:
  `artifacts/logs/cody-connect-codex-cooperative-kdf.txt`. Yes at 29766 ms entered
  Connect Codex; derivation started at 29769 ms and completed all 50,000 iterations
  at 57560 ms with ESP_OK, elapsed_us=27790323. The pairing window and display code
  were ready, and the code-copy route entered at 57621 ms. No watchdog/reset or
  frame-stop error was observed in the captured window. User confirmed the code
  appears but is small; button appearance and PC proof compatibility remain open.
- Implemented the reported readability follow-up: screen 12 adds Please wait /
  About 30 seconds during preparation, reflecting this capture's approximate
  duration. The six-character code uses the existing antialiased 40-pixel title
  font instead of the 25-pixel body font, cyan and centered at y=338 (height 40,
  width 109). It appears whole after the instruction and contributes no typewriter
  audio/count. These bounds remain within the content slot and safe square.
  Static review and build/flash completed as recorded above; physical review
  remains pending. No new tests or capture were run for this follow-up.
- Screen 12 now declares a 180-second standby override. Canonical successful code
  delivery refreshes it once, preserving three minutes from code-ready presentation
  without fabricated touch events, preference writes or a changed CCP expiry.
  Unchanged snapshots do not repeatedly extend it; existing touch and departure
  handling remain. Source-reviewed; timed physical behavior remains unverified.

- Follow-up on 2026-10-10: `4766df0b` / `cody-14-g4766df0b` built and flashed
  successfully on COM10 (6,718,384-byte application, four verified image hashes,
  final RTS reset). The authorized 90-second reproduction
  `artifacts/logs/cody-connect-codex-priority-fix.txt` recorded Yes at 15813 ms,
  derivation start at 15845 ms on CPU0/priority 1, then IDLE0 watchdog at 28015 ms
  and restart. No display timeout was observed before that watchdog. User reported
  reset at Preparing secure code. The first affinity change was insufficient.
- Implemented cooperative PBKDF2-HMAC-SHA256 using official PSA HMAC calls: the
  original single 32-byte block, six-byte code, sixteen-byte salt and 50,000
  iterations are retained. Ten-millisecond slices block one tick without holding
  service/snapshot/PSA operation locks, admitting IDLE0 as well as higher workers.
  Output is published only after successful key cleanup; intermediates are wiped,
  real computation/cleanup failures remain visible. Source review only at this
  checkpoint; PC verifier compatibility and physical recovery are not yet proven.
  Requested phase and 5,000-iteration progress logs omit all code/salt/key values.
- Updated onboarding and skin controls to appear as complete buttons/cards only
  after title/main-line typing. Their labels do not extend narrative typing or
  typewriter audio. Drawing and touch share the visibility gate; existing action
  admission, layout geometry, gradients/icons and lateral Back stay integrated.
  Its build/flash and code-generation capture are recorded above; physical button
  appearance remains pending.
- The required pre-flash cleanup removed the earlier timeout/watchdog reproduction files
  after its decisive timestamps/errors were retained above and in TODO.md.

- Completed the newly authorized 90-second COM10 reset/capture on installed
  `004d44d6`: `artifacts/logs/cody-connect-codex-freeze-aa.txt`. Yes at 21562 ms
  entered Connect Codex; at 22865 ms the complete-frame deadline failed after
  four rectangles and stopped rendering. No malformed-touch failure or panic
  was observed in this reproduction. The user confirmed the freeze during capture.
  SD initialization/retry timeout remains concurrent and separately unresolved.
- Static review found CCP's monolithic 50,000-iteration PSA PBKDF2 running at
  priority 3 without affinity, above the priority-2 CPU1 renderer. Corrected the
  scheduling contract: CCP background work is priority 1 pinned to CPU0, leaving
  acquisition/audio/radio workers above it and isolating native frame deadlines.
  Added secret-free derivation duration/core/result and failed-frame stage timing.
  The observed deadline failure is confirmed; scheduling causation and corrected
  physical behavior still require fresh evidence. No timeout increase or discarded
  error, protocol/KDF-cost change, driver reset or saved-data repair.

- Implemented the subsequent requested antialiasing refinement: compile-time
  4x4 corner coverage for all current outer/inner radii, reconstructed 24x24 icon
  masks, concentric two-pixel antialiased borders and per-row gradient weights.
  Current static controls share immutable flash masks without per-frame sampling,
  allocation or a full framebuffer. Existing text coverage, tile byte order,
  stride, rotation ownership and hit bounds are retained. The user's ten-pixel
  suggestion is an optical indication: card icons move down ten pixels, ordinary
  capsule icons six and compact/Options icons four. Two-line captions move down four pixels to
  preserve separation. Compact footer icons remain inside y=[390,434).
  Static source review completed before the authorized installation command:
  `scripts/device.ps1 flash -Profile cody-companion -Feature wake-word -BuildMode
  Debug -Jobs 8 -Port COM10`. Shared transient cleanup ran first. Build and flash
  completed successfully at `004d44d6`, version `cody-12-g004d44d6`, with four
  written-image hashes verified and final RTS reset. Application size reported by
  the flash build is 6,717,760 bytes. Evidence: `artifacts/logs/flash.log`.
  Sophia and prerecorded audio retained. No tests, simulator, standalone size
  report or post-install serial capture were run; physical appearance is pending.

- Completed both requested 90-second captures on COM10: passive current-state
  `artifacts/logs/cody-ccp-freeze.txt` and explicitly authorized reset/reproduction
  `artifacts/logs/cody-ccp-freeze-boot.txt`. The latter recorded Yes at 16497 ms,
  Connect Codex at 16498 ms, then fifteen FF CST9217 bytes / ACK=FF (expected AB)
  at 17112 ms. ESP_ERR_INVALID_RESPONSE stopped the display worker while LAN/IP
  was ready. No panic/watchdog reset was observed in that capture. The underlying
  FF report cause remains open; existing SD timeout is separately unresolved.
- Implemented the documented optional runtime malformed-acquisition policy:
  preserve the touch owner's real error and canonical invalid touch/body state;
  discard queued gestures, cancel hold/swipe/caress, wait for an actual release,
  and continue display completion. An acquisition ESP_ERR_INVALID_RESPONSE can
  also mean I2C NACK; the policy does not infer hardware origin. Startup, other
  acquisition errors, locks, capacity and route errors remain terminal.
  No fabricated release, sensor fallback or controller
  reset. Static review only at this implementation checkpoint.
- Generated four setup and four single-action ImageGen proposals. User approved
  the pair of rounded gradient cards with centered Options below, and the single
  icon-left capsule. Preserved all eight and both approved images in
  `docs/Cody/ui-proposals/20261009/`, with single-action prompts/tool provenance.
  Added the continuing two/three/single-action rule to the sole root AGENTS.md.
- Native onboarding actions now share dark gradients, semantic borders, white
  labels and typed flash-backed icons; Yes/No and Network/Later are horizontal.
  Ordinary single actions use 240x72 capsules; QR and the tall skin list use
  the 220x44 variant. Separate validated content/action slots retain clipping
  and retired-frame damage without borrowing width from the circle's bottom arc.
  Options opens 10 / Make it yours, 36 and 11 / skin, then returns to Setup.
  Back retraces this context. QR/footer separation and existing saved data remain
  intact. Regenerated scene definitions without modifying the ASCII glyph assets.
- Appended three pages to each requested PDF: approved UI references and the
  important decision, Options/touch explanation, final dated Errata and corrections
  acknowledging the original analysis gaps. Preserved every preceding page content
  stream and earlier errata. All fifteen added pages were rendered and visually
  reviewed. Navigation/Planning 34 pages, A5 Quick Start 11, CCP Service 18,
  Codex Connection 22. No firmware tests/scans/simulator or corrected-device input
  validation ran for this checkpoint; installation evidence follows separately.
- First authorized installation build stopped before device writes at an obsolete
  skin-scrollbar palette reference. Corrected that reference to semantic teal;
  no warning suppression. The acquisition INVALID_RESPONSE/NACK qualification
  was also made explicit in source comments and all five revised flow pages.
- The repeated authorized Debug wake-word installation completed with eight jobs
  and COM10 flash at source `010f76e0`, version `cody-9-g010f76e0`. Four image hashes
  were verified and RTS reset completed. Sophia and prerecorded audio were retained;
  no NVS/LittleFS/full-device erase. Application: 6,705,536 bytes, SHA-256
  `94421604c3e85d397cd126f488a11d8c9b4533ee633564a18a605dfeb12d3511`.
  Evidence: `artifacts/logs/flash.log`. The shared required cleanup removed temporary
  PDF renders/staging and earlier captures after recording their findings; tracked
  PDFs and approved images remain. No post-install capture, simulator, tests or
  validation scans ran. Physical layout/input/standby and FF hardware cause remain open.

## Cody setup escape correction - 2026-10-09

- The explicitly authorized 90-second passive COM10 capture completed without
  reset. It recorded station DHCP success at device log time 519235 ms, the
  onboarding inactivity return to Greeting/Welcome at 534964 ms, and standby
  entry at 566626 ms. No Connect Codex press/menu activation was captured; the
  initial reported wrong selection remains open. Capture provenance:
  `artifacts/logs/cody-connect-routing.txt` (transient, removed by later build cleanup).
- Static source review identified the concrete escape failure: timeout Welcome
  touch immediately entered Wi-Fi invitation while character holds excluded
  Welcome/05/06. Welcome stationary tap release now opens Setup; resting 04/05/06
  admit character hold with all worker/provisioning/cancellation gates retained.
  A wake-spanning contact starts a fresh 1500-ms hold only at visible eligibility.
  Logs record capsule coordinates and named action, never QR payloads/CCP codes.
- Appended recovery explanation and final dated Errata and corrections to each
  requested PDF, preserving every preceding page content stream and earlier
  errata. Navigation/Planning now have 31 pages, A5 Quick Start 8, CCP Service 15,
  Codex Connection 19. All ten added pages were rendered and visually reviewed.
- No firmware tests, validation scans, simulator or corrected-device input
  verification accompanied implementation. Installation evidence is recorded
  separately; corrected physical behavior remains in TODO.md.
- The user-authorized installation then completed the Debug wake-word build
  with eight jobs and COM10 flash at source `669a33e9`, project version
  `cody-6-g669a33e9`. Four images were hash-verified and RTS reset completed;
  no NVS/LittleFS/full-device erase was requested. Application: 6,701,136 bytes,
  SHA-256 `a6b4af8f96f8468dfceff08340896aeb8a1dca75d20f8b779aef66691abc244a`.
  Evidence: `artifacts/logs/flash.log`. Required pre-flash cleanup removed PDF
  staging and the earlier passive capture after their findings were recorded.
  No post-install capture/input test was run; physical behavior remains open.

## Cody Debug firmware installation - 2026-10-09

- User explicitly requested installation on connected Cody. The single present
  Espressif USB serial device was COM10. Retained the existing Debug wake-word
  variant with Sophia and prerecorded Cody audio; used ESP-IDF 6.0.2 and eight
  Ninja jobs through the sole public `scripts/device.ps1` entrypoint.
- The first flash stopped before compilation because the current partition-table
  hash differed from the older build state. Compared the previous table and
  current source: NVS remains `0x9000 / 0x6000`, LittleFS remains
  `0xe33000 / 0x100000`. The model partition grows to 1 MiB by shrinking the app
  allowance; neither persistent-data location moves. Public `set-target` then
  regenerated the selected configuration; no safety gate was bypassed.
- The repeated public flash command completed a full Debug build and wrote
  bootloader, partition table, the three-model image and the application. Esptool
  verified each written image and requested an RTS reset. No full-device erase,
  NVS/LittleFS write, serial monitor/capture or physical UI/pairing test ran.
- Installed source: `09fd405b`, project version `cody-4-g09fd405b`. Application
  binary: 6,700,432 bytes; SHA-256
  `fc690b93f5b4c671abb6eccfbf7cea00cd0db2ca099300c3276e05a58f0eea87`.
  Build/flash evidence: `artifacts/logs/flash.log`. Runtime checks remain in TODO.
- The required shared pre-flash artifact helper removed the reviewed PDF staging,
  originals and PNGs after document QA. All five revised versioned PDFs and their
  original pages/errata remain preserved; earlier originals are also in Git.

## Cody startup admission and reversible setup implementation - 2026-10-09

- Revised all five requested repository PDFs with the startup/setup correction
  and a final dated Errata and corrections page explicitly naming the original
  analysis gap. Preserved previous pages/errata and added installation correction
  to the A5 quick start. Page totals: Navigation 29, Planning 29, Quick Start 6,
  CCP Service 13, Codex Connection 17. All 16 added pages were rendered and
  visually reviewed before the later authorized pre-flash transient cleanup.

- Closed the original analysis gap: `cody_firstboot=false` no longer skips an
  absent Codex association. Normal startup uses canonical Wi-Fi/IP and the valid
  retained `ccp_pairing` record, with no redundant paired flag or Internet probe.
- Added stable scenes/screens 40 (Connect Codex? Yes/No), 41 (Setup menu) and 42
  (network wait). No/Later leaves native Idle without persistence/backend action;
  saved pairing adopts its exact attempt and requires authenticated rediscovery.
- Added a 1500-ms stationary character hold with movement, sample-gap, rotation,
  lifecycle and busy-route gates. Setup reentry preserves saved state. PWR/BOOT
  keep their original standby/capture/restart policy; no factory erase was added.
- Regenerated scene definitions through the public generator while retaining
  committed ASCII fonts. Static review covered mailbox/attempt ownership,
  typewriter updates for both choices, startup/standby and settings boundaries.
  No tests/scans, firmware build/size/configure, simulator or device operation ran.
  Required runtime/physical verification remains in TODO.md.

## Cody branch starting points and onboarding source review - 2026-10-09

- Local and remote tag listings were empty. Created annotated local tags
  `base-display-touch` at `3283a194` from `codex/base-display-touch`,
  `luna-shell` at `369e5026` from `codex/luna-shell`, and `cody` at `9d2fe8c5`
  from the current clean `main` / `codex/cody` starting point. Opened
  `codex/cody-onboarding` from `cody`; no refs were pushed.
- Static source review found the continued 09 -> 10 -> 36 -> 11 -> 12 -> 13
  -> 16 -> 14 -> 15 route, stable skin persistence and the existing CCP plugin.
  Screen 10 currently lists preference categories without editing controls;
  screen 15 represents authenticated plugin liveness, with chat/message/task
  transport still deferred. Recorded these boundaries in the root README.
- No firmware or plugin implementation changed. No tests, validation scans,
  configure/build/size, simulator, plugin execution or device operations ran.
  Existing open checks and physical evidence requirements remain in TODO.md.

## CCP Python integration preparation - 2026-10-09

- Followed the unchanged ten-page `docs/cody/cody-ccp-service-atlas-v1.pdf` and
  extended the existing Python MCP service rather than adding another owner.
  CCP remains discovery, display-code commissioning and authenticated presence;
  Codex chat/task/message transport remains deferred.
- Added a repository-local marketplace catalog, CCP display branding and
  synchronized 0.2.0 package/server versions. Both installed MCP entrypoints use
  the plugin-root path and Python `-B`; no user Codex configuration was changed.
- Acquired the exclusive UDP listener before protected-state access, retained
  real startup/receive/shutdown failures and rejected duplicate/invalid persisted
  fields without repair. Commissioning no longer invents presence; only verified
  FIND/READY starts the ten-second monotonic PC liveness lease.
- Prepared host regressions behind `scripts/device.ps1 check -Check cody-ccp`
  for source packaging, denied/corrupt state, listener ownership/failure and
  proof/nonce checks, two-sided confirmation and presence versus persistence/clock
  correction. Static source review
  only: suite execution, installation, LAN/device observation and all firmware
  validation remain pending. No PDF was revised and no refs were pushed.

## Requested recognition-only voice selection — 2026-10-07

- Added the pinned Espressif Jarvis and Computer WakeNet weights beside Sophia,
  with per-file provenance, a 1 MiB model partition, exact three-model inventory
  admission and Sophia as the sole active AFE model. No settings or user-facing
  selector were added. Implementation and static source review only; model
  packing, build, flash and physical behavior remain unverified for this change.
- Explicit probing-clock preparation at `7408ca5` compiled with eight jobs and
  flashed on COM10; all four hashes verified. App 6,695,168 bytes, SHA-256
  `CDBCFC33D960A83DDA335DF619147FC5BEFF74353EDBD86EFFCB5310C02FC0BF`;
  preserved matching `7408ca5.elf` SHA-256
  `B40E4042D08C721F693EC19A435B21AF39B16EFF1BF88C9C2078AC08E76259B3`.
  The 30-second passive capture completed without reset, spanning uptime about
  27.168-56.650 seconds. The observed third attempt selected/calculated 400 kHz
  and reconfigured the host clock once, instead of before every command, but
  ACMD41 still returned `40ff8000`; no mount or write probe succeeded. The
  capture ended before that attempt's final timeout. The previous attempt's
  timeout was already published. Clock logs do not measure physical waveforms.
  Evidence: `sd-probing-clock-{flash,startup}-20261007.log` in the protected
  evidence directory. The planned reset of a working card was not performed
  because mounting never succeeded. The device fault remains open.
- Astra audited the original Waveshare sequence and exact IDF 6.0.2 host/protocol
  path, then reassessed the failed corrected run. Published reset/busy reports
  support analogies, not a proven device cause; no matching confirmed Waveshare
  resolution was found. Debug automatic light sleep is disabled, and the SDK
  already sends CMD0 on each attempt. Another delay, card swap or unchanged
  reset loop lacks discriminating evidence. Further diagnosis requires observing
  actual SD power and CLK/CMD across the reproduced working-to-reset transition.
  Source references and limits are recorded in the storage architecture document.

- Cody memory-only protocol at `b9cfcb8` compiled/flashed on COM10 with verified
  hashes: app 6,694,736 bytes; SHA-256
  `2A5D9B15F6202574A4698AA093D3D033D6502EFDDC084FA20BC0274E5A47651A`;
  matching preserved ELF `b9cfcb8.elf` SHA-256
  `A194316D0CEB6317D049B0205C8EE7B405A2F0FAF7F94FEE3A3588E48EBF1BD0`.
  The 45-second passive capture completed with `sdio_probe=disabled`, but OCR
  still timed out. SDIO exclusion alone is disproven as the timeout remedy.
  Evidence: `sd-memory-only-{flash,runtime}-20261007.log` in the protected
  evidence directory. Configuration used the CLI-requested `set-target` recovery
  after changed defaults made `reconfigure` reject the previous signature.

- On `c3dceaf`, the owner confirmed renewed Sophia wake without touch; UART
  records matching WakeNet detection, successful wake and successful subsequent
  DMA frames. Card reinsertion restored mount attempt 5 and 49 verified writes.
  An explicitly requested reset immediately reproduced ACMD41 busy/OCR timeout
  before FAT mounting. Capture: `sd-working-card-reboot-20261007.log` under the
  protected evidence directory. The following standby/alternate-card captures
  were cancelled at the user's steering; cancellation is not a passed capture.
- Audio silence reported by the owner matches deliberate suppression in the
  current logs: boot quiet hours and personality `clock_policy=1`. Time service
  uses saved `UTC0`, with valid RTC/SNTP near 04:38 UTC. Codec/PCM registration
  succeeds. TTS exclusion did not remove the prerecorded cue registrations;
  audible playback outside quiet hours remains unverified. No saved clock or
  quiet-hours preference has been overwritten.

- Queued-only QSPI and completed CST9217 protocol at `c3dceaf` compiled with
  eight jobs on ESP-IDF 6.0.2 and flashed on COM10; all four hashes verified.
  App: 6,697,616 bytes (`0x663290`), 53% application partition free; SHA-256
  `4DDAC1F8A68CDCA61770CF9423B1F6EDBE8D4454CEACA80A1E07AF07A390F134`.
  Preserved matching ELF SHA-256:
  `8D8C527158372FEA0E6FCCEE4F04EF506C9DC00BF0490C9ADCD8A81E158EEB0C`.
  Final build log: `artifacts/build-cody-companion-wake-word-evidence/evidence/queued-qspi-touch-final-flash-20261007.log`.
  An earlier validation was intentionally cancelled before flash to correct
  command-only SPI admission; Ninja/Xtensa termination was confirmed before
  restarting. No warning suppression or successful partial build was claimed.

- Three captures of `c3dceaf` completed with public CLI exit zero: 240 seconds,
  then two 90-second reset/boot repeats. All reached inactivity standby, with
  75 successful periodic DMA frame samples total, zero watchdog/panic/core dumps,
  zero QSPI errors and zero rejected touch reports. These bounded observations
  do not establish indefinite stability, physical pixel quality or voice/touch
  wake on the new transport. SD initialization still timed out before mounting;
  no write probe could run. Captures are retained under
  `artifacts/build-cody-companion-wake-word-evidence/evidence/` as
  `queued-qspi-touch-runtime-20261007.log`,
  `queued-qspi-touch-repeat-1-20261007.log` and
  `queued-qspi-touch-repeat-2-20261007.log`.

- Captured and decoded a complete checksum-validated watchdog dump using the
  matching `2bd0626` ELF through `device.ps1 check -Check decode-core-dump`.
  Cody's thread is blocked in IDF LCD parameter transmission acquiring the SPI
  bus with `portMAX_DELAY`, during branding rows 464-465. Other running tasks
  are not evidence of the blocked frame's cause. Preserved full decoder output:
  `artifacts/build-cody-companion-wake-word-evidence/evidence/waveshare-core-decoded-20261007.log`.
- Implemented native-only queued QSPI admission/result retirement with bounded
  waits and internal immutable descriptors. The unchanged CO5300 panel driver
  uses its original opcode/address/pixel wire format; real errors retain unsafe
  DMA ownership. This removes the observed infinite bus-acquisition call path;
  final compilation/flash passed as recorded above. The subsequent 240-second
  capture recorded 25 successful frame samples, no watchdog or QSPI errors,
  and completed standby. Physical output and renewed wake remain unverified.
- Compared original Waveshare Arduino SensorLib touch code and constants at
  the same pinned commit: two-finger capacity, report ACK D000/AB and normal-mode
  D109/readback 0002. Implemented the missing ACK and mode restoration while
  retaining malformed-packet and original I2C errors. The subsequent 240-second
  capture initialized the controller without rejected reports. Physical
  two-finger input and renewed touch wake remain unverified.

- Compared the official Waveshare firmware and example BSPs at pinned source
  `e4344e70c2fa78a13e8a06566507f1ba8af6672a`; restored default SD host frequency
  and external-pull-up slot flags. Protocol captures identify repeated ACMD41
  busy responses before filesystem mounting; card reinsertion restored readiness
  and 34 verified write/read probes in one capture. Warm-reset recovery remains
  open. No formatting or shared power-rail cycling was introduced.
- Corrected the controller capacity for the fitted CST9217 to two contacts,
  retaining fail-fast packet validation and the UI's single-primary-contact
  interface. This addresses a captured boot abort caused by a legitimate second
  contact. Physical validation remains pending.
- Preserved original manga artwork and regenerated fourteen individual native
  ImageGen masters. Packaging now uses shared 256 x 256 indexed-alpha sprites
  (1,835,520-byte data payload), with unchanged palette and geometry; Windows
  reads the same extent and bytes. Generated preview reviewed; device quality
  and compilation evidence are recorded after their actual completion.
- The focused high-resolution personality suite passed through the public CLI
  on 2026-10-07 using the existing Visual Studio host compiler: 1,142 native
  assertions, 122 native/Python parity frames, and 118 independent host checks,
  plus fourteen icon/audio contracts. Updated two stale 600-ms fade expectations
  to the already-authored 400-ms fade-out. No firmware behavior was changed by
  this fixture correction. The mandatory pre-build cleanup removed the transient
  full host report; its observed counts are retained in
  `artifacts/build-cody-companion-wake-word-evidence/evidence/icons-v2-host-check-summary-20261007.json`
  and the public CLI result in `icons-v2-check-pass-20261007.log` there.
- Debug Cody wake-word at `2bd0626` compiled with eight jobs on ESP-IDF 6.0.2
  and flashed on COM10; all four written hashes verified. App: 6,701,296 bytes
  (`0x6640f0`), 53% application partition free; app SHA-256
  `0928570DE02DDFF1E3FC5720E7D700F53F50CABE8128E34E48B75EF4D5E3F741`.
  Matching preserved ELF SHA-256:
  `17B2EA4A7820C59C3C9721EA0F819DDB29741B44A226558ACD30C59B34428CAE`.
  The actual generated configuration enables wake-word/UART core dumps and
  excludes TTS; managed CST9217 source contains the two-contact capacity.
  Build/flash log: `artifacts/build-cody-companion-wake-word-evidence/evidence/waveshare-icons-v2-flash-20261007.log`.

- Recorded the user's selection: exclude the local speech synthesizer while
  enabling Sophia wake-on-word. Source review confirmed the previously loaded
  `289dbe3a` image excludes both features; retaining voice wake therefore needs
  the existing `cody-companion-wake-word` variant, not the ordinary `none` variant.
- Wake-word defaults now explicitly exclude TTS and synthesized Italian boot
  speech. Existing feature gates already reject a TTS component/backend in this
  variant. Its reviewed Sophia model uses the internal model partition, with no
  SD asset installation or deletion. Existing prerecorded PCM paths are separate
  from synthesis. Subsequent checks, build/flash and physical results are below.
- User confirmed prerecorded firmware audio is retained and explicitly requested
  wake-word activation on `main`. Restored `_MODEL_INFO_` from 172-byte CRLF to
  its pinned 170-byte upstream content, verifying the original manifest SHA-256
  before writing; recognition weights and provenance checks were unchanged.
- Public `device.ps1 update-lock -Profile cody-companion -Feature wake-word
  -BuildMode Debug` resolved and published the missing Cody wake-word lock.
  Generated configuration enables Sophia, AFE and standby listening with a
  64-KiB internal reserve; the actual component graph contains ESP-SR/DSP/FFT and
  no TTS component. Subsequent build/flash and physical results are below.
  Resolver evidence is under `artifacts/build-cody-companion-wake-word-evidence/`.
- Selected build-profile check and the first Debug build/COM10 flash passed:
  app `cabb5f59`, 4,966,576 bytes; sole packed recognition model 291,168 bytes;
  all four flash hashes verified. Runtime capture admitted the Sophia AFE, but
  rejected 22.05 kHz boot playback and stopped inference when a prerecorded
  clip held the shared capture/playback mutex longer than the 200-ms fetch wait.
  Capture also retained watchdog resets and the existing pre-FAT SD timeout;
  this image is not evidence of usable voice wake.
- Audio integration now converts authored boot PCM in bounded chunks at the
  fixed voice transport rate, supplies matching AEC samples, and keeps the
  acknowledged sole AFE RX owner independent of TX playback waits. Stream close
  and diagnostic capture reject active AFE ownership. Source integration reviewed.
- Corrected Debug `d4fafbca` compiled and flashed on COM10 through the public
  CLI with eight jobs. App: 4,967,648 bytes, SHA-256
  `1ABEE710297B083340EBF05EECB37EF880423CC35D4A88B4E947881C4B6BCC24`.
  Packed sole model: 291,168 bytes, SHA-256
  `EB17437145A7434DE9FE7E47165147D26542115A8367719F04BEB6D0F9EDFB21`.
  All four flash hashes verified; TTS remains outside the component graph.
- Corrected runtime capture completed boot PCM playback at 16 kHz from its
  authored 22.05 kHz source with `ESP_OK`; scene audio also played while AFE
  continued publishing real microphone samples. After a display-watchdog reset
  in the first captured boot, the next boot reached software standby at
  65.111 s. Sophia was detected at 67.990 s; active transition completed at
  69.000 s with reason `wake-word`, storage-resume and wireless `ESP_OK`.
  The owner explicitly confirmed saying Sophia and display wake without touch.
- The complete 180-second capture also recorded another standby at 130.732 s,
  Sophia detection at 139.305 s and wake-word active transition at 140.422 s.
  No AFE fetch failure or microphone-clock conflict occurred in the corrected
  capture. Only the first utterance has explicit physical confirmation.
- This proves one physical Sophia software-standby wake, not long-term voice
  reliability, false-positive rate, Deep-sleep wake or SD recovery. The card
  remained unmounted with pre-FAT `ESP_ERR_TIMEOUT`, including the probe after
  voice wake; a successful storage-service resume is not card access success.
  The first captured boot's `lvgl-worker` watchdog and counted logger truncation
  remain open. The wake feature uses internal recognition models and excludes
  synthesis; prerecorded firmware audio remains included.
- Evidence: `artifacts/build-cody-companion-wake-word-evidence/evidence/`
  `wake-no-tts-profile-check-20261007.log`, `wake-audio-fixed-flash-20261007.log`
  and `wake-audio-fixed-runtime-20261007.log`. `git diff --check` passed before
  both requested flash builds. Simulator/Release/full-profile/size analysis,
  broader checks and field endurance were not run.

## Requested SD/reset and display-watchdog fault localization — 2026-10-07

- User explicitly requested continuing until both remaining errors are
  understood and closed. Added Debug UART core-dump selection with an internal
  2-KiB panic stack, capturing task stacks without flash/SD persistence; Release
  explicitly disables this diagnostic output. Added stock SD command/response
  logging only around the existing card-init operation and restored both log
  levels afterward, preserving the original returned protocol error.
- Source review: the card has no detect GPIO or independent firmware-controlled
  supply; VCC3V3 is shared with touch/IMU/audio amplifier. No shared-rail cycle
  or implicit format is admissible as an inferred card reset. The display watch
  covers one frame, including SPI submission whose upstream control-command
  queue wait can be unbounded. Current running-task backtrace does not locate
  the blocked renderer. Diagnostic build/capture and exact causes remain pending.
- The first diagnostic configure did not discover the core-dump component,
  leaving its defaults unknown. Compilation was cancelled before flashing;
  Ninja/Xtensa compiler processes were confirmed stopped. Debug now explicitly
  admits official `espcoredump` before Kconfig and rejects missing UART capture
  after configuration. The working Sophia image remains on the device.

## SD mounted-card standby/touch-wake verification — 2026-10-07

- The owner confirmed the second physical card reinsertion, then corrected the
  display observation to off. A passive 180-second COM10 capture showed steady
  firmware standby with SD polling suspended, not a new card probe or timeout.
  Touch wake at 487.629 s restarted polling; mount attempt 4 succeeded at
  487.782 s without formatting. Active transition at 488.596 s and subsequent
  successful probe writes corroborated the owner's confirmed physical wake.
- A second passive 180-second capture, without reset/reinsertion, recorded a
  complete mounted-card cycle. Before standby the diagnostic reached sequence
  48; standby entered at 550.380 s with the card retained and I/O suspended.
  Physical touch requested wake at 615.405 s. Polling/mirroring resumed at
  615.419 s; verified write sequence 49 was published at 615.788 s. The display
  became active at 616.385 s, confirmed by the owner. Mount attempt remained 4:
  this was resumed access to the same mount, not a replacement mount.
- Successful writes reached sequence 96 before the next clean inactivity
  standby at 678.319 s. The owner also reported the SD visible in Info. No SD
  failure, consumer-stop, abort or Task-WDT event appeared in these two captures.
  During standby the last successful I/O timestamps/sequence remained unchanged
  and their sample ages increased; no suspended write was claimed as fresh.
- Protected evidence: `artifacts/build-cody-companion-debug/evidence/`
  `sd-second-reinsert-wake-20261007.log` and
  `sd-mounted-standby-cycle-20261007.log`, with companion capture transcripts.
  Loaded firmware remains `289dbe3a`; no new build/flash or SD format/erase ran.
  Diagnostic writes remain enabled in Debug. This proves one complete working
  card standby/wake cycle in Debug, whose automatic Light-sleep is disabled;
  intermittent failure, warm-reset cause and Release/Deep-sleep remain unproven.

## SD/standby diagnostic baseline and reporting corrections — 2026-10-07

- User granted exclusive device access and authorization for tests and flash.
  Public `device.ps1 check -Check storage-logger` and `snapshot-integrity` passed.
  Debug `cody-companion`, `Feature none`, eight-job build/flash on COM10 passed
  for `10a7b8f8`; application size 4,665,632 bytes. Written hashes were verified.
  Application SHA-256:
  `bf18b138d4aca0c429fb7f4a1eb75ea593bd448ddbd280f982b05fb38f9e48bd`.
- Completed a 300-second capture. At 39.097 s uptime SD was already unmounted
  with `ESP_ERR_TIMEOUT`, before standby. Attempts 3 (56.426 s) and 4 (121.708 s)
  failed at card initialization/OCR (`0x107`), with successful cleanup and no
  format authorization. Standby entered at 64.896 s; physical touch woke the
  screen, confirmed by the owner and the 119.285-s active transition. Another
  standby entered at 180.948 s. No status/write samples occurred (`seq=0`);
  this run cannot establish writable media or a working-card standby failure.
- Preserved baseline capture under the active build's protected evidence path:
  `artifacts/build-cody-companion-debug/evidence/sd-standby-debug-baseline-20261007.log`.
  Companion checks/flash/capture transcripts use `sd-debug-*` in that directory.
- Corrected wake logs to report service `storage_resume`, never inferred card
  readiness, and removed a card/FAT-validation claim when no card was mounted.
  Heartbeat admission now shares the polling tick clock: microsecond wake jitter
  had sometimes skipped an entire second. Original observation timestamps and
  synchronous-I/O delays remain explicit.
- Correction checkpoint `289dbe3a`: both storage/logger and snapshot-integrity
  public checks and `git diff --check` passed. Its eight-job Debug build/flash
  passed on COM10 with all written hashes verified. Application: 4,665,328 bytes;
  SHA-256 `409648f58c0677f4ead1a25b23298b1ce30933723b8d75c19b512a32d8fe926f`.
  Flash/check transcripts use `sd-debug-corrected-*` under protected evidence.
- The following 240-second capture recorded two restarts: a Task-WDT naming
  `lvgl-worker` at 18.833 s in its first boot, then a mandatory boot-completion
  abort after a real touch `ESP_ERR_INVALID_RESPONSE` in its second boot. The
  third boot recorded contact count 2 with maximum 1 at 20.423 s, stopping the
  Cody consumer. These faults do not establish an SD cause. Keep them explicit.
- The owner physically extracted/reinserted the card and confirmed an active
  display. In the third boot mount attempt 3 succeeded at 53.211 s, with 7,680 MB
  capacity, 7,657 MB measured free space and mirrored logs. The diagnostic then
  reached 126 successful bounded write/sync/readback samples, with the card still
  mounted at 216.816 s. No formatting or alternate card-detect GPIO was used.
  UI failure prevented standby at 80.418 s (`ESP_ERR_INVALID_RESPONSE`), so this
  capture proves an awake write interval, not a mounted-card standby cycle.
- A subsequent authorized 180-second reset/capture left the card inserted.
  It reproduced OCR timeout on attempts 1, 2 and 3 before clean inactivity standby
  at 64.903 s, with no further watchdog/abort or consumer-stop event in that
  capture. State heartbeats during steady standby were one second apart;
  synchronous initialization still delayed observations as documented. This
  isolates a reproducible warm-reset initialization symptom; electrical cause
  and a working-card standby/wake failure remain unproven. A second physical
  reinsertion was requested but not yet confirmed when capture completed.
- Preserved serial evidence:
  `artifacts/build-cody-companion-debug/evidence/sd-debug-corrected-reinsert-20261007.log`
  and `artifacts/build-cody-companion-debug/evidence/sd-debug-warm-reset-20261007.log`.
  Final loaded image remains `289dbe3a`; SD formatting/erase and simulator checks
  were not performed. Diagnostic implementation/evidence checkpoints are local.

## Requested SD/standby diagnostic implementation — 2026-10-07

- Added a Debug-only, nominal one-second heartbeat in the existing storage
  owner's polling/notification waits, with mounted/suspended/resume/terminal,
  logger, mount/cleanup and original status/write errors plus sample ages.
  State heartbeats remain available during standby and retry backoff without
  reading the card, disturbing quiescence or shortening retry deadlines.
- On awake, mounted, responding media, the same owner overwrites only the
  bounded `/sdcard/LUNASHELL/.sd-debug-probe` record, syncs, reads back and compares
  it. Real errno/stage/byte counts and close failures remain explicit; ambiguous
  close stops further diagnostic writes. Canonical `storage.debug` holds these
  observations; old success timestamps are not refreshed during suspension.
- Confirmed configured `SDMMC_SLOT_NO_CD` and CLK/CMD/D0 GPIO2/1/3, consistent
  with the local no-card-detect hardware inventory. This does not measure wiring
  or identify the intermittent cause. Static source review only: no tests/scans,
  configure/build/size, simulator, new flash, monitor or capture were run.

## VibeWare Cody firmware flash — 2026-10-07

- On explicit user request, ran `scripts/device.ps1 flash -Profile
  cody-companion -BuildMode Debug -Feature none -Port COM10 -Jobs 8` with the
  shared transient-artifact cleanup and its required incremental build. ESP-IDF
  6.0.2 compiled main image `6ae36bda`, including the VibeWare decoder/resources.
- Application image: 4,661,360 bytes (`0x472070`), with 69% of the selected
  application partition free according to the build's mandatory fit check.
  ESP32-S3 `a4:cb:8f:db:42:d8` on COM10 accepted bootloader, partition table and
  application; esptool verified all three written hashes and issued the final
  RTS hard reset. The public command completed with exit code 0.
- Application SHA-256:
  `42e3a8516585a8ba59f3512dcc53b4669ebd0a899e6a19c6340e17e9cace7182`.
- Log: `artifacts/build-cody-companion-debug/evidence/vibeware-boot-flash-20261007.log`.
  Cleanup removed the earlier local
  `artifacts/pdf-review/` evidence; final PDF files and their Obsidian source
  manifest remain outside that transient directory.
- No separate tests/scans, size report, simulator, Luna build, monitor or capture
  ran. Written bytes are verified; startup success and physical animation,
  timing, handoff and gestures remain unobserved.

## VibeWare boot identity — 2026-10-06

- Preserved the approved Obsidian press-kit GIF and generated 42 exact RGB565
  frames with their original 3,500-ms cycle through `device.ps1 generate
  -Generator vibeware-boot`. Twofold nearest-neighbor artwork is 256 x 292 at
  (105, 87); deduplicated immutable pixel data occupies 630,600 bytes.
- Implemented the first boot image on Cody's existing native DMA/frame owner
  and Luna's LVGL boot owner, completing a cycle after real service readiness.
  Existing Cody character phases follow the logo with their clocks preserved;
  standby and deep-sleep wake omit the introduction. Rejected resources and
  asynchronous LVGL failures retain real errors and canonical publication.
- Added the Windows `boot-vibeware` preview, matching Luna simulator artwork,
  package inputs and the user-provided Studio manifesto URL. Source integration
  received static review only. No tests/scans, simulator, firmware configure,
  build, size, flash, monitor or capture ran; physical behavior remains open.
- Local implementation checkpoint: `3e97832`; no push accompanied the feature.
- Revised both Obsidian atlases and their repository mirrors: Manga Status now
  has 27 pages, Navigation 26. Preserved all 24/23 existing page content streams
  and earlier errata; added B01 marketing identity, B02 startup sequence and a
  final dated corrections page with original page/screen references and the
  supplied manifesto link. All six added pages were rendered with Poppler and
  visually reviewed before the local files were updated. Original backups,
  renders and the document hash manifest are under
  `artifacts/pdf-review/vibeware-boot-2026-10-06/`; this is document QA only.

## Cody atlas documentation revision — 2026-10-06

- Updated both requested Obsidian atlases and their `docs/Cody` mirrors: Manga
  Status has 24 pages, Navigation has 23. Preserved all 18/19 prior pages,
  including earlier errata, and appended final dated corrections with original
  page/screen references. Covered stronger mouth breathing, corrected Whistle,
  icon placement/sizes/fades/SFX, caress/eye/tap behavior, idle scheduling,
  returning-device boot and Windows fake audio/UI/branding.
- Generated and visually reviewed all ten new pages through the public generator
  CLI and Poppler. The document generator checks original decoded page streams
  remain unchanged. Review images, source backups and hash manifest are under
  `artifacts/pdf-review/cody-documentation-2026-10-06/`. This document review does
  not constitute automated firmware tests, a new build/flash or physical evidence.

## Tap, manga cues and Windows fake audio — 2026-10-06

- Added independent physical tap feedback, manga symbols 20% larger, 400-ms
  fade in/out and bounded visible-symbol sound retries. Cody Debug with feature
  `none` built successfully with eight jobs at `0ea2613`; hardware playback
  and layout remain unobserved. Build evidence is retained below
  `artifacts/build-cody-companion-debug/evidence/touch-feedback-build.log`.
- Final Cody Debug/`none` build and COM10 flash completed at `de9b8b18`.
  The app is 4,027,456 bytes; bootloader, partition table and app hashes verified,
  followed by RTS reset. No NVS erase or reprovisioning ran; no monitor/capture
  or physical behavior check accompanied the flash. Final evidence:
  `artifacts/build-cody-companion-debug/evidence/touch-feedback-flash-20261006.log`.
- Implemented the Windows fake audio owner linking unchanged Cody C++
  personality/whistle/typewriter modules and their original PCM. Connected
  presented symbols, whistle generations, greeting glyphs and pointer taps to
  the existing PC playback backend. Generated boot WAVs share that owner.
  The DLL compiled successfully before the subsequent user-requested launch;
  PC listening remains unverified. Documentation: `docs/architecture/cody/runtime.md`.
- On the subsequent user-requested Windows launch, the painter stopped at
  stale palette provenance. Normalized its text source and regenerated the
  icon manifest; all sprite binaries remained unchanged. Added a first-frame
  presentation diagnostic, Consolas host UI fonts and equal inspector-tab
  width/padding across selected states. The authorized restart at `03c1fa3`
  presented its first frame and retained an active animation callback without
  further console exceptions during observation. PC listening and physical-device
  behavior remain unverified.
- Added the approved VibeWare logo and three user-click links: Cody GitHub,
  the creator's website and the public GitHub manifesto. The source asset lives
  at `Obsidian/20_Execution/Projects/VibeWare/Branding/vibeware-logo.png`;
  the simulator bundles its unchanged PNG, licence and provenance independently
  of OneDrive. Link navigation has not been exercised.
- The user then reported a stopped preview during a Whistle gesture:
  `mouth_width=0.49784793754180495` was rejected by the rig's 0.5 minimum.
  Both native and Python response models targeted an invalid fixed 0.42 width.
  Changed that target to the rig's authored minimum, retaining strict renderer
  validation. Firmware rebuild/flash and formal regression checks of this
  correction remain unperformed; the earlier flashed image is still `de9b8b18`.
- Increased normal mouth breathing on request: 75% relative closure on
  exhalation, a stronger reopening for closed expressions and 18% width motion
  toward authored endpoints. Changed the rest lip to a thin smile and shifted
  its clipped tongue with the lower lip. Native and Python equations match;
  the generated rig was refreshed through `device.ps1 generate -Generator
  cody-rig`. Quiet/Worried retain the authored 0.28 tongue threshold. Formal
  checks and firmware compilation/flash of this change remain unperformed.
- Authorized final checks: `storage-logger` and `radio-memory` passed.
  `cody-native` stopped before compilation because the host compiler was absent
  from PATH; installed MSVC was subsequently found. `snapshot-integrity` failed
  an obsolete mandatory-logger assertion, corrected in `45316e5`.
  The user explicitly declined rerunning these two checks.
  `cody-personality` stopped at a palette text hash dependent on Windows line
  endings. `cody-runtime` compiled and failed idle-clock parity at tick 255.
  Normalized text provenance and matched the Python idle clocks to native
  float32 rounding; these corrections have not been retested.

## Optional local TTS implementation — 2026-10-05

- Implemented Debug-only PWR double-click software restart within 500 ms.
  The existing single-click screenshot waits for that window before its 3/2/1
  countdown; separately acquired clicks are required, acquisition failures
  cancel pending gestures, and ambiguous batched timing is rejected.
  The reset preserves settings and uses the existing bounded SD/serial drain.
  Release excludes screenshot admission, shutter worker and PWR/BOOT software
  reset gestures while retaining ordinary standby/fullscreen controls.
  The resulting `61ab74a` Cody `tts-sd` Debug image subsequently built with eight
  jobs and flashed on COM10; bootloader, partitions and app hashes verified,
  then RTS reset completed. App is 4,212,592 bytes. Evidence:
  `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-debug-pwr-flash-20261005.log`.
  No further boot capture, gesture test or Release build ran.
- After SD reinsertion, the authorized COM10 reset completed on 2026-10-05.
  The user reported the startup sound and brief clicks, not an intelligible
  Italian sentence. A subsequent explicitly authorized 30-second reset/capture
  still shows `sdmmc_init_ocr` timeouts on both attempts, at 4.676 and 23.004 s;
  speech request 1 fails with `ESP_ERR_TIMEOUT` at 4.688 s before engine
  initialization. Mandatory boot and Cody rendering continue. Evidence:
  `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-sd-reinserted-audio-20261005.log`.
- Created `E:/ESP32 Luna Speaks` on `feat/luna-speak` from local `main`
  at `1fbc0d7b`; preserved the original worktree. Implemented default-off
  `CONFIG_LUNASHELL_BUILD_ENABLE_TTS`, isolated `tts-sd` and `tts-flash` variants
  for Cody/full, native pinned eSpeak C/Unicode sources, bounded UTF-8 request
  and cancellation APIs, initially silent startup and canonical speech state publication.
- Implemented registry ownership, guarded PSRAM-only engine allocation, a
  continuous session in the existing audio owner and owner-mediated SD package
  reads with exact size/SHA-256 verification on every request. SD stores English,
  Italian, Mandarin and Cantonese; flash embeds English only. Ordinary profiles
  exclude the component and its asset generation; SD embeds metadata only.
  CLI/menu/configuration gates preserve explicit backend selection. No SD-to-flash
  fallback was introduced.
- Subsequently wired the requested fixed Italian phrase after mandatory boot in
  `tts-sd`, gated separately by `CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN`:
  "Ciao questa è una sintesi vocale di test 5 ottobre 2026". One-shot worker waits
  only for the initial canonical SD report, with a 30-second deadline and real
  failure publication. This diagnostic bypasses clock/quiet hours while normal
  requests retain their policy; active power, storage integrity and saved volume
  still apply. It replaces Cody's usual boot cue only while the test is enabled.
- Pinned synthesis to core 0 and Cody/LVGL renderer tasks to core 1 using shared
  constants, including a guarded patch for LVGL's otherwise unpinned software
  draw workers. Single-core TTS is rejected. Default-off speech and flash-English
  composition remain intact. The Cody SD Debug build subsequently completed;
  LVGL patch application/build, scheduling performance and audible boot behavior
  remain unverified.
- Pinned principal source payload is 827,224 bytes for English or 3,111,550
  bytes for all four languages, excluding voice metadata/code/runtime memory.
  These are package metadata, not linked size or measured memory evidence.
  [Implementation and usage](docs/architecture/text-to-speech.md).
  Outstanding variant/test/runtime evidence remains in `TODO.md`; no upstream
  publication was performed.
- Authorized deployment preparation: generated the SD package through
  `scripts/device.ps1 generate -Generator tts-assets`, copied 12 files plus the
  manifest to `G:/LUNASHELL/tts/espeak-ng-data`, and verified every payload's
  manifest SHA-256 on readback. Payload totals 3,112,743 bytes; preserved unrelated
  SD content and did not format the card.
- Corrected first IDF 6.0.2/GCC 15 compile/link causes in `57121ca2`: FreeRTOS
  header order, native libc endian definitions, explicit intended fallthroughs,
  bounded dictionary identities, checked number/phoneme formatting with actual
  capacities, synchronous dispatch compilation, and exclusion of desktop dictionary
  trace dependencies. Compiler diagnostics remain enabled. Cody `tts-sd` Debug
  configured and built with eight jobs; the first linked app is 4,211,264 bytes.
  Build evidence: `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-sd-build-20261005.log`.
  No standalone tests/scans, size command, simulator or listening test ran.
- Authorized flash completed on COM10 (ESP32-S3, MAC `a4:cb:8f:db:42:d8`):
  `scripts/device.ps1 flash -Profile cody-companion -Feature tts-sd -BuildMode Debug -Jobs 8 -Port COM10 -Baud 460800`.
  Its required rebuild of `57121ca2` succeeded; deployed app is 4,211,408 bytes.
  Esptool verified bootloader, partition-table and application hashes and issued
  the RTS reset. The separately gated Italian boot phrase is enabled. Evidence:
  `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-sd-flash-20261005.log`
  and `tts-sd-deploy-20261005.json` in the same directory. No monitor/capture was
  started; SD mounting, audible playback and concurrent rendering remain unverified.
- User reported a black screen after this deployment. Authorized reset/30-second
  capture on COM10 confirms repeated `ESP_ERR_NO_MEM` aborts at
  `main/main_headless.cpp:216`, `LUNASHELL_power_manager_init()`, before mandatory
  startup completion or any boot speech request. Captured version `57121ca2`,
  ELF SHA prefix `bbad4927a`, matches the deployed image. Evidence:
  `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-black-screen-boot.log`.
  The mandatory power task requires an 8,192-byte internal stack; native engine
  BSS occupied internal RAM despite the already external heap and worker stack.
- Implemented the resulting correction: component-local IDF linker fragment
  routes native engine BSS/COMMON to PSRAM, with Kconfig/CMake/CLI admission
  requiring external BSS support. Public service synchronization and native
  guard state stay internal. Both startup paths now create optional speech only
  after their mandatory owners. Request text and SD package pointer storage also
  reside in external BSS, alongside the already external package/heap/stack.
  Static review only; this correction has not yet
  been built, flashed or observed on the device.
- Subsequently built/flashed correction `58757005` on COM10 through the same
  eight-job `cody-companion`/`tts-sd` Debug flash command; all esptool written
  hashes verified and RTS reset completed. App remains 4,211,408 bytes.
  The authorized fresh 30-second boot capture matches ELF SHA prefix `98dc75b01`:
  early free internal RAM is 105,119 bytes versus 88,703 (16,416 recovered),
  mandatory power service and startup complete, the speech worker starts on core 0
  after those owners, and Cody rendering continues without the previous abort.
  SD card-init instead times out twice; speech request 1 correctly fails with
  `ESP_ERR_TIMEOUT` before any engine initialization. User subsequently reported
  reinserting the prepared card; mounting and audible playback need a new observation.
  Evidence: `artifacts/build-cody-companion-tts-sd-debug/evidence/tts-psram-fix-flash-20261005.log`
  and `tts-psram-fixed-boot.log` in the same directory.

## Cody and connectivity — 2026-10-02 to 2026-10-05

- Implemented the authorized PSRAM and best-effort logger revision: selected
  task-only models, benchmark/canonical snapshot, PCM scratch and 20 KiB of
  application worker stacks move to external BSS with original defaults.
  DMA/control/ISR, NVS, pairing and flash-reading workers stay internal.
  Logger callback pressure counts losses without stopping a healthy SD mirror;
  optional startup/capture publication and logger I/O do not reset the device.
  Boot retention frees after confirmed replay or archives asynchronously to
  PSRAM; failed flush/allocation retains unconfirmed records and original errors.
  Full Snapshot acquisition publishes actual loss/memory observations even
  without SD, with coalesced warnings and freshness. Updated existing scheduling
  contracts and architecture docs received static review only. The estimated
  roughly 68-KiB internal recovery remains unmeasured; no checks/build/size,
  simulator, push, flash or physical verification was run. Validation is in TODO.

- Made the wake-word PSRAM policy explicit: PSRAM initialization/malloc,
  external BSS/static stacks, a zero internal-preference threshold and at least
  64 KiB reserved internal memory are enforced by feature defaults, CLI and
  CMake. AFE keeps PSRAM-only aligned PCM/ring/feed/fetch stack allocations and
  the vendor's MORE_PSRAM mode; stack sizes are named in IDF bytes. The branch
  moved 12,640 authored bytes of playback stacks and playback/diagnostic PCM
  staging to external BSS; main's independent shared-audio PSRAM revision now
  provides the same placement for ordinary profiles too, without double counting.
  TCBs, synchronization and driver DMA
  resources remain internal; immutable assets/models remain flash-backed.
  Static review only: no configure, build, tests, linker/runtime verification,
  flash or push. Memory placement and external-stack behavior remain in TODO.

- Implemented the reported icon readability correction: Main/Sleep centers move
  30 logical pixels right and 40 down, with threefold scale-dependent extent
  (87 px at Full, 78 at Greeting, 63 at Medium; smaller eligible sizes at least
  36 px). Micro and excluded routes still suppress icons. Shared validated YAML
  drives native/generated profiles and Windows; both allow upper-body overlay
  while retaining circle and status/Back admission. Source masters remain
  64 x 64; bounded destination support is 192 x 192 without new framebuffers.
  Static review only: no new build, tests, simulator, push or flash accompanied
  this correction. Physical readability and frame cost remain in TODO; PDFs
  await approval. The earlier flashed image is recorded separately below.

- Implemented the follow-up touch/boot changes: recognized caresses request
  Affection through priority arbitration, without an extra icon repeat cooldown;
  gentler 300-ms/path thresholds retain speed, zone and acquisition guards.
  Canonical eye taps request varying Fear/Frustration icons using prior stress;
  existing original SFX retain completed-frame admission with a two-second audio
  cooldown. Idle uses a complete three-scene random bag. Waking/Ready now take
  1.5/2 seconds; returning devices receive random Affection/Joy after cold boot
  without changing saved pairing/first-boot keys. PDFs await the user's approval.
  Firmware checkpoint `09ea3b70` was pushed to `origin/main`, then the requested
  `scripts/device.ps1 flash -Profile cody-companion -BuildMode Debug -Port COM10 -Jobs 8`
  completed on 2026-10-05. The CLI performed transient cleanup and the required
  automatic CMake/incremental build with ESP-IDF 6.0.2; the 4,027,072-byte app
  leaves 73% of its partition free. Bootloader, partition-table and app hashes
  verified before the final hard reset. Only those three flash regions were
  written; the NVS pairing region was preserved. No tests, simulator, monitor,
  capture or physical visual/audio observation was run. Those checks remain
  pending in TODO; the flash log is under `artifacts/logs/flash.log`.

- Implemented stronger shared mouth breathing and random Idle Yawn/Whistle/
  Greeting accents on 2026-10-05, preserving the screen-16 route and priorities.
  Added the original bounded flash whistle to the existing optional audio owner
  and published actual playback cue identity through the canonical snapshot.
  The Windows model mirrors visual behavior and marks whistle audio device-only.
  Updated both requested atlases with appendices and dated final errata, retaining
  previous pages. The firmware changes were subsequently compiled/flashed in
  `09ea3b70` as recorded above. Host/simulator and physical behavior remain pending
  in `TODO.md`; the initial checkpoint claimed static source review only.

- Integrated the SIMD and DMA branches into `main` and removed their worktrees
  and local/remote branches on 2026-10-05. The preserved-evidence directories
  named in the historical records below were later removed by the authorized
  full local cleanup; their tracked summaries remain here and in architecture docs.
- After the requested branch cleanup, only `main` remained locally and on
  `origin`; this records the resulting ref state without asserting ancestry for
  the deleted branch tips. After the clean checkout removed its
  generated state, `device.ps1 set-target` recreated Debug `cody-companion` and
  `device.ps1 flash -Port COM10 -Jobs 8` built commit `160a080a`. The 4,014,496-byte
  app left 73% of the app partition free. Bootloader, partition table and app
  hashes verified before the hard reset. After workstation power was restored,
  the same public flash command completed again from the incremental build with
  the same hashes and final reset. No monitor/capture or physical UI observation
  accompanied either flash.
- Audited the repository records after branch consolidation. One root `AGENTS.md`,
  `lessons.md`, `TODO.md`, `planned-todo.md` and `done.md` remain canonical; no
  nested instruction file or `TASKS.md` alias existed. Removed stale live-branch
  wording from active work and marked cleaned ignored artifacts as historical.
- Assigned the continuous native Cody renderer to CPU1 and made the Cody profile's
  CPU0 ownership explicit for startup, Wi-Fi, the Bluetooth controller, NimBLE and
  lwIP. The single frame/display owner and unpinned bounded service workers are
  unchanged. This checkpoint has static source review only; build and physical
  per-core/frame/radio validation remain open in `TODO.md`.
- Corrected source admission for the automatic Idle clock greeting: its retained
  Greeting SceneFrame no longer disables the main personality channel or
  petting. Explicit onboarding greetings and other routes remain excluded.
  Added bounded contact-start/exclusion/release logs for actual acquired input
  so scene suppression and incomplete/rejected strokes can be distinguished.
  The raw touch failure remains unresolved; driver acceptance/recovery has not
  been relaxed. The real routine-Greeting geometry host integration check passed
  (gesture, happiness, channel admission and actual heart raster), then Debug
  `26dd7866` built successfully and flashed on COM10 with verified hashes.
  Image size: 4,007,344 bytes; app partition 73% free. Physical recognition remains
  open; see `caress-greeting-*` command/capture logs in the protected build evidence.

- Enabled the existing measured Debug HUD in the main companion view after
  retained pairing, including the automatic clock greeting. FPS counting,
  text preparation and raster composition share the same route admission.
  Main-view sleeping-face selection remains eligible; original onboarding,
  footer, benchmark and session-preference guards are retained. CPU1/CPU2 use
  existing canonical per-core runtime measurements without new polling. The
  prepared next increment adds IPv4, free internal RAM/PSRAM and truthful SD
  presence/access/free-space rows, plus a quieter date, larger `HH:MM` without
  seconds and more legible battery/Wi-Fi/Bluetooth icons. Static review complete;
  Debug `3898f3d2` built successfully (4,007,536-byte image, 73% app partition
  free) and flashed on COM10 with verified hashes. The user confirmed seeing
  FPS/CPU below Cody. The `caress-main-hud-device-20261005.log` capture records
  ordinary main-view throughput around 16-18 FPS and total core0 usage around
  75-80%, with core1 usually rounding to 0%. These samples do not establish
  heart-icon cost: no Affection trigger was captured. Real acquired contacts
  reached Main admission but ended rejected, with durations of about 127 ms,
  451 ms and 167 ms, below the configured 500-ms minimum. The specific earlier
  rejection cause was not published; contact timing alone cannot explain every
  rejected gesture. SFX-on-appearance is already enabled in the YAML/runtime;
  heart playback remains physically unverified until the icon is admitted.

- Added typed first-rejection evidence for physical caresses without changing
  gesture thresholds, plus one-shot SFX disposition logging at the first retired
  visible icon. Added a clean transient SD-resume gate that releases power
  suspension for the existing owner retry/backoff while preserving the original
  error and keeping the logger mirror disabled. The board explicitly uses
  `SDMMC_SLOT_NO_CD`; no card-presence GPIO participates. Integrated remote main
  `06560c1d` and its bounded two-slot display DMA implementation in merge
  `f16043e0`. The final `cody-runtime`, `cody-personality` and `display-dma` host
  checks passed. Debug built successfully through the public console with eight
  jobs (4,014,512-byte image, 73% app partition free), then flashed to COM10 with
  verified hashes and hard reset. The reset capture confirms that exact app
  version, two active six-row DMA slots, Wi-Fi/DHCP at `192.168.1.28` and normal
  main-view throughput around 17.5-19.1 FPS. Total core0 usage remained about
  96-98% and core1 about 0-2%; this is full-core utilization, not Cody task time.
  Three physical SDMMC probes failed at card OCR with `ESP_ERR_TIMEOUT`, clean
  cleanup and 15/30/60-second retry backoff, so card access/free space correctly
  remained unavailable. The capture contained no touch event or wake after
  standby, therefore heart/SFX and the new first-rejection evidence remain
  physically unverified. Evidence is under
  `artifacts/build-cody-companion-debug/evidence/final-*20261005.log`.

- Investigated the reported caress freeze on device firmware `99342b7a`:
  the first capture attached after a retained touch `ESP_ERR_INVALID_RESPONSE`,
  while periodic firmware snapshots continued. Source review establishes that
  an acquisition error terminates the Cody worker and leaves later IRQs pending;
  it does not identify the original malformed packet. Reset captures booted to
  ready touch/display and later entered normal inactivity standby without a
  captured caress or panic. One live capture was interrupted when Codex restarted;
  it is incomplete evidence. The user reports a later stroke neither froze nor
  showed a heart; physical gesture recognition remains open in TODO.
  `device.ps1 check -Check cody-personality` passed 1,041 native assertions,
  122 controller parity frames and 118 host assertions. `cody-runtime` passed
  500 model/host ticks after its obsolete screenshot hit-target expectation
  was corrected to the already implemented hidden/noninteractive control.
  Initial check attempts retained a missing compiler environment and that stale
  expectation in their command logs; MSVC developer environment was restored.
  Checks ran through the public CLI. Evidence and command logs are under
  `artifacts/build-cody-companion-debug/evidence/caress-*`; transient host reports
  under `artifacts/checks/` were removed by the subsequent authorized build cleanup.
  That diagnostic checkpoint changed no firmware; the later Greeting fix and
  build/flash are recorded above. The simulator remained closed.

- Integrated remote main `649cbc7f` into `feat/personality-icons`, preserving
  petting, happiness, symbol arbitration and the Studio DPI/renderer changes.
  Ordinary S3 Pearl rendering now uses the upstream SIMD kernel and transparent
  row/block skipping; automatic benchmark and detailed block profiling remain
  disabled. Resolved native profiling conflicts and added missing personality
  sources/includes to two host-check recipes. Static integration reviewed.
  Debug firmware `99342b7a` built through `scripts/device.ps1 build` with eight
  jobs and flashed through `scripts/device.ps1 flash` to the USB ESP32-S3 on
  COM10; both commands returned success, all written hashes verified, and
  esptool performed its final hard reset. App size is `0x3d2330` bytes
  (4,006,704), with 73% of the app partition free. Evidence is in
  `artifacts/build-cody-companion-debug/evidence/` as
  `personality-simd-main-merge-build-20261005.log` and
  `personality-simd-main-merge-flash-20261005.log`. No new tests, monitor,
  capture or simulator session ran. Startup behavior and physical performance
  of this combined image remain unverified.

- The first Studio UI `679fbda2` opened and responded after the authorized
  restart; the user reported blurred Windows text and ~100 ms host ticks, then
  closed it. Implemented explicit PMv2 before Tk, actual startup DPI point/pixel
  scaling and physical monitor work-area bounds, scanline edge buckets and sparse
  coverage composition with C-backed
  opaque copy and the original exact partial RGB565 blend. Visible inspector
  readouts are capped at10Hz, status at5Hz; model/gesture clocks retain per-tick
  advancement. Desktop redraw defaults to30Hz with an explicit15/25/30/60 limit;
  native firmware cadence is unchanged. Added pure host raster regression check source through the public
  CLI. Static review only for these corrections; no new suite, restart, benchmark,
  firmware build or COM access. No claimed FPS/CPU improvement. Different-DPI
  monitor transitions remain open.

- Created requested pre-UI checkpoint `b4a7a75` on `feat/personality-icons`.
  Refreshed the Windows inspector in source with a local Tkinter/ttk dark theme,
  navy/teal palette, Segoe UI, image-backed switches and slim sliders. Added a
  fixed studio header, grouped controls, independent tab scrolling, a wrapping
  expression grid, target-value readouts and a two-line diagnostic footer.
  Clean view/Face are initial desktop preferences; inspection remains explicit.
  Firmware, native character geometry and model rules are unchanged. Static
  integration review completed; subsequent startup is recorded above. No new
  suite, build or COM access. Earlier build/parity results below belong to the
  preceding UI revision.

- Integrated remote `origin/main` at `4d026d13` into `feat/personality-icons`.
  Preserved both personality/YAML work and upstream mouth/tongue, greeting,
  pairing and BLE/display DMA fixes. Resolved the worker conflict using main's
  atomic parked-frame observation while retaining happiness advancement,
  audio suspension and transient heart cleanup. Both Done histories are kept.
  Merge checkpoint `355c7f04` passed the authorized Debug build with eight jobs
  on ESP-IDF6.0.2: image `0x3d02e0` bytes, 73% application partition free.
  The authorized focused suite passed 1,041 native assertions, 122 native/Python
  controller parity frames, 32 host petting, 32 happiness and 54 wellbeing
  assertions, plus fourteen icon/audio contracts. Evidence is preserved under
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261005-merged-main/`
  and `personality-main-merge-debug-build-20261005.log` in its parent.
  Corrected the Windows simulator's obsolete blue boot PNG references to the
  versioned pearl frames; the Rig window opened and was responding. That
  host-only reference correction occurred after the suite and has startup
  evidence, not another suite run. No COM access, flash or hardware validation.

- Expanded build-time personality authoring to strict `cody.personality.v2`:
  permanent typed event/zone/metric/icon IDs, `caress = 1` for both anatomical
  zones, gesture/mood/channel/follow/anchor policies and global/per-cue SFX gates.
  Generated four immutable headers through the public CLI. Added priority
  arbitration (LowBattery900 > Affection200 > Charging100), smooth urgent
  handoff, per-identity repeat cooldown retained across hidden scenes and
  canonical low-battery hysteresis (enter15%, exit20%). Native/host fixtures
  and simulator sources are integrated; the corrected suite passed as recorded above.

- Authored commented dormant wellbeing policies with explicit session events,
  minute-based fatigue, screen break 60min/5min, movement 30min and eye 20min/20s/
  20ft. Recorded HSE/NHS/NEI sources. Water 60min is labeled a product default,
  since NHS specifies no hourly cadence. Engine and delivery stay disabled;
  pure native/host deadline logic is implemented without a runtime adapter
  or visible reminders. This does not change the sole active happiness MVP.

- The expanded focused suite compiled native portable sources on 2026-10-05
  and ran 1,041 native assertions. Two handoff expectations incorrectly checked
  rendered cue identity at zero opacity; corrected them to inspect the channel
  identity and require an empty frame. This first attempt did not reach Python
  parity or independent host fixtures; the corrected post-merge suite subsequently
  passed as recorded above. Host controller state is616B,
  including112B repeat timestamps, not a firmware stack result. Failed-run
  report/trace retained under
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261005-first-attempt/`.

- Added a source-reviewed single-value happiness MVP: canonical
  `assets/cody/personality.yaml`, strict schema and immutable generated flash
  rules (generation through `scripts/device.ps1` completed). Session happiness
  starts at 50, recognized carezze add 10, and each 60 awake seconds subtracts
  one point, with intentional 0/100 saturation and retained partial minutes.
  Sadness is derived as 100 minus happiness. Facial/motion bindings remain
  disabled. Native and synthetic host source expose values independently of
  heart occupancy; the passing expanded suite, build and simulator startup are
  recorded above. Physical hardware behavior remains unverified.

- Added a source-reviewed gentle petting experiment on 2026-10-05 in normal Main view:
  body-contained AboveEyes/BelowMouth zones, acquired-time/path/speed/gap
  recognition, one reaction/contact and six-second cooldown. An idle symbol
  channel admits a four-active-second Affection request; occupied channels
  drop it, explicit selection remains authoritative, and existing fades/audio
  admission/body drag remain in use. Native helper and Windows pointer preview
  share authored thresholds. Added focused native and independent host gesture
  fixtures; new gesture trace parity is not claimed.
  Needs/mood/session state exploration is documented separately and deferred.
  The expanded suite attempt is recorded above; no new firmware build,
  interactive simulator execution or hardware action has run.

- The earlier `cody-personality` suite passed before petting changes: 748 native
  assertions, 86 native/Python parity frames and fourteen icon/fourteen audio
  contracts. Preserved evidence:
  `artifacts/build-cody-companion-debug/evidence/personality-host-20261004/report.json`.

- Configured and built the personality implementation `2518f83a` as Debug with
  ESP-IDF 6.0.2, `scripts/device.ps1` and eight jobs. Compilation/link succeeded;
  binary is `0x3c5870` bytes with 73 percent application-partition free. Evidence:
  `artifacts/build-cody-companion-debug/evidence/personality-icons-debug-build-20261004.log`.
  Authored a focused `cody-personality` host suite covering actual controller,
  finite following, Sleep/pause, dirty raster, native/simulator parity and icon/
  sound asset contracts; corrected `cody-native` source linkage. The suite later
  passed as recorded above; new petting tests await fresh command-specific
  approval. No device upload/access for this branch.

- Extended the personality channel to Main/full-size Sleep only, with one latest
  target, 300-ms stability, 400-ms fade in, 2.5-second hold and 600-ms fade out.
  Semantic Sleep has explicit priority/release; device invalidity clears false
  facts. Configurable face anchoring, bounded geometric following, fixed chrome
  exclusions and explicit blocked-layout status replace adaptive relocation.
  Simulator source mirrors the channel. Created fourteen original 16-kHz mono
  PCM SFX (103,360 sample bytes) and a 12.33-second audition reel through
  `scripts/device.ps1 generate -Generator cody-personality-audio`. Completed
  frames trigger an optional platform-owned worker with busy drops, expiry,
  8-second cooldown and quiet/standby gates. Asset generation and static review
  only; no listening, checks, builds, simulator or hardware validation.

- Implemented the isolated `feat/personality-icons` worktree from `290b6da3`:
  fourteen standalone ImageGen manga groups, deterministic non-reserved shared
  palette indices with separate alpha coverage, individual indexed PNG/bin
  assets, exact crop/component/hash provenance and internal-flash tables.
  Generation ran through `scripts/device.ps1 generate -Generator
  cody-personality-icons`; payload is 115,200 bytes plus descriptors/code.
  Native rendering uses one selected typed cue, every existing size except
  Micro, bounded above-body admission, 85-ms delayed position/squash and old/new
  frame damage. Session narrative/device selectors keep device truth canonical.
  Simulator source mirrors the feature and labels host device-cue previews
  synthetic. Only generation and static source/artwork review were performed;
  compilation, automated checks, simulator and physical behavior remain open.

- Implemented native frame DMA overlap on `codex/cody-dma-double-buffer`: reborrow
  idle storage before every dirty tile, keep at most one color DMA outstanding,
  drain the final transfer before freeing memory, ending the watchdog and
  committing geometry. Synchronous writes drain earlier DMA before reuse.
  Native allocation prefers two 16-row slots within the previous 29,824-byte
  budget; fragmented heaps may use independent blocks and shorter even rows,
  retaining 4,096 bytes of aggregate headroom. Partial allocations are released
  before degradation; uncertain DMA retains the complete lease until reset.
  Actual capacity/completion error is published in the canonical frame snapshot.
- Actual-BSP frame/callback/synchronous/allocator host contracts pass, including
  the observed 12,800-byte second block, partial allocation, one-slot degradation,
  payload immutability, lost/duplicate completion, failure drain and quarantine.
  Native raster, SIMD model and finite benchmark contracts pass. Debug builds and
  flash/hash verification pass through `device.ps1` with eight jobs. Evidence is
  under `artifacts/build-preserved-evidence/cody-dma-double-buffer/`, in
  `build-cody-companion-debug/evidence/`, with
  host reports in `host-checks/` and `display-dma-adaptive-host/`.
- Fixed 100% SIMD device run at `a01aed4`: two 12-row slots (22,400 total DMA
  bytes), 842 frames, identical 63,000-pixel payload, mean 45,828 us, p95 46,667 us,
  maximum 47,607 us and 842/842 misses at 40 ms. The earlier one-slot run on the
  same combined Cody source completed 753 frames: mean 51,542 us, p95 52,139 us,
  maximum 52,726 us. DMA overlap recovers 5,714 us mean and 5,472 us p95;
  raster stays 35,086 versus 35,112 us, transfer falls 12,508 to 6,761 us.
  Evidence: `dma-benchmark-100.log` and `dma-adaptive-benchmark-100.log`.
  No display completion/quarantine failure or panic was captured; normal idle
  standby drained the renderer and rearmed the BLE reserve. SD transport timeouts
  and I2S disable diagnostics were also observed, matching existing open issues.
  Benchmark autostart/detail have been restored to false. Normal Debug and Release
  builds pass at `6261cdb2`; only Debug was flashed, with hash verification. Its
  95-second `dma-normal-boot.log` capture identifies that version, ordinary SIMD,
  two 8-row startup slots, DISPLAY/TOUCH/WIFI/BLE ready and completed inactivity
  standby with the BLE reserve restored. No benchmark entry, display fault or
  panic was captured. Normal-route performance, visual/touch/PWR correctness and
  repeated wake cycles remain unverified. The stored normal Debug SHA256 is
  `6377266e447ea059f94b94406c07fca13c8335072e07f13ddb8cce84dada2bdf`;
  Release is `7b8a8d5c282ff5157b1be7823409adc4e5b0ba9eab964478f015a5d6b6e6521f`.
  `git diff --check` and the changed-files secret scan pass for this increment.
- Integrated the selected 16-pixel SIMD and transparent row/block skips with
  current main's pairing, standby, mouth and boot-audio changes. Ordinary S3
  Pearl rendering now starts on SIMD in Debug and Release; portable host
  rendering starts on scalar. Benchmark autostart/detail are disabled, normal
  placement is retained, and RGB565 composition is unchanged. Added a host
  assertion for initial backend selection. Portable native, SIMD-model and raster
  benchmark contracts pass after updating the installation-scene expectations to
  main's current heading and scale. Combined Debug and Release builds pass at
  `e714737` through `device.ps1` with eight jobs; both have benchmark autostart
  disabled. Build logs, image hashes and portable reports are retained in each
  mode's `artifacts/build-cody-companion-{debug,release}/evidence/` directory.
  The combined image was not flashed or physically timed; earlier fixed-route
  measurements below belong to the SIMD worktree benchmark image.
- The same 16-pixel SIMD and row-coverage path completed fixed 40-second device
  windows at smaller scales. At 75%, 975 frames submitted 35,256 pixels each:
  mean 33,888 us, p95 34,512 us, maximum 35,147 us, and zero budget misses.
  At 50%, 975 frames submitted 15,600 pixels each: mean 21,834 us, p95
  22,335 us, maximum 23,179 us, and zero misses. These are SIMD-only scale
  measurements, not new C/SIMD A/B comparisons. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-coverage-75.log`
  and `cody-simd-coverage-50.log` beside it. The authored scale and flashed
  firmware were returned to 100%; the restored app payload matches the
  measured 100% payload, with only image metadata/checksum bytes differing.
- The fixed 100% Pearl Mist S3 run with 16-pixel SIMD blocks and generated
  per-row coverage completed 766 frames at 63,000 submitted pixels per frame:
  mean 50,715 us, p95 51,463 us, jelly mean 30,736 us, and 766/766 frames over
  40 ms. Coverage skipped 14,544,808 pixels (18,988 per frame) before packing;
  host scalar/SIMD-model parity and full generated coverage checks passed. The
  frame mean is 4,897 us lower than the 16-pixel zero-skip version and 6,235 us
  lower than the fresh C control. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-coverage-100.log`.
- The intermediate 16-pixel all-zero skip completed 699 frames: mean 55,612 us,
  p95 56,374 us. Enlarging blocks to 64 pixels regressed to 59,794 us mean and
  p95 60,519 us over 652 frames because fewer transparent blocks were excluded.
  Replacing constant `/255` in RGB565 composition with an exact shift/add form
  preserved host pixels but regressed to 51,497 us mean and p95 52,299 us over
  752 frames, so that source change was removed. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-16-zero-skip-100-2.log`,
  `cody-simd-64-100.log`, and `cody-simd-coverage-div255-100.log` beside it.
- Intrusive SIMD profiling at 100% completed 508 frames. Per-frame jelly mean
  rose to 57,052 us because 3,952 blocks triggered many clock reads. Its
  accumulated segments divided by 508 were approximately preparation 19,673 us,
  interpolation 6,947 us and RGB565 composition 19,216 us; the difference from
  jelly total is unassigned instrumentation/loop time. These numbers rank work,
  not uninstrumented costs or FPS evidence. The 64-pixel experiment above
  demonstrated why larger packed blocks are unsuitable for this artwork.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-simd-detail-100.log`.
- Fresh C control on the same 100% route: 684 frames, mean 56,950 us, p95
  57,634 us, jelly mean 37,106 us; all missed 40 ms. Against the 697-frame SIMD
  run below, SIMD reduced frame mean by 1,104 us (1.94%), p95 by 1,139 us
  (1.98%) and jelly mean by 1,226 us (3.30%). Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-scalar-100-fresh.log`.
  This is a measured fixed-pose gain; it does not meet the 25 FPS budget.
- Measured the S3 `simd` kernel on the same fixed 100% Pearl Mist route for 40
  seconds: 697 frames, 63,000 submitted pixels each, frame mean 55,846 us and
  full-run p95 56,495 us. Jelly mean 35,880 us; 31,434,700 pixels used SIMD,
  139,400 used scalar tails and 11,567,412 skipped all-zero footprints. All
  697 frames exceeded the 40 ms budget. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-simd-100.log`.
  The fresh C control above confirms a small gain for this fixed scene.
- Flashed the S3 Debug verifier with all written hashes accepted. In the second
  80-second reset/capture, the actual PIE kernel passed 65,536 weight pairs and
  262,144 reference samples, then compared 30,267,144 live RGBA samples across
  489 measured frames without a mismatch. The benchmark finished, restored the
  scalar renderer and excluded contaminated timing data. The first capture was
  interrupted by touch after 229 measured frames; neither capture reported a
  panic. Evidence: `artifacts/build-cody-companion-debug/evidence/cody-simd-verify-2.log`.
  This proves observed parity for the fixed workload, not a performance gain.
- Built `0883de76` as ESP32-S3 Cody Debug with ESP-IDF 6.0.2 and eight jobs.
  The S3 assembly and C++ validation source compiled; the final ELF links
  `cody_jelly_bilinear_s3`. App image: 3,725,904 bytes, 75% partition free;
  ELF SHA256 `5f615d5437f90b4d762642b9647f92db319751f3b657f0c75bb591d964b94908`.
  The authorized host-check attempt stopped before assertions with Windows
  `WinError 216`: the specified ESP Clang produced an executable Windows cannot
  launch. After updating one stale screenshot-control assertion to the authored
  hidden behavior, the full `cody-raster-benchmark` suite passed with MSVC,
  including the host SIMD arithmetic model over all 65,536 weight pairs. Reports
  were retained under `artifacts/build-cody-companion-debug/evidence/` before
  transient-artifact cleanup. Target execution is still pending.
- Implemented an opt-in ESP32-S3 PIE bilinear sampler (source review only):
  two RGBA pixels per eight 16-bit lanes, 40-bit accumulation with the same three
  rounded lerps, aligned row scratch, unchanged RGB565 composition and scalar
  odd tails. Explicit `scalar`, `simd_verify` and `simd` benchmark selections
  preserve backend identity and restore the ordinary scalar path on exit.
  Added an independently computed 256x256-weight/guard contract and live sample
  comparisons for `simd_verify`; this mode suppresses performance summaries.
  Extended host contracts with a labeled arithmetic model, packer/byte-order/
  clipping/tail checks and deliberate mismatch injection. Generated configuration
  selects verification, not a claimed fast path. At the source checkpoint,
  tests, build and S3 execution had not run; no speed or target parity is claimed.
  The preceding `5947db83` detail-only flash command was cancelled during CMake
  reconfiguration, before flashing, when the user withdrew COM access. No build/
  flasher processes remained. Last verified device image remains `4ac6c812`.
- Built and flashed raster baseline `4ac6c812` with ESP-IDF 6.0.2, Debug, eight
  jobs and COM10 at 460800 baud; all written hashes verified. App: 3,721,952
  bytes, 75% partition free. Matching ELF SHA256:
  `ecce55a8922eaffe430db7531c2084f9c2618b29c8fa3decf8c1b956a7d7bf97`.
  The completed 80-second reset/capture contains one boot and no captured panic
  or reboot. Fixed Ready/Pearl Mist at 100%, coarse profiling, produced 684
  measured frames in the 40-second window, each submitting 63,000 pixels:
  mean 56,922 us, p95 57,611 us, max 58,605 us, all 684 over the 40 ms budget.
  Stage means: geometry 2,781 us, raster 40,886 us, transfer 12,243 us;
  artwork subsets: jelly 37,080 us, vector face 3,003 us. Logged steady FPS was
  about 17.0--17.2, with worker-core CPU around 85--86%. The run completed,
  restored the transient standby override/look and entered the deferred greeting.
  Minimum logged worker free stack: 3,440 of 12,288 bytes. SD initialization
  timed out three times and unrelated long log records were truncated; these
  conditions remain part of this baseline. Host contracts, interruption cases,
  other sizes/detail mode and SIMD gains remain unverified. Evidence and exact
  ELF/bin are retained under
  `artifacts/build-cody-companion-debug/evidence/cody-4ac6c812*`.
- Reworked the temporary raster benchmark in `feat/simd-perf-opt` (source review
  only): worker-owned Debug/Pearl entry, forced fixed Ready redraw, 5-second warmup
  and 40-second measurement, core-1 affinity, bounded PSRAM full-run p95 and
  40 ms misses. Restores transient look/standby settings and deferred greeting;
  power, capture, touch and workload changes interrupt rather than certify a run.
  Coarse fused rendering is separate from opt-in 16-pixel block profiling; shared
  profile types no longer depend on the Pearl feature gate. Added finite-window
  and reference pixel-parity contracts through `cody-raster-benchmark`, not run.
  Regenerated runtime settings/manifest through the public generator; its demo
  channels now also match the already-authored/current generated rig presets.
  No tests, firmware build or physical capture ran for this checkpoint, and no
  SIMD implementation or measured gain is claimed.
- Real pairing 07 now allows 90 awake seconds without genuine activity; 05/06
  retain 30 seconds. Its scene standby override is also 90 seconds and refreshes
  on canonical progress, without synthesizing touch or changing saved settings.
  Backend deadline remains five minutes. Debug `0a4f1b87` built with eight jobs,
  flashed to COM10 with verified hashes and booted with the matching ELF identity.
  Source assertions updated but not run; physical 90-second behavior is pending.
  Saved Wi-Fi credentials reconnect with DHCP and SNTP in this boot capture.
  SD initially timed out before FAT after reinsertion; after the owner's full
  power cycle, the snapshot reported mounted=yes and card-response=verified on
  attempt 1, without formatting. The owner confirmed a saved PWR screenshot.
  Ordinary idle standby completes at 64.697 s with BLE budget rearm.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-0a4f1b8-deploy.md`.
- Captured the intermittent wake complaint: the earlier renderer had already
  stopped with INVALID_ARG before standby was requested; the first terminating
  operation was outside the capture. After an explicit reset, `bb8fd327` entered
  standby at 64 s and woke on touch at 179 s with wireless ESP_OK, confirmed by
  the owner. No new renderer termination was captured. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-bb8fd32-renderer-repro-20261005.log`.

- Corrected false monolithic BLE admission: SDK 51d9dfd EMI uses separate
  allocations; `0x7800` in the inspected routine is an address offset, not a
  malloc size. Preserve the 34,816-byte aggregate budget with four 8-KiB chunks
  and one 2-KiB chunk, all-or-nothing acquisition and real SDK error handling.
  No SDK patch/allocation hook or Wi-Fi buffer reduction. Debug `bb8fd327`
  built with eight jobs and flashed to COM10 with verified write hashes.
  The owner confirmed the real pairing QR. The passive log records healthy
  BLE wake, provisioning startup, clean inactivity closure with budget rearm,
  then a second provisioning startup without `ESP_ERR_NO_MEM`. Phone credential
  transfer/DHCP remain unverified; no tests or simulator were run.

- Lifetime BLE DMA ownership: one static 34,816-byte reserve survives standby
  and pre-controller failures. After actual SDK teardown Cody parks display DMA
  and rearms the budget; cleanup and reservation errors have separate outcomes
  and canonical pairing fields, so clean Retry/known-off standby remain valid
  if a future budget cannot be restored. Limits and Wi-Fi configuration stay
  unchanged. Debug `a19e0093` built/flashed successfully, but physical Pair
  reproduced `0x101`. Its complete boot capture proved SDK startup succeeds;
  after first clean standby teardown the monolithic rearm fails with 97,443
  bytes free and largest 21,504. The chunked correction above follows this evidence.

- Captured explicit Pair failure on main `a154b677`: the 34,816-byte internal
  DMA hold failed with 70,955 bytes free but largest block 21,504, before manager
  startup or session QR. Explicit Pair now drains/parks display DMA before BLE
  teardown/admission and keeps the gate through controller startup; every path
  attempts restoration and preserves real errors. Hold/headroom limits and Wi-Fi
  configuration remain unchanged. Pre-controller failures retain an existing
  lease; no new lease is allocated by Cancel. Frame admission captures intentional
  parking under the lock to avoid terminating Cody after a fast failed handoff.
  Debug `7e45bf32` built and flashed with verified write hashes. The following
  passive monitor reproduced allocation failure after the drain: 96,243 bytes
  free, largest 21,504. No panic/reboot was observed; the standby path discarded
  the earlier reserve. This led to the lifetime-ownership correction above.
- Debug `143fdb22` built with eight jobs and flashed to COM10 / ESP32-S3
  `a4:cb:8f:db:42:d8`. Esptool verified written data and reset the board.
  Application: 3,755,536 bytes, 75 percent of the app partition free. Includes
  greeting C, regenerated mouth/tongue/QR assets and corrected Retry teardown.
  No tests, simulator or post-flash monitor ran; physical behavior remains
  unverified. ELF/BIN hashes and transcript are preserved in
  `artifacts/build-cody-companion-debug/evidence/cody-143fdb22-deploy.md`.
- Prepared three private vocal greetings with ffmpeg and explicit read-only
  source recipes. The owner selected C (`cody-hello-murmur`) for the boot cue;
  A/B and chime-only remain configurable. Shared authoring helpers pin source
  and rendered identities, while runtime composition retains the original
  1.3-second chime plus 100-ms gap. The selected combined asset needs 90,412
  PCM bytes at 22,050 Hz (about 2.05 seconds); no new device decoder/worker or
  canonical-clock/quiet-policy changes. Local asset generation and independent
  static review completed. On 2026-10-04 the owner confirmed purchased perpetual
  commercial-use licences for the SFX packages; the selected PCM/WAV and matching
  manifest are now included in the asset checkpoint. Debug build/flash completed
  as `143fdb22`; physical acoustics and updated host checks remain unverified.
- Mouth source refresh: widened the shared lip controls 18 percent, reused the
  existing breathing phase for opening/width motion and added a coral tongue
  clipped against the evaluated mouth. Native and host coverage intersect the
  same geometric spans. Fixed budgets are 200 points, 62 cubics, 16 paths and
  124 debug tangents; existing raster arrays/worker stack are unchanged.
  Source implementation, independent review and rig/jelly regeneration are
  complete and deployed as `143fdb22`. Physical appearance and timing remain pending.
- QR copy now distinguishes installation (`Scan to install`) from the live
  session (`Scan to pair`, `Scan me in the app.`). Install/session routes share
  compact cloud placement; actual 40/25-pixel font cells govern title/caption
  separation. Existing QR quiet zones, footer bounds and white theme remain.
  Scene regeneration is complete; physical checks are pending. The Obsidian planning
  atlas has corrected page 23 and a dated final errata page 27; other original
  pages are preserved. Backup and visually inspected page renders are retained
  under `artifacts/build-cody-companion-debug/evidence/qr-atlas/`.
- Passive `9fde2b48` logs confirm the reported graphical wake works even after
  the BLE reservation fails (34,816 bytes requested, largest DMA block 21,504).
  Subsequent Retry contacts fail during Cancel cleanup, with primary result
  `ESP_OK`: teardown incorrectly reserves memory for a future session.
  Removed that future admission from teardown; next Start retains strict
  reservation/preflight and its original error. Cancel now retains the failed
  attempt's cause alongside the actual cleanup result, allowing clean Retry
  navigation to 05. Source integration reviewed and deployed as `143fdb22`;
  physical Retry remains unverified. Capture starts after the original Pair Cody failure, so its
  exact startup stage is not established:
  `artifacts/build-cody-companion-debug/evidence/cody-9fde2b48-pairing-before-reset.log`.
- Passive monitoring of `d766e08c` recorded wake input followed by failed BLE
  DMA reservation (34,816 bytes required; largest block about 21-22 KiB), then
  rollback to standby. A later Interrupt-WDT backtrace enters the log mutex
  from CST9217's point logging inside a critical section. Preserved evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-d766e08c-wake-failures-20261004.log`.
  Removed that logging in the reproducible vendor patch. Standalone graphical
  wake now accepts a radio failure only after confirmed shutdown, preserves the
  original error until verified recovery and still rejects uncertain cleanup.
- Added a bounded 30-second awake-inactivity policy for 05/06/07 returning to
  Welcome 04 without replaying boot. Real touch/pairing progress refreshes it;
  animation pause does not stop the timeout, standby does. Detached cancellation
  blocks new pairing and rejects late route promotion. Returned Welcome waits
  for a fresh tap, keeping the normal typewriter presentation.
- Boot audio now waits for verified canonical time instead of permanently
  suppressing that boot's cue when RTC is invalid. A single temporary 4 KiB
  PSRAM worker respects quiet hours/active state, with pending-only 1 Hz reads
  and real playback/error publication. Static integration review found no
  remaining actionable issue; physical confirmation remains open.
- Built/flashed `9fde2b48` on COM10 (Debug, eight jobs, no test runs): app
  3,721,616 bytes, 75% partition free; all written hashes verified. Matching
  ELF SHA256 starts `c572bfaefa`; ELF/build/flash evidence is preserved under
  `artifacts/build-cody-companion-debug/evidence/cody-9fde2b48*`. This image
  includes the wake/touch/inactivity/deferred-audio changes and raster stage 1;
  successful writes do not establish physical wake, audio or performance behavior.
- Captured 28 deterministic `CODY_companion` stack overflows in 100 seconds
  after flashing `dd9f05d8`. The matching ELF shows the worker/frame/coverage
  call chain alone needs 8,176 bytes against an 8,192-byte stack; enabling cheek
  coverage added 2,496 bytes at that depth. Raised only the PSRAM worker stack
  to 12 KiB, preserving the requested artwork and internal DMA headroom, and
  added Debug minimum-free-stack logging sampled at most every two seconds.
  Evidence: `artifacts/build-cody-companion-debug/evidence/cody-dd9f05d8-reboot-loop.log`.
- Removed the boot website branding together with its layout, fade clock,
  raster layer and damage cache. Sleeping/Waking/Ready and first-setup timing
  remain unchanged; this does not remove attribution from the planning PDFs.
- Built/flashed correction `d766e08c` (eight jobs, Debug, COM10, verified writes).
  App: 3,715,104 bytes, 75% partition free; matching ELF SHA256 starts `70961649e`.
  The first capture stopped after 43 seconds with a closed host serial stream,
  not a captured firmware panic. The following 100-second capture completed:
  one boot, no panic/reboot and successful inactivity standby/radio teardown.
  Cody's measured minimum free stack was 3,524 bytes (peak used 8,764/12,288).
  Steady screen 05 ran about 24.4 FPS with sampled artwork around 14 ms and
  geometry around 3 ms; this is a narrow baseline, not a full-scene guarantee.
  Repeated wake/visual/audio/storage checks remain open. Retained ELF and logs:
  `artifacts/build-cody-companion-debug/evidence/cody-d766e08c*`.
- Raster optimization stage 1: all-zero bilinear footprints skip
  the original three packed lerps; zero-alpha samples preserve destination bytes
  without load/swap/store. Other samples retain their arithmetic. Added separate
  jelly/vector microsecond timings within artwork and reference contracts for
  transparent prefixes, fractional samples, backgrounds, byte order and guards.
  Included in the `9fde2b48` Debug build/flash; no tests or device comparison
  ran. An extra nonzero-footprint branch may offset the saving.
- The earlier Cody BLE startup mitigation reserved a 30,720-byte internal DMA
  block before display startup and parked display frames during radio startup.
  It passed an eight-job Debug build, flash and short capture, but a later
  100-second capture exposed intermittent BLE DMA preflight failure and a
  separate NimBLE teardown panic at standby. The short capture was not proof
  that the reboot loop was fixed. Evidence preserved before flash cleanup:
  `artifacts/build-cody-companion-debug/evidence/cody-standby-wake-20261004.log`.
- Added a larger held BLE startup lease and ordered NimBLE stop/deinit so the
  host cannot free its dynamic context before the stop caller finishes. These
  source changes were built and flashed in `dd9f05d8`; repeated cold boot and
  standby/wake behavior are open in TODO.md.
- Authored distinct Worried (06) and Afraid (08) faces from the planning atlas,
  kept the white 06 QR layout, routed 08 Retry back to 05 only after confirmed
  cleanup, and enabled the Debug FPS/CPU HUD on authored scenes without visible
  bottom actions. These changes were built and flashed in `dd9f05d8`; current
  physical display and pairing checks remain open.
- Refined the full atlas face catalog and softened Cody's cheeks. Replaced the
  blue boot source frames with Imagegen pearl Sleeping/Waking/Ready frames,
  packaged as flash-owned indexed assets. Added session-controlled PWR capture
  with a retired-frame 3, 2, 1 countdown, SD result feedback and an original
  shutter cue; the old touch camera control is hidden. These changes were built
  and flashed in `dd9f05d8`; no current simulator or physical behavior check ran.
- Built `dd9f05d8` Cody Debug with ESP-IDF 6.0.2 and eight jobs, then flashed
  COM10 at 460800 baud. Bootloader, partition table and 3,715,936-byte app
  passed written-data hash verification; the device was reset after flash.
  The app partition retains 75% free space. Evidence:
  `artifacts/build-cody-companion-debug/evidence/cody-dd9f05d8-flash.log`.
  No additional test suite or serial capture was run; successful flashing
  does not establish reboot-loop, standby/wake, graphics or audio behavior.
- Created the English A5 front/back CCP pairing quick-start leaflet in Obsidian
  and `docs/Cody/`, with the proposed ZIP/local-marketplace installation path,
  display-code pairing, ten-second Idle flow, atlas illustrations and note lines.
  Both pages were rendered and visually inspected. No ZIP release, plugin
  install/runtime, firmware, LAN or hardware validation is claimed.
  [Artifact record](docs/planning/2026-10-04-ccp-pairing-quickstart.md).
- Checkpoint `f4cc7e97`: adopted Alpha terminology in active code/tools; boot
  cues, runtime authoring and appearance assets have coordinated names. Persisted
  keys, routes, skin IDs, parameter values, PCM/WAV and image bytes are unchanged.
  Real tests, test doubles and synthetic-host labels remain truthful.
- Final Cody validation on 2026-10-04: ESP-IDF 6.0.2 Debug/Release clean and
  incremental builds and size passed with eight jobs. App binaries are 3,612,288
  and 3,481,856 bytes respectively. All 21 focused host/static checks passed,
  including 32 Alpha tests, 15 response tests, native raster/runtime contracts,
  seven simulator groups, active-profile dependency/legacy closure, whitespace
  and a 99-file changed-secret scan including local checkpoints. Eight host
  captures were visually reviewed (four rotations, three boot states, greeting).
  Native checks explicitly used Windows Python/MSVC; initial environment-selection
  failures remain recorded. PDF binary attributes correct Git classification;
  document bytes were not changed. A separate validation correction aligns the
  Debug scene's brows with its declared Calm preset (`ef184a57`).
  Evidence: `artifacts/build-validation-evidence/journey-20261004/`.
  Global profile validation still fails on the missing `display-touch` canonical
  lock; wake-word locks are also unresolved. No physical device, phone, flash,
  LAN, CCP install/end-to-end session or runtime memory soak was validated.
- Created the 14-page English Codex Connection Atlas with three ImageGen
  concept scenes, circular previews, visual flows and handwriting space on
  every page. All pages were rendered and visually reviewed; the PDF is mirrored
  in Obsidian and `docs/Cody/`. Existing atlases remain unchanged. Document
  review does not validate runtime/LAN/device behavior.
  [Artifact record](docs/planning/2026-10-04-codex-connection-atlas.md).
- Implemented standalone Cody boot assets, front-facing named cloud rig, palette,
  face/contour channels, debug overlays, responsive Windows inspector and
  synthetic stimuli. Added the canonical-snapshot response engine, fatigue/
  stress, charge/microphone/playback response, eye taps and elastic drag.
  [Rig and response](docs/architecture/cody/rendering.md).
- Implemented typed scene phases, first-boot greeting/branding, measured HUD,
  shared cardinal auto-rotation, reusable entrance/teletype and face lifecycle:
  ordinary Calm/Happy/Calm, completed Sleep and dark Wake preparation.
  [Runtime and scenes](docs/architecture/cody/runtime.md).
- Ported the user-approved pearl scanline artwork and exact face warp; added
  pre-raster old/new damage, compact tiles, word conversion, prepared strokes/
  markers, edge shortlists and frame-owned DMA. Narrow historical captures
  reached about 29 FPS; CPU/frame targets remain open for the current image.
  [Rendering](docs/architecture/cody/rendering.md).
- Implemented screen 11's five typed internal-flash looks, stable-ID persistence,
  display-owned activation and asynchronous save. Defaults apply only to an
  absent key; real read/write/unknown-ID failures preserve the previous look.
  [Onboarding](docs/architecture/cody/onboarding.md).
- Added the post-pairing write/readback of `cody_firstboot=false`, the compile-time
  Debug-only screen 39, screen-09 60-second standby, and the source route
  09 -> 10 -> 36 -> 11 -> optional 12 -> 13 -> 14. Screen 11 now has a disabled
  Done footer until an explicit row tap and no first-run Back. At checkpoint
  `ce6ac300`, screen 15 still remained gated on a future real Codex/PC producer;
  the later CCP checkpoint below replaces that gap. Added visually reviewed A04
  pages to both append-only atlases. Source/artifact review only; no build,
  simulator, automated test or device validation.
- Added isolated Cody's Codex Plugin (CCP) packaging with portable and Codex
  compatibility manifests, a standard-library Python MCP STDIO server, a bounded
  CCP1 UDP protocol and DPAPI-protected PC state. Firmware now owns a canonical
  non-secret Codex-link snapshot, six-character display-code pairing, retained
  PC identity and authenticated FIND/READY liveness. The PAIRED/CONFIRM/ACTIVE
  commit reports success only after both retained records exist. Screen 12
  cannot self-advance; verified pairing enters 13, Let's go shows native Idle
  for ten active seconds, 14 searches for the retained PC and only authenticated
  READY enters 15. This is commissioning/liveness, not the future Codex
  message/session transport. Source review only; plugin install/run, checks, build,
  simulator, firewall, LAN and physical-device behavior remain unverified.
- Integrated async Start/Retry/Cancel, per-attempt proof/session QR, credential
  adoption and BLE handoff on main; HTTP remains opt-in. Added white QR themes,
  bounded integer scaling/quiet zones, rounded controls and gesture routing.
  [Pairing](docs/architecture/cody/onboarding.md).
- Checkpoint `f7d1925f`: completed OOBE footer actions, immediate route 07 with
  the current QR, real error/online gates, per-scene transient 60-second standby,
  retained software-wake route/active clocks/paused pose, and separate title/
  body/footer/look/status typing. Cold/deep-sleep reset remains distinct.
  Added the original flash microclick with bounded audio-owner notifications,
  coherent cancellation, quiet/volume/time gates and optional failure reporting.
  Source review/asset preparation only; no new tests/build/simulator/device
  validation. Archive-cue auditions are not firmware assets; their perpetual
  commercial-use licences were subsequently confirmed by the owner on 2026-10-04.
- Implemented native snapshot-only status and Screenshot chrome with atomic SD
  BMP writing and temporary PSRAM; recent preview evidence covers control
  visibility only, not firmware status or storage behavior.
- Implemented trusted clock/quiet preferences, offline greetings, optional first
  RTC sample, deferred first-IP SNTP, measured RTC offset/rate before correction,
  and Cody RTC profile activation. No alarm scheduler/calibration or measured
  power-saving claim. [Clock and RTC](docs/architecture/cody/clock.md).
- Implemented optional Sophia wake-on-word profiles, capability gates, flash
  model, canonical microphone/events, AEC/pause ownership and software wake.
  Locks, full firmware closure and physical behavior remain open.
  [Wake-on-word](docs/architecture/wake-on-word.md).

- 2026-10-05 wake-word revision: made the AFE an internal dependency of the sole
  opt-in flag, excluded disabled voice source/stubs/hooks/publication, added
  wrapper/CMake gates for partition/model/dependency/flash admission, and pinned
  acquisition/fetch/internal ESP-SR to CPU0 away from Cody/Luna rendering on CPU1.
  Reduced the behavior to settled software-standby wake only, rejecting old
  events; removed active-word Cody interaction. Implementation/static review
  only; locks, compilation, tests/scans and physical evidence remain pending.

## Recorded validation boundaries

- Authorized rig/response/native/Tk host checks passed at their checkpoints,
  including native/host model and raster parity. On 2026-10-04, snapshot-integrity,
  cody-native, cody-runtime, cody-response (15) and cody-poc (32) passed, covering
  real host persistence error/retry/activation paths. They do not prove async
  UI races, physical output or changes made afterward.
- Cody greeting/intro/render checkpoints were explicitly built/flashed on COM10.
  Revision `0295963d` later built in Debug with eight jobs and flashed a
  3,550,736-byte app with verified hashes/reset; no monitor or UI/leak-soak
  observation accompanied it. Later source increments need their own validation.
- Earlier live captures showed continuous frames/rotation and did not reproduce
  the intermittent stop/reset report. The first failure capture lacked the
  trigger; neither later captures nor the working-device report establish a fix.
  [Failure evidence](docs/architecture/cody/rendering.md).
- Wake-word integration received build-mode/power/snapshot and console host
  checks plus limited syntax/provenance checks, not a full firmware link/device
  test. Physical audio, rotation, standby, electrical and stack evidence remains
  explicitly open.

## Platform, runtime and tools

- Reorganized 82 owned source/private-header files under responsibility-based
  `src/` folders; kept public headers, component IDs, configuration, vendor code
  and assets in place. Updated CMake and source-contract consumers, added short
  folder READMEs, consolidated 13 Cody guides into five, and shortened root
  rules/backlogs without closing unresolved gates. Static integration review
  only; compilation, checks and device validation remain unperformed.
- Separated physical display/touch from LVGL UI, added standalone hardware locks/
  RGB565/input APIs and consolidated the device console behind `scripts/device.ps1`.
  Added deliberate COM selection, shared native-error formatting, menu/help,
  artifact cleanup/containment and bounded compiler jobs. Recent console changes
  have source review only.
- Implemented SD boot-backlog/previous-log retention and CRC/revision-protected
  native USB readback. Monitor attaches without reset by default; reset is
  explicit. No physical SD/readback validation for this increment.
  [Offline logging](docs/architecture/offline-logging-usb.md).
- Added Debug/Release modes and touch/IMU Deep-sleep policy with explicit wake/
  rejected-entry recovery. Integrated current main source policies and hardware
  consumption inventory; actual current/wake behavior remains unverified.
  [Build modes](docs/architecture/firmware-build-modes.md),
  [Deep-sleep](docs/architecture/release-deep-sleep.md).
- Implemented frame deadlines/watchdogs/DMA leases, retained failure/byte reports,
  grouped companion state and separated snapshot reporters/log/pairing/document
  actions. Source splits and prepared regressions do not complete their release
  gates. [Mitigations](docs/architecture/display-touch-runtime.md).
- Earlier Luna checkpoints implemented canonical snapshots/freshness/CPU,
  synthetic device twin, user-started BLE provisioning, resident bootstrap/lazy
  route documents, guarded SD formatting, stationary calibration, transactional
  standby/Performance Mode/rotation, opt-in HTTP and measured FFT visualization.
- Added isolated profiles/reviewed locks, timestamp-driven generated staging,
  resource budgets, hot-path caches, bounded logger/memory reporting, diagnostic
  sample reuse, parked idle acquisition, PMU battery validity, RTC oscillator-stop
  retention and trusted-clock coalescing. Hardware savings are not established.
- Implemented 12-second watchdog/liveness domains, retained breadcrumbs, bounded
  mutex/SDK-worker lifetimes and error-preserving transactional recovery.
  Forced-stall and stack-margin device checks remain open.

## Leg Edition — 2026-09-27

- Implemented the five-page measured IMU UI, activity/relative-peak indicators,
  lower navigation, fades/dropdown styling, saved motion/sound policy, embedded
  boot WAV and louder movement cues. Serial evidence recorded deliberate level-3
  cues, no repeated table-vibration cue, qualified motion wake and approximately
  25-second display-off. Playback `ESP_OK` does not prove audible output.
- Latest recorded navigation image:
  firmware SHA-256 `69C47732DAC8CD712F71767F16994B52AC2CCCFD0D37A174E66BD7032E7AD492`;
  LittleFS `8282F023B283A4792C22C17B35287C0B96CE6B9701C3BC2A6C85A6BD0996FCC5`.
  Both were verified/flashed on COM8; previews/captures are under
  `artifacts/leg-*`. Latest physical UI, acoustics, levels 1/2, PWR,
  persistence/rotation and disabled-RTC summary remain open.

## Cody service documentation — 2026-10-04

- 2026-10-06 implementation: added nonblocking 20-ms physical press feedback
  through the existing audio worker, independent of scene text suspension.
  Increased Main/Sleep manga symbols by 20 percent and set both fades to 400 ms.
  Removed the redundant global SFX cooldown and added a bounded retry/sequence
  observation for the currently visible icon when audio admission is busy.
  Preserved canonical quiet policy, saved volume, standby cancellation and the
  single shared codec owner. Build, tests and physical audio/visual evidence
  remain pending for this checkpoint.

- Created the ten-page English CCP Service Atlas with discovery/pairing,
  persistence, 100-PC identity filtering, liveness and security-boundary diagrams.
  Added Umberto Giacobbi and hello@umbertogiacobbi.biz to all five Cody PDFs,
  visibly and in metadata. All ten new pages and 58 historical footers were
  visually reviewed; historical page bodies remain pixel-identical at 96 DPI.
  No runtime, LAN or physical-device validation was performed for this document.
  [Artifact and preservation record](docs/planning/2026-10-04-ccp-service-atlas.md).
