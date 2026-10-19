# Expensive model notification

The short plink comes from **Kenney, Interface Sounds 1.0**, `Audio/pluck_001.ogg`,
distributed under **CC0-1.0**. The [original pack](https://kenney.nl/assets/interface-sounds)
and its download were checked on 2026-10-14. The exact selected source and the
pack's license are retained in `source/`; `source-selection.json` pins source
and archive SHA-256 identities. This CC0 source has its own license boundary,
independent of the purchased recordings used by other Cody sounds.

Generate through the public CLI:

```powershell
.\scripts\device.ps1 generate -Generator cody-model-cost-audio
```

The generator verifies the pinned source, decodes mono 16-kHz PCM16 with existing
ffmpeg, limits the excerpt to 220 ms, applies 150-Hz/5-kHz filters and short edge
ramps, and sets a digital peak of -18 dBFS. The manifest records the actual
length and hash. One flash-owned sample supplies five repetitions with 100-ms
gaps through the existing audio owner; silence and repeated PCM are not stored
five times. A listening preview is written to
`artifacts/audio/cody-model-cost/model-cost-five-plinks.wav`.

The intended event is the first visible Expensive price after a model change.
Panel rotation, taps and temporary source loss do not create another event.
Playback keeps the existing volume, quiet-hours, recording and busy admission.
Digital asset checks and host playback do not prove physical speaker volume
or successful audible delivery.
