# Cody cloud/title scene assets

`scenes.json` is the English-only authoring source for reusable standalone
cloud/title/content scenes. The initial greeting is based on planning-atlas
page 05, screen 04. It shares the existing pearl cloud and snapshot response.

Generate using `scripts/device.ps1 generate -Generator cody-scenes` with an
explicit `--font-path` forwarded through `-ToolArguments`. Layout-only changes
may use `--definitions-only` with the existing committed font. The source must be
JetBrains Mono; no substitute font is selected. The full source font is not
packaged. Derived glyph coverage is renamed **Cody Title Mono** under SIL
OFL 1.1; keep `OFL.txt` with the asset distribution.

`generated/scene_data.hpp` owns immutable scene definitions.
`generated/scene_font.cpp` embeds 19,000 bytes of two-bit printable-ASCII
coverage in firmware flash. `generated/scene_font.2bpp.bin` and `manifest.json`
are for the Windows preview; that binary is not a second firmware embed.

Layout, setting behavior, content lifetime, costs and deferred validation are
documented in `docs/architecture/cody/runtime.md`.
