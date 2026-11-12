# Muse sound effects

This library contains reusable, independently selectable pet cues. The six
authored sounds are a startup ding, the growing birth sound, a two-chirp
greeting, a touch pop, a jump and a surprised reaction. The jump is available
for a future jump animation. Manga symbols and Working currently reuse the
existing Codex action recordings through the independent pet audio channel.

The birth sequence plays the rising sound during the white-to-black transition,
then the greeting as Muse appears and says Welcome. Ordinary power-on also
requests the startup ding. Playback follows the device's sound and quiet-hour
settings and shares its speaker with other audio.

`source-selection.json` records the read-only source files, hashes, excerpts,
mixes and envelopes. The generated mono PCM is embedded in firmware; WAV files
are convenient previews. To recreate the assets using the owner's source
library, run `scripts/device.ps1 generate -Generator cody-pet-sfx`. A different
archive location can be passed using the generator's `--archive-root` option.
The source archive's licensing terms also apply to these derived samples; see
the existing Codex action audio README for the owner's source-library notes.
