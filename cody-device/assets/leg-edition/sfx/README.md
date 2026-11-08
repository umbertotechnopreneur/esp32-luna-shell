# Leg Edition motion cues

These are original, short notification cues synthesized with FFmpeg. They are
staged as project assets for a later copy to the SD card; no physical card is
accessed by this repository change. The SONY Loops source library was inspected
but not modified or redistributed.

| File | Intended feedback | Duration |
| --- | --- | --- |
| `movimento_lieve.wav` | One soft pip | 160 ms |
| `movimento_elevato.wav` | Two descending pips | 400 ms |
| `scossone.wav` | Two high pips and one lower, longer pip | 780 ms |

All files are mono signed PCM16 WAV at 16 kHz, matching the board speaker API.
The current firmware synthesizes the equivalent cues on demand to avoid SD
read races while the storage service owns mount/unmount. The WAVs are not yet
loaded from the SD card. They are movement reminders, not a calibrated
weight-bearing or medical-safety alarm.
