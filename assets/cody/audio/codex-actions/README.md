# Cody boot and Codex action sounds

The 2026-10-10 set uses seven recordings from the owner's purchased local SFX
archive. Only transformed Cody cues are versioned. Original source recordings
stay read-only outside the repository. `source-selection.json` records their
relative paths, SHA-256 identities, chosen windows, pitch, filters, envelopes,
mix gains and provenance. The generated manifest retains exact transformations,
PCM hashes, durations and digital levels.

Generate using the existing public entry point:

```powershell
.\scripts\device.ps1 generate -Generator cody-codex-sfx
```

An alternate archive location is explicit:
`-ToolArguments @('--archive-root', 'D:\Local Sfx')`. ffmpeg and ffprobe must
be on PATH. Missing or changed source samples fail preparation; no substitute
is selected. Generated WAV/PCM and the typed C++ asset catalog are in
`generated/`. The combined listening preview is
`artifacts/audio/cody-codex-sfx/cody-codex-sfx-audition.wav`, with one second
before the first cue and between cues. Its offsets are recorded in the manifest.
Individual WAV previews remain beside the versioned PCM so a build cleanup
does not remove the selected sounds.

| Cue | Duration | Intended actual event |
| --- | ---: | --- |
| BootRise | 1,800 ms | Once-per-boot greeting through the existing boot owner |
| Working | 230 ms | Selected chat starts a turn |
| Reading | 210 ms | Reserved for an observed reading action; never inferred |
| Editing | 240 ms | Entry into an observed edit operation |
| Running | 280 ms | Entry into an observed command operation |
| ResponseReady | 350 ms | Assistant response complete; does not imply task success |
| Paused | 260 ms | Paused or waiting for explicit permission |
| Interrupted | 230 ms | User interruption |
| SessionEnded | 340 ms | Selected chat actually ends |
| ConnectionLost | 340 ms | Authenticated PC liveness expires |
| ConnectionRestored | 340 ms | Authenticated PC liveness returns |
| Question | 340 ms | New question asks for the user's decision |
| AnswerReceived | 250 ms | Agent confirms reply delivery |
| ChecksPassed | 350 ms | Explicitly observed checks pass |
| ChecksFailed | 340 ms | Explicitly observed checks fail |
| ApprovalRequired | 300 ms | Permission must be handled on the PC |

BootRise is a warm overlapping rise with a soft tail at 22,050 Hz. The action
cues are mono 16 kHz PCM16, each within the existing 350-ms owner limit.
Boot peaks at -13 dBFS; actions peak at -14 to -20 dBFS. These are authored
digital levels, not measured loudness on Cody's speaker. Boundary samples are
zero. No looping sound, continuous busy bed or speech is included.

Questions use rising bell notes, failed checks use descending bell notes, and
confirmed reply delivery uses a short digital receipt. These identities are
independent of the assistant response-ready cue. The more audible notice levels
retain digital headroom and do not override the user's speaker volume or Sounds switch.

Trigger once on a semantic state change, never on each status packet, repaint,
poll, tool output chunk or reconnect attempt. The display remains authoritative:
a deliberately suppressed, expired or busy-dropped cue must not delay activity
presentation or replay later. Reading is prepared as an asset only until its
event can be observed reliably.

`CODY_codex_audio.hpp` provides `notify_codex_audio`, `set_codex_audio_suspended`,
`set_codex_audio_enabled` and `get_codex_audio_status`. The existing optional
owner handles these cues through one shared mailbox; `Dispatched` is admission,
not completion. Codex activity requests are limited to one start every 1.5
seconds. Other notices skip that limit but do not interrupt an in-flight sound.
Codex presentation starts suspended and has a cancellation generation separate
from personality-symbol visibility. Volume zero, quiet hours, global sound
policy and standby still apply. Quiet hours remain configurable and default Off
only when their settings key is absent; existing preferences are preserved.
Real failures remain in the owner's status and
canonical snapshot. An outcome from another channel has `cue: None` in the
Codex view; observers must correlate cue and request sequence.

## Rights and review

The owner confirmed perpetual commercial-use source licences on 2026-10-04 and
requested this selection on 2026-10-10. This records the owner's statement;
licence documents were not found in the inspected archive, whose PDFs are
package indexes. Package terms remain authoritative. Repository licensing does
not relicense the original sample packs.

Asset generation and the authorized native/runtime host checks are complete.
The integrated Debug wake-word firmware built with eight jobs and was flashed
on COM10 on 10 October 2026 at `da37aaac` (`cody-26-gda37aaac`); all four image
hashes were verified. Listening review and actual speaker playback remain
unverified; compilation and installation do not establish audible behavior.
