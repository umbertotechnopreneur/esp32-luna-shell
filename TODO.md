# Active work

Updated 2026-10-15. The retained firmware implementation queue is complete. Release
`cody-178-g0d82a93f` is installed on COM10; tested CCP source, including the new
presence owner, is installed in the local plugin cache. Host checks, Release
build/size/individual compiled frames, flash and passive capture are recorded in
[done.md](done.md), with the four Home panels and earlier delivery evidence.

Normal-EOF reporting loads in existing development clients when Codex next
reopens. Already-loaded clients supply no normal-close proof and remain
Unavailable on release. After broker restart, a genuine new prompt restores
followed chat metadata; no activity is synthesized to fill that gap.

The optional native audio installer now has a `codex-audio` CLI/menu route with
explicit dependency installation, live Desktop ownership/chat checks and atomic
per-user configuration. Final host checks passed; local package adoption is
recorded in [done.md](done.md). The inspected Desktop host uses stdio and owns no TCP listener, so
actual native endpoint activation remains unavailable in VOICE-01.

The owner approved a public experimental audio queue pilot on physical Cody;
the exact reply was acknowledged as delivered. Native initialization, queue
listing and one synthetic `localAudio` WAV admission passed. A temporary standard
stdio queue writer never loaded/resumed a chat or requested inference. The pilot
left the queue, but the resulting Desktop turn reported that audio content was
omitted because audio input is unsupported. Speech was not transcribed or
understood. Keep physical submission through this route disabled. Do not replay
the pilot automatically or restore the rejected custom launcher/proxy.

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
Native audio still requires a verified attach endpoint; existing Desktop questions
cannot be imported retroactively. Moving a gate does not mark it passed or request
user action now. A restarted CCP broker awaits a genuine user prompt to follow a chat.

The accepted SD/PCB limitation is unchanged. No model training or classifier-led
automatic shutdown is included. This queue grants no execution authorization;
follow [AGENTS.md](AGENTS.md) and the current user instruction.
