# Cody palettes

`theme.json` is the central semantic color reference for Cody's device UI and
Studio preview. It records current source colors by role, including text,
surfaces, states, controls, chrome, the assistant-quota and battery perimeter
arcs, and gradients. Native RGB565 words are the
device reference; their sRGB previews are expanded representations, and Studio
RGB888 colors retain their own actual values where these differ.

Studio reads its authored colors from the JSON at startup. Restart Studio to
see changes; a packaged executable contains the palette present at packaging
time and must be rebuilt to include later edits. Native firmware uses immutable
RGB565 constants generated from `device` in the same JSON. Run
`scripts/device.ps1 generate -Generator cody-theme` after editing the palette;
the public build, simulator and check commands also synchronize that header.
Native rendering does not parse JSON. Keep byte order, color interpolation,
state meanings and artwork indexing intact when adding or changing roles.
Navigation material colors also come from the semantic theme; navigation.json
owns geometry and timing. Regenerate cody-theme and cody-rig after editing them.

`palette256.json` is the separate indexed artwork palette used by retained rig,
scene and personality assets. Its fixed slots and quantization semantics are not
replaced by the semantic theme.
