# Active work

## Canonical Git delivery - 2026-10-20

- All source work is consolidated on main and codex/cody; obsolete branches
  were removed only after verifying their history is retained.
- Debug cody-229-g027800b1 built successfully from source with wake word and
  the activity classifier. Build provenance and hashes are in done.md.
- The owner explicitly limited this delivery to Git and build. The new Debug
  image was not flashed; the earlier physical acceptance gates below stay open.

## Standby wake hardening - delivered, physical acceptance pending

- Source checkpoints 70678d9f and 99b167b3 decouple native touch-IRQ wake from
  coordinate-read success, retain resource-level RTC breadcrumbs and report
  them before boot-log overflow, and flush an SD session identity before
  asynchronous writer admission.
- Release cody-224-g99b167b3 was built and flashed on COM10 on 17 October.
  All five segment hashes were verified; the tool requested its RTS reset.
- The original failure remains unproven. Verify physical touch/PWR wake,
  rejected-touch-report behavior, retained diagnostics after a reset and the
  current-image SD identity before claiming device acceptance.
- No automated tests, simulator, separate size, dependency/legacy scans or
  whitespace check were run for this narrower build/flash request. The
  existing source-contract lookup was aligned without executing the check.
- GCC crashed twice in unchanged ESP-DSP dspi_conv_f32_ansi.c during complete
  recompilation (eight/four jobs). Incremental retries passed at four/one job;
  no source or compiler-flag workaround was applied. The toolchain cause
  remains undiagnosed; retain both failures.
- Evidence and recovered historical logs:
  artifacts/build-cody-wake-evidence-20261017/.

## Earlier delivery record

No unresolved gate remains for the authorized 17 October delivery. The Debug
footprint, missing profile locks, PDF corrections and backlog reconciliation are
complete. All 48 automatic checks and four selected builds/size reports passed;
checkpoint commits were pushed and the Release firmware was flashed over USB.
Exact source versions, image hashes, initial failures and final delivery evidence
are in [done.md](done.md).

Physical pixels, touch, acoustics, measured wake latency, current, endurance and
nested device stack headroom remain separate from host checks/build/flash.
Voice/walking/classifier trials and speech-to-Codex follow-up stay deferred;
WAV delivery does not establish model-side speech understanding. Keep wake word,
the accepted SD/PCB limitation and observational HAR without model training or
automatic classifier-led shutdown. Specific remaining physical gates and broader
source/board/product work are in [planned-todo.md](planned-todo.md).

## Repeated approval-needed prompts - host checks passed, PC activation pending

- Observation bookmark: 17 October 2026, 08:45:39 Asia/Saigon (UTC+07:00),
  equivalent to 01:45:39 UTC. The owner resumed investigation, authorized the
  announced CCP suite and targeted recovery, and requested a new firmware flash.
- The source correction limits informational approval admission to one notice per
  physical recipient and native turn. Back, expiry and partial delivery errors
  cannot reopen an already admitted notice. Resumed tool activity retires only
  that chat's advisory approval notice. Native decisions remain on the PC.
- Consolidated CCP evidence: 147/147 passed. The initial invocation passed 146
  cases and failed the native parser case because the Visual Studio environment
  was not loaded. The explicitly reauthorized native parser check passed after
  loading that existing environment; both invocations are retained.
- Release firmware cody-220-gce83bd10 was built at eight jobs and flashed on COM10.
  All five segment hashes were verified, followed by the tool's RTS reset request.
  This does not establish physical pixels, touch, audio or wake behavior.
- PC plugin 0.6.10 is installed through the public Codex CLI after direct
  owner confirmation in this PC chat. The installed manifest and notice source
  were inspected. The owner will restart Codex later; plugin reload and a genuine
  new prompt remain required for live activation. No hook events are replayed.
  The earlier automatic-review rejection and completed installation are retained
  in done.md and the local evidence rather than treated as a current blocker.
- Historical accepted PermissionRequest events were not traced, so the exact
  cause/count of the original repetitions remains unproven. New broker traces
  retain fixed outcomes and hashed native identities without tool/chat contents.
- At the earlier inspection the broker reported zero observed chats. Historical
  invalid transcript-locator and capability-authentication errors remain separate
  observations, not errors claimed resolved by the notice-admission correction.
- The supported SD-log download failed with esp_err=263 / errno=0; no historical
  device log was recovered. Keep the accepted SD/PCB limitation separate.
- Preserved evidence: artifacts/build-cody-approval-evidence-20261017/. The earlier
  artifacts/approval-investigation-20261017/ was copied here before the required
  build/flash cleanup removed that transient directory. Live device acceptance
  of the PC notice correction remains pending.
