<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Muse sound effects

This library contains reusable, independently selectable pet cues. The six
authored sounds are a startup ding, the growing birth sound, a two-chirp
greeting, a touch pop, a jump and a surprised reaction. The jump is available
for a future jump animation. Manga symbols and Working currently reuse the
existing Codex action recordings through the independent pet audio channel.

On the first boot, Cody requests the separate onboarding melody and the birth
sequence is silent. Later ordinary power-ons request the startup ding; when the
birth introduction is explicitly replayed, the rising sound and greeting remain
available. Playback follows the device's sound and quiet-hour settings and
shares its speaker with other audio. First-boot playback and timing still need
physical verification.

`source-selection.json` records the read-only source files, hashes, excerpts,
mixes and envelopes. The generated mono PCM is embedded in firmware; WAV files
are convenient previews. To recreate the assets using the owner's source
library, run `scripts/device.ps1 generate -Generator cody-pet-sfx`. A different
archive location can be passed using the generator's `--archive-root` option.
The source archive's licensing terms also apply to these derived samples; see
the existing Codex action audio README for the owner's source-library notes.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
