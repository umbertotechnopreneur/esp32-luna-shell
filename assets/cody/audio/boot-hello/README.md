# Boot greeting auditions

Three short vocal candidates from the read-only local SFX archive, shaped with
ffmpeg. These are pseudolanguage experiments, not verified English "hey"/"hi"
recordings. The owner selected C (`cody-hello-murmur`) for the boot cue;
A (`cody-hello-chatter`), B (`cody-hello-gurble`) and the original chime remain
available. Perceived intelligibility and device acoustics still need listening.

```powershell
.\scripts\device.ps1 generate -Generator cody-boot-hello
```

Requires ffmpeg/ffprobe on PATH. An alternate archive can be supplied with
`-ToolArguments @('--archive-root', 'D:\Local Sfx')`. Selection, phrase boundaries,
pitch and additional post-pitch tempo are in `source-selection.json`; the total
speed multiplier is their product, not an independent tempo control. Output stays in
`artifacts/audio/cody-boot-hello/`, not in versioned firmware assets.

Each candidate has a mono 22,050 Hz PCM16 WAV and raw PCM, plus a `-boot` preview
containing the unchanged original chime, 100 ms of silence and the greeting.
Filters remove low rumble/high hiss and fade both boundaries; the normalized
peak is -9 dBFS. This is a digital level, not a speaker loudness measurement.
The manifest records exact source hashes, filters, sample counts and output
hashes. Only the original chime identity is accepted: no implicit fallback or
duplicate greeting if an already-combined boot asset is supplied.

The audition generator never replaces a firmware asset. It extracts only the
SHA-pinned original chime prefix when preparing previews, so rerunning it after
selecting a firmware greeting cannot append that greeting twice.

## Selected firmware cue

`assets/cody/runtime.yaml` explicitly selects `audio.eyes_open_hello`. Set it to
one of the three full cue IDs or `none` (chime only); unknown IDs fail fast.
Preparation reads the external archive and requires the exact authored source
and rendered PCM hashes. There is no replacement cue if the source is missing,
modified, silent or fails ffmpeg processing.

```powershell
.\scripts\device.ps1 generate -Generator cody-runtime-assets
```

This appends C to the existing 1.3-second eyes-open chime at authoring time:
57,330 chime bytes + 4,410 gap bytes + 28,672 vocal bytes = 90,412 flash PCM bytes
(about 2.05 seconds). Only one variant is embedded, not all three. The YAML's
chime duration remains 1.3 seconds; the manifest records each component and
the combined duration independently.

Canonical-clock waiting, quiet-hours suppression and the once-per-boot trigger
are unchanged. An ordinary software-standby wake does not repeat the boot cue.
Busy typewriter microclicks keep their existing nonblocking drop policy while
the boot sound owns the codec. No extra decoder or audio worker is introduced.

On 2026-10-04 the owner confirmed that the source SFX packages were purchased
with perpetual licences including commercial use. This is owner-confirmed
provenance, not an independent inspection of the licence documents. The selected
derived PCM/WAV and matching manifest can be checkpointed as Cody assets;
the earlier precautionary commit/distribution block is removed. Package terms
remain authoritative, and repository licensing does not relicense the external
source sample packs. The source archive remains read-only.

Local asset preparation does not establish playback, perceived loudness or
successful deployment.
