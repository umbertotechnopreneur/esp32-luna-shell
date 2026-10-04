# Cody gentle SFX listening candidates

Ten short excerpts from the local SONY Loops archive: one eye-close candidate
and nine presence/activity variations. The source archive remains read-only.
This folder contains selection metadata only; no source or rendered audio is
versioned here, and no playback is connected to the firmware.

Generate through the repository's public entry point:

```powershell
.\scripts\device.ps1 generate -Generator cody-gentle-sfx
```

The generator requires `ffmpeg` and `ffprobe` on PATH. To use another copy of the
same archive, supply `-ToolArguments @('--archive-root', 'D:\Local Sfx')`.
Outputs stay in `artifacts/audio/cody-gentle-sfx/`: ten WAV candidates,
`cody-gentle-sfx-audition.wav`, and `manifest.json`. The audition starts after
one second of silence, separates each cue by two seconds, and ends with two
seconds of silence. Its cue offsets are recorded in the manifest.

Each candidate is mono 16 kHz PCM16, 200–720 ms long, with a target peak of
-26 dBFS, reduced high-frequency content, and a smooth fade at both edges.
The generator selects an active fixed-length passage within each configured
source window; exact source hashes, trims, filters, gain, duration, file size,
and audition offsets are recorded for review. The first and last PCM samples
are explicitly zero. No added melody, voices, alarm or success notification is
authored. A listening review is still required to assess the source excerpts.
Digital level alone cannot establish perceived gentleness or speaker volume.

## Proposed sparse use

- `cody-eye-close`: reserve for a deliberate eye-close expression, at most once
  in 20 seconds; routine blinking stays silent.
- `cody-soft-rustle`, `cody-page-nudge`, `cody-cloth-settle`: alternate small
  settle/fidget gestures.
- `cody-pencil-thought`, `cody-doodle`: occasional visible idle activity.
- `cody-water-drift`, `cody-tiny-bubble`: isolated curious or drifting gestures.
- `cody-idle-murmur`, `cody-small-stretch`: rare presence/stretch variations.

During long pairing waits, keep the first 45 seconds quiet, then allow at most
one presence/activity cue every 45–90 seconds, with irregular gaps and no
immediate repeat. Couple a cue to an actual visible gesture; waiting time or a
network retry alone should not trigger it. Never loop these sounds or use them
as evidence that pairing has completed. These are authoring recommendations;
no automatic triggers have been implemented.

## Source rights

The original sample licences and redistribution rights have not been verified.
These outputs are local private listening candidates. Keep the generated WAVs
out of commits, firmware packages, downloads and public distribution until the
source licences have been reviewed. Repository licensing does not grant rights
to these external sample recordings.
