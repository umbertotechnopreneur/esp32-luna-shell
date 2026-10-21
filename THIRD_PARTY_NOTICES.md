# Third-party source provenance

## License scope

The root [MIT License](LICENSE) covers project-owned material. Third-party
sources, models and assets keep their original copyright and license notices,
including local modifications to imported files. This index records provenance;
the license files accompanying each component remain authoritative.

## Independence, trademarks and product imagery

The author is not affiliated with, sponsored by, or employed by Espressif or
Waveshare. ESP32 Luna Shell and Cody Companion are independent projects and are
not endorsed or certified by either company.

All third-party product names, trademarks, logos and images belong to their
respective owners. References identify the products and technologies involved;
they do not imply a partnership or endorsement and do not change any license.

The root README displays the existing Waveshare manufacturer photos
`product-photo-2.jpg`, `product-photo-4.jpg`, `product-photo-6.jpg` and
`product-photo-7.jpg` from
`hardware/references/waveshare/ESP32-S3-Touch-AMOLED-1.75C/images-product/`
to identify the purchased aluminum-case model. These images remain third-party
material, outside the project's MIT license.
The original collection and provenance are retained in the
[hardware reference index](hardware/references/waveshare/README.md).

## Waveshare board support package

- Component: `components/esp32_s3_touch_amoled_1_75`
- Upstream: <https://github.com/waveshareteam/Waveshare-ESP32-components>
- Upstream revision used: `32d86900af6916d5e6eb629741d57ee19a577740`
- Component version: `3.0.1`
- License files and upstream headers are preserved in the component.

## ESP-Brookesia

- Source: official monorepo submodule at `third_party/esp-brookesia`
- Upstream: <https://github.com/espressif/esp-brookesia>
- Pinned upstream revision:
  `5361118b9953800e4fe710e57e5314fccec9cbcf`
- Selected framework components: System Super `0.8.1`, System Core `0.8.1`,
  GUI LVGL `0.8.1`, and their official monorepo dependencies
- Apache-2.0 license: `third_party/esp-brookesia/license.txt`

The component manifests resolve the selected framework components from this
pinned submodule. The monorepo layout is retained so official relative
component relationships remain intact.

Managed ESP-IDF component versions are recorded in `dependencies.lock`.

## Purchased Waveshare hardware reference snapshots

- Location and provenance: [hardware reference index](hardware/references/waveshare/README.md).
- Products: ESP32-S3-Touch-AMOLED-1.75C and ESP32-S3-Touch-AMOLED-1.43C.
- Supplied folders imported on 2026-10-15; source examples, firmware, images,
  schematics, manuals and collection notes remain reference material outside
  the project's firmware component roots.
- The 1.75C archive carries Apache-2.0; its top-level license is also preserved
  as `software/UPSTREAM-LICENSE.txt`. Bundled subcomponents retain their own terms.
- The 1.43C archive has no top-level license. Its `codec_board` components retain
  the Espressif Modified MIT License, restricted to Espressif products. No
  project-wide license is inferred from their availability on GitHub.
- Vendor images, documentation and binaries are not relicensed under the root
  MIT License. Preserve their notices and determine applicable redistribution
  terms before any future publication; no publication is part of this import.

## Native eSpeak NG

- Imported source: `third_party/espeak-ng`, from eSpeak NG 1.52-dev through
  `pschatzmann/arduino-espeak-ng` revision
  `93b2651d324874e1e00077af008d8ab83ab32e73`.
- [Source provenance and local changes](third_party/espeak-ng/ORIGIN.md).
- Preserved license files: [GPLv3](third_party/espeak-ng/COPYING),
  [Apache](third_party/espeak-ng/COPYING.APACHE),
  [BSD](third_party/espeak-ng/COPYING.BSD2), and
  [Unicode data](third_party/espeak-ng/COPYING.UCD).
- Matched runtime data: `assets/tts/espeak-ng`.

The optional speech profile includes imported sources with their own licensing
requirements. The root MIT License does not relicense them; see the
[speech distribution notes](docs/bsp/services/text-to-speech.md).

## Wake-word assets

`assets/wake-word` retains its [Espressif license](assets/wake-word/LICENSE),
including its Espressif-products condition. The project MIT License does not
replace those terms.

## VibeWare artwork

The VibeWare artwork comes from Umberto Giacobbi's VibeWare repository. The
simulator copy retains its [license](simulator/windows/assets/branding/LICENSE)
and `provenance.json`; the firmware animation's provenance is in
[the asset guide](assets/branding/VibeWare/README.md).
Use the upstream [press kit](https://github.com/umbertotechnopreneur/VibeWare/blob/main/PRESS-KIT.md)
for identity and reuse guidance. Branding does not establish verification or
release status for this project.
