# Cody palettes

`theme.json` is the central semantic color reference for Cody's device UI and
Studio preview. It records current source colors by role, including text,
surfaces, states, controls, chrome, the assistant-quota and battery perimeter
arcs, and gradients. Native RGB565 words are the
device reference; their sRGB previews are expanded representations, and Studio
RGB888 colors retain their own actual values where these differ.

The Device Studio preview reads its top-level background, foreground, muted,
accent, setup, connection and control palette constants from the
`studio_preview` section at startup. Restart Studio to see changes. Other
Studio colors and existing firmware color constants still live in their
respective source files; changing their corresponding JSON entries alone does
not yet change those consumers. Firmware integration must derive immutable
constants from the theme for both firmware builds and direct host compilations;
Cody's native device renderer must not add a runtime JSON parser. Preserve byte
order, RGB565 interpolation, state meanings and current behavior when migrating
each consumer.

`palette256.json` is the separate indexed artwork palette used by retained rig,
scene and personality assets. Its fixed slots and quantization semantics are not
replaced by the semantic theme.
