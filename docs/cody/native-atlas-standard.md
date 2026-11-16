<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Studio native atlas standard

**VibeWare · Cody Companion**

This standard governs the inventory-linked Studio native screen atlas. The
[annotation film standard](annotation-overlays.md) governs printable overlays.

## Layout

- Use portrait A4 and an embedded JetBrainsMono Nerd Font Mono (regular and
  bold). Install the fonts on the host before exporting; the PDF embeds the
  glyphs and does not depend on the recipient's fonts. Keep all text English.
- Place up to three screens per page in maintained catalog order. Every fixed
  slot has a smaller square native frame on the left and concise identity and
  source on its right. Mark isolated presenters and nondefault variants or failures. Do not
  stretch, reconstruct, or replace missing images. Selected unavailable cases
  show a short reason instead of an invented frame.
- Keep explanations short and avoid repeating common status beside every image.
  Full capture details, scenario revision, hashes and limitations remain in the
  immutable Studio manifest and captured metadata; inventory revision and build
  identity are also recorded in the PDF properties. Do not add evidence,
  coverage or correction-register text pages to the visual atlas.
- Preserve the three existing writing lines at the bottom of every page and the
  simulation/physical-acceptance qualification. Never turn acquisition into
  approval.
- Keep the same positions for screen slots across pages and preserve the
  readable image proportions. Short absence excerpts do not replace the full
  reason retained in the manifest.

## Export and annotation films

Export only a validated immutable Studio run through
`scripts/device.ps1 generate -Generator cody-native-screen-atlas`. The default
is the published last-valid run; `--manifest` selects another retained run.
The export validates identities and hashes and fails if current mappings no
longer match. It does not rerun the simulator or update evidence.

The earlier `cody-native-atlas-films` geometry was designed for three screen
slots. It must be recalibrated and visually validated for the current three-slot layout
before another film is generated; no existing overlay should be assumed to align.
Print validated films at actual size / 100%, without fit-to-page or additional
mirroring. Copy final documentation PDFs byte for byte to the configured
Obsidian atlas when that location is available.

`CODY_FILM_FONT_DIR` may point to a directory containing licensed `segoeui.ttf`
and `consola.ttf` when those standard film fonts are not in the system font
locations. Generated outputs stay beneath `artifacts/`; cleanup is manual.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
