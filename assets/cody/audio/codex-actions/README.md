# Cody boot and Codex action sounds

The 2026-10-10 set uses four recordings from the owner's purchased local SFX
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

BootRise is a warm overlapping rise with a soft tail at 22,050 Hz. The action
cues are mono 16 kHz PCM16, each within the existing 350-ms owner limit.
Boot peaks at -13 dBFS; actions peak at -20 to -26 dBFS. These are authored
digital levels, not measured loudness on Cody's speaker. Boundary samples are
zero. No looping sound, continuous busy bed or speech is included.

Trigger once on a semantic state change, never on each status packet, repaint,
poll, tool output chunk or reconnect attempt. The display remains authoritative:
a deliberately suppressed, expired or busy-dropped cue must not delay activity
presentation or replay later. Reading is prepared as an asset only until its
event can be observed reliably.

## Rights and review

The owner confirmed perpetual commercial-use source licences on 2026-10-04 and
requested this selection on 2026-10-10. This records the owner's statement;
licence documents were not found in the inspected archive, whose PDFs are
package indexes. Package terms remain authoritative. Repository licensing does
not relicense the original sample packs.

Asset generation is complete. Listening review, device playback and firmware
validation have not been performed for this set.
