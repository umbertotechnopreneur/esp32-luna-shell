# Muse avatar

The renderer and its headers come from [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)
revision d0a65abf70a99647aacea18ed8a0159a824fabe7. The source paths are
esp32/avatar/muse_pixel.c and esp32/components/muse/{muse_pixel,muse_speaking,muse_state}.h.

Cody adds a frame snapshot function so its display owner can retain the indexed
grid and both RGB565 palettes across clipped display transfers. The upstream
pose, palette, speaking gestures and frame drawing are intact. Cody supplies
state, time, touch and optional audio energy. Playback uses Cody's audio service.
The GCC/Clang printf annotation is guarded for the Windows Studio compiler.

The bundled [Apache 2.0 license](LICENSE) applies to the imported source.
