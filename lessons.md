# Lessons worth keeping

Observed failure patterns and their remedies. Policy belongs in [AGENTS.md](AGENTS.md),
open work in [TODO.md](TODO.md), and dated evidence in [archive.md](archive.md).
Read the startup, memory and build sections before boot/Brookesia changes.

## Startup and memory

- **A handoff must finish before its memory is reusable.** Keeping the 32-KiB
  boot backlog internal until after Cody's ready frame forced emergency DMA and
  a frame timeout. Wait for the logger owner's post-free archival result, not
  merely an accepted request or detached pointer.
- **Budget nested copies, not just individual functions.** Full snapshot frames
  of 3,904/3,792 bytes exhausted the 8-KiB power stack. Schema growth also exposed
  two copies in the 12-KiB acquisition worker and a 5,616-byte GATT frame on a
  4,096-byte NimBLE task. Use bounded projections and locked owner scratch;
  recheck all callers after growth, including fresh display state after wake.
- **Small graphics can have large scratch costs.** The worker/frame/coverage
  chain reached 8,176 bytes: startup's 12-KiB stack drew successfully while the
  8-KiB worker overflowed. A successful boot frame is not worker headroom evidence.
- **External allocation does not relocate linked BSS.** TTS `57121ca2` exhausted
  internal RAM before creating the mandatory 8-KiB power task. Explicit linker
  placement and admitting optional workers after mandatory owners fixed the design.
- **PSRAM has cache constraints.** NVS/cache-disabling flash commits need the
  internal-stack settings/storage executors. Capability-allocated tasks require
  matching deletion APIs. Allocation-failure hooks must not allocate or log;
  long active clocks need wide saturating arithmetic.

## Display and radio ownership

- **A frame owns DMA until confirmed completion.** Quarantine ambiguous transfers
  after deadlines; partial frames invalidate comparison history. Commit geometry
  only after success, erase old and new bounds, and respect native pixel byte order.
- **Free RAM is not contiguous DMA.** CO5300 QSPI uses bounded internal bounce
  strips here; direct PSRAM DMA is unsafe. Two requested buffers do not prove
  overlap. Inspect admitted slots and complete-frame costs, including strip-command
  overhead and deferred touch work.
- **Reserve BLE memory before SD/display startup.** Park and drain display DMA
  during radio startup and before explicit Pair telemetry teardown. Keep aggregate
  and per-block budgets distinct. An assembly address offset is not an allocation
  size: the old monolithic guard rejected healthy controller restarts.
- **Record parking while holding its lock.** Re-reading a quickly reopened gate
  once turned deliberate frame parking into a terminal renderer failure. Keep BLE
  reserve ownership across standby/errors; rearm only after confirmed SDK closure.
  A failed future reservation must not pretend a closed session is still alive.
- **Measure completed work.** A working-device report or capture begun after a
  failure cannot identify its trigger; a mismatched ELF invalidates backtraces.
  Compare frame stages, payload, CPU and an actually static route, not FPS alone.
  [Rendering](docs/cody/rendering.md) has the current contracts.

## Power, wake and acquisition

- **Policy reapplication is not failed state verification.** Setting
  `consistent=false` just to force a callback broke paired Cody's LAN gate.
  Use an explicit reapplication path; service requirements may legitimately retain
  software standby even when Release permits Deep-sleep.
- **Acquisition time counts toward cadence.** Waiting a full sampling period
  after acquisition/commit repeatedly invalidated HAR windows. Schedule the remaining
  deadline, preserve real timestamps and reject actual gaps.
- **Low lock cost does not exclude scheduler starvation.** IDF can pin a floating-
  point task on first FPU use. The 20-Hz activity owner needed explicit CPU1
  affinity beside higher-priority voice work; do not hide it by changing other priorities.
- **Restore while dark.** Complete Sleep before closing writes; restore consumers,
  then producers, before exposing Wake. Lock display/brightness/rotation before
  settings; require fresh orientation before replay. Roll back failed transitions
  or retain explicit inconsistency. Recheck diagnostic microphone ownership under lock.
- **Software standby and Deep-sleep have different state lifetimes.** Retain route,
  pose and active clocks only in software standby, reconciling backend changes on
  return. Scene timeouts must not persist global settings or renew activity each frame.
  Positive pairing overrides suppress early dim; Deep-sleep resets RAM/session owners.
- **Know the actual wake wiring.** PWR is PMU PKEY, not SD GPIO41; PMU/RTC interrupts
  lack a direct ESP wake route. Touch GPIO11 and IMU INT2/GPIO21 are separate
  candidates. QMI WoM needs revision-aware CmdDone/ACK, explicit exit and release of
  RTC holds/mux after rejected sleep. Keep touch scanning alive.
- **Optional radio failure needs known-off cleanup before graphical wake.** Keep
  the real error even if graphics resume. Never log inside touch critical sections:
  filtered ESP logs can still acquire the log-level mutex and break event-list operations.
- **Chip sleep figures are not board current.** Native USB does not survive
  Light-sleep here; keep unsafe IDF 6.0.2 SOF/unsupported retention hooks disabled.
  Shared powered GPS rails prevent Backup claims. Retain the measured 80-MHz floor;
  codec shutdown and lower floors need physical evidence.
- **Slower polling is not power-off.** RTC oscillator-stop stays unavailable until
  trusted write/readback. LC76G ACK timing, terminal parsing and bounded read backoff
  matter to the shared bus. Reuse each fresh sample once; coalesce duplicate logs
  with immediate error/recovery and counted 60-second reminders, not hidden faults.

## Audio and protocol lifecycle

- **WakeNet startup is not continuous listening.** Playback locks or DMA retirement
  can starve AFE fetch. Keep the acknowledged RX owner independent of TX waits;
  acknowledge pause before route close. Convert boot PCM in bounded chunks to the
  fixed duplex clock, supplying identical speaker/AEC samples.
- **Sound follows what was displayed.** Typewriter audio uses retired visible-prefix
  deltas. Scroll/replacement/resume re-prime silently; cancellation and pending audio
  share one generation/mailbox. Admission does not prove playback or justify
  suppressing codec/fetch errors.
- **Transport delivery and device retirement differ.** Hold a terminal question
  until authenticated EVENTACK before publishing its successor. Dwell expiry,
  accepted credentials, one strip write or watchdog progress is not completion.
- **A deadline cannot kill a wedged SDK call.** Keep buffers and Task-WDT ownership
  until actual worker exit; recoverable deadlines need margin below Task-WDT.
  Repeated fast errors are neither success nor necessarily a hang.

## Builds, UI and diagnostics

- **Set up the existing compiler before authorized native checks.** `vswhere`
  needs `-prerelease` to find Visual Studio Insiders. Load `Launch-VsDevShell.ps1`
  with x64 host/target; missing PATH is not permission to install another compiler.
- **IDF discovers components before Kconfig.** Admit roots/defaults and reviewed
  build-local locks explicitly; optional-requires cannot discover excluded roots.
  Gate headers too. Codec admission alone does not enable boot WAV.
- **Fresh build roots start empty.** Assets/stamps belong to their own build.
  Existing Release output does not select Release for the next request.
- **CMake and source contracts move with responsibilities.** Keep acquisition,
  timestamps, persistence and publication at their owners. Moving DPAPI/launcher
  code into modules does not implement other-platform support.
- **Core runtime and Super overlays have different lifetimes.** One lazy route
  owns its document/subscriptions/maps; unload releases captures. Destroy callbacks
  outside registry locks, retain real partial failures and intern only static styles.
  Bindings write final values, coalesce masks, then notify; no-op values need no batch.
  Use actual declarative schemas, not invented transparency/popup properties.
- **Text still has to fit its real font.** A 22-character caption occupied 243 pixels
  in a 232-pixel slot and stopped screen 15 on `da37aaac`. Share bounded instructions;
  do not widen guards or drop renderer errors to hide it.
- **Optional logging must stay optional.** Queue pressure is counted delivery loss,
  not terminal SD failure. Capture zero-wait internally, archive sealed records,
  and advance replay only after flush. Keep uncertain I/O ownership and no reset
  watchdog/startup dependency on logging. Memory estimates need linked/runtime evidence.
- **Passive serial capture must stay passive.** Monitor can reset an attached MCU;
  COM order is not identity. Preserve native error/exit codes, own artifacts before
  writing diagnostics, and keep secondary log failures separate. Capture before
  best-effort USB output; release storage locks before transmitting. CRCs prove
  transfer identity, not a freeze's cause.
- **History needs dates, not stale branch names.** After consolidation, record the
  merged capability and commit. Ignored artifacts can disappear; retain results
  in tracked records without promising that old local files still exist.
