# Third-party source provenance

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
