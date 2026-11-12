# Muse typefaces

Muse Gadget SDK revision d0a65abf70a99647aacea18ed8a0159a824fabe7 supplies
these source fonts in esp32/components/muse/eink/fonts. The round Muse UI
uses the same typefaces through LVGL 9.5.0.

| Source | Muse usage | License |
| --- | --- | --- |
| unscii.ttf | UNSCII 8 and 16 for pixel text, status and captions | Public domain, Viznut |
| montserrat.ttf | Montserrat Medium for menus and larger text | [SIL OFL 1.1](Montserrat-OFL.txt) |

The files are upstream subsets of the LVGL 9.5.0 font sources. The Montserrat
license is included from that release. Cody can use these sources to bake
glyph coverage for its native CODY2D font format.

On first import, verify the official source and version before use.
