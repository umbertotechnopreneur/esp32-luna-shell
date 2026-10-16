# Active work

The owner's final 16 October diagnostic correction is implemented in source:
remove the facial-channel list, its bounded PSRAM storage and scroll handling,
while keeping internal animation channels and one device diagnostic view in
Release and Debug. The plugin and Studio accept only `character` and `normal`;
the reserved scroll field is zero. Final USB delivery and publication are pending.

Release `cody-196-gda3e03ad` remains installed on COM10 with the soft 1.8-second
question bell and quieter 300-ms reminders on the ten-second cadence. Its two
diagnostic commands returned authenticated applied ACKs; `character` was reopened
after the owner removed the facial-channel view from scope. These ACKs do not
verify visible pixels. New host suites, physical sound quality/timing and nested
target stack headroom remain unverified.

The owner's 15 October enclosed-board reference import and static assessment are
recorded in [done.md](done.md). New 1.75C/1.43C hardware support remains deferred
as HARDWARE-PORT-01 in [planned-todo.md](planned-todo.md); those boards are not
covered by the existing firmware profile.

Updated 2026-10-16. The offline standby/farewell and faster software-wake changes
are implemented in source, together with a longer first question alert and
gentle reminders every ten awake seconds while a fresh question awaits Send.
Release `cody-196-gda3e03ad` built with ESP-IDF 6.0.2 and eight jobs and was installed
on COM10 with all five written hashes verified. Wake word and the experimental
activity classifier remain selected. Their native timer/owner/voice/lifecycle/
question-cadence checks are prepared but have not been run. No new test suite,
simulator, standalone size report, serial capture or physical observation was performed.
Validate USB/battery PC absence, pairing/confirmation deferral, farewell cancellation,
sound/quiet policy, retained-state rollback, manual-only offline wake, nested stack
headroom and actual wake latency only after the owner's fresh explicit authorization.
The authored software-wake timeline is 1.4 seconds; this is not a device timing result.

Earlier delivery: Release `cody-187-g58b67f3b` was installed on COM10 on 15 October;
tested CCP source, including the new presence owner, is installed in the local
plugin cache. Host checks, Release
build/size/individual compiled frames, flash and passive capture are recorded in
[done.md](done.md), with the four Home panels and earlier delivery evidence.

The 15 October delivery-wait visual refinement is implemented in source: a lower
electric-blue breathing indicator shared by firmware and Studio. Its Release
build and COM10 flash passed on 15 October with the written image hash verified.
Fresh host/simulator checks and physical visual confirmation of this indicator
remain pending explicit authorization. One separately authorized question on the
preceding firmware completed with recipient acknowledgement and owner-confirmed physical display
success, as recorded in done.md. The earlier `Reply unconfirmed` was not
reproduced; available broker logs cannot establish its missing delivery step.
No other caller's answer was recovered, replayed or acknowledged.

Normal-EOF reporting loads in existing development clients when Codex next
reopens. Already-loaded clients supply no normal-close proof and remain
Unavailable on release. After broker restart, a genuine new prompt restores
followed chat metadata; no activity is synthesized to fill that gap.

The owner accepts WAV attachment delivery to this existing Codex chat and declines
separate transcription APIs/services/costs (15 October). The public experimental
queue pilot preserved all 359406 WAV bytes with matching SHA-256 in this chat.
Codex prompt preparation replaces unsupported audio with the observed omission
message before the model sees it; no transcription or understanding is claimed.

The audio receiver now uses an explicit installed native executable and the
public queue, with a reused queue-only stdio client. Setup is
`device.ps1 codex-audio -EnableAudioQueue`; it probes read-only access and stores
no API credentials. Completed WAVs are admitted once to the frozen followed chat;
uncertain admission is not retried. Source implementation, local validation and
installed adoption are distinguished in [done.md](done.md). Physical recording
and model-side speech understanding remain in VOICE-01. No private endpoint,
model-modality override or discarded launcher/proxy is restored.

Readable tool captions are active on the PC, including a known-ID table and
complete underscore-cleaned unknown names. Final host, installed-cache and
authenticated post-reload adoption evidence is recorded in done.md.

The latest Studio run passed 28 of 29 cases; Windows desktop screen capture
failed in ImageGrab. Native pixel previews passed and remain available. This
unavailable capture is retained as STUDIO-CAPTURE-01 in [planned-todo.md](planned-todo.md).

GitHub visibility is public. Main ruleset 25049727 is active: contributors need
a pull request; only the owner's user identity has direct-push bypass.

CCP-USAGE-01 in [planned-todo.md](planned-todo.md) retains physical display/audio
confirmation during normal genuine chat and compaction events. It is not inferred
from synthetic screenshots, current PC metadata or authenticated LAN presence.

The user's deferred voice and walking/classifier trials remain in
[planned-todo.md](planned-todo.md), together with physical wake/shutdown, energy
measurements, nested stack headroom and native Desktop audio/runtime verification.
Future built-in speech understanding still needs a usable public ingress;
existing Desktop questions cannot be imported retroactively. Moving a gate does not mark it passed or request
user action now. A restarted CCP broker awaits a genuine user prompt to follow a chat.

The accepted SD/PCB limitation is unchanged. No model training or classifier-led
automatic shutdown is included. This queue grants no execution authorization;
follow [AGENTS.md](AGENTS.md) and the current user instruction.
