# Cody symbol sounds

First authored pass of fourteen original cartoon-style effects, synthesized
analytically without external recordings, voices or samples. Each cue has a
16-kHz mono PCM16 WAV for listening and a raw PCM copy. The generated C++ catalog
stores the same samples in internal flash, keyed by `PersonalityCue`.

[Listen to all fourteen](audition-all-14.wav): Sleep, Fear, Joy, Hunger, Thirst,
Heat, Cold, LowBattery, Charging, NoWifi, Curiosity, Confusion, Affection,
Frustration. The reel lasts 12.33 seconds with 700-ms gaps. Individual sounds
last 170-300 ms; peak level is -18 dBFS. These are generation parameters, not a
listening judgment or a measurement of physical speaker loudness.

`manifest.json` records recipes, durations, hashes, provenance and reel offsets.
The native sample payload totals 103,360 bytes before descriptors and code.
WAV headers and the listening reel are not linked into the firmware.

Regenerate using the sole public CLI:

```powershell
./scripts/device.ps1 generate -Generator cody-personality-audio
```

One appearance in a successfully completed frame may request one sound. The
audio owner drops busy requests, enforces eight seconds between playback starts
and expires unstarted requests after 120 ms. Quiet hours, mute and display
suspension can suppress playback. There is no replay queue. Global enable is
session-only; per-cue enable lives in the authored generator/catalog.

No listening, build, simulator or hardware validation has been performed for
this first pass. Refine the recipes after listening and actual speaker review.
