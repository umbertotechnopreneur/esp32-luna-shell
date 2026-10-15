# Active work

Updated 2026-10-15. The retained firmware implementation queue is complete. Release
`cody-178-g0d82a93f` is installed on COM10; tested CCP source, including the new
presence owner, is installed in the local plugin cache. Host checks, Release
build/size/individual compiled frames, flash and passive capture are recorded in
[done.md](done.md), with the four Home panels and earlier delivery evidence.

The 15 October delivery-wait visual refinement is implemented in source: a lower
electric-blue breathing indicator shared by firmware and Studio. Its final
checks, build, flash and physical visual confirmation remain pending explicit
authorization. The reported `Reply unconfirmed` after a physical question is
also under investigation: current LAN presence is confirmed, but
available broker logs do not identify the missing delivery step for that ticket.
No answer has been recovered, replayed or acknowledged on another caller's behalf.

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
