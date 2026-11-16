# Latest validation round

## 2026-11-16 · Cody Studio compact atlas and new host cases

The latest verified complete coverage remains 98 native cases. A compact
20-page PDF from those captures is published in Documenti; each page retains
three handwriting lines. The additional reply-ready, checks-passed, Debug
provisioning and three static voice cases are source additions and are not yet
part of that published atlas.

Linux host validation through `scripts/device.ps1`:

- The native graphics driver compiled before the latest source corrections.
- The combined `--native --inspection --events-ui --atlas-pdf` suite ran 71
  tests: 69 passed and two failed. The P04 CLI worker could not import `yaml`
  in the bundled Python. The Debug provisioning case rejected its native
  caption because the full BLE name exceeded the presenter's text width.
- The compact PDF checks passed, including embedded native pixels, inventory
  identity and three handwriting lines. Fifteen event fixtures repeated and
  restored P04; real Tk opened the added event states. Voice fixtures repeated
  and restored P04, but a source review subsequently identified possible prior
  microphone sample leakage that this suite did not exercise.
- Source corrections now shorten the Debug caption without truncating the BLE
  name and isolate the synthetic voice microphone projection while preserving
  the underlying observation. A new regression check seeds different prior
  levels and checks restoration; these corrections require a fresh native build
  and separate host suites before acceptance.

The most recent compact atlas was made from the previously verified 98-case
run, not the six new source cases. Local changes outside these checkpoints were
preserved. No firmware build or flash, physical-device operation, human audio
acceptance or push was performed. Windows and device acceptance remain separate.
Generated artifacts are retained; cleanup remains manual.
