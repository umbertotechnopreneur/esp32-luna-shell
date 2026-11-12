# First-boot melody

The original 4.6-second startup melody is synthesized from the project-owned
generator. The generated mono 16 kHz PCM is embedded in the firmware and the
matching WAV is an audition copy. Neither file is sourced from an installed
application or a prebuilt release.

Generate both files from source with
`scripts/device.ps1 generate -Generator cody-onboarding-boot-music`.
The display owner consumes a separate durable one-shot marker after the first
visible boot frame and before requesting the music. A suppressed, failed or
interrupted attempt is not replayed on an ordinary restart. If the marker
cannot be saved, the melody is withheld and its original persistence error is
logged. The last floppy frame remains visible while an admitted cue is active,
subject to a bounded failure deadline. Sounds and quiet-hours policy remain
with the shared audio owner. Physical playback and flash capacity are unverified.
