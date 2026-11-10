"""Encode the existing VibeWare logo as bounded RGB565 rows for the About page."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "cody-device/simulator/windows/assets/branding/vibeware-logo.png"
DESTINATION = ROOT / "cody-device/components/CODY_companion/src/scenes/CODY_vibeware_logo.hpp"
SIZE = 240


# No parameters: regenerate the fixed About resource from the checked-in source image.
# Raises file, image or row capacity errors without publishing a partial table.
def main():
    with Image.open(SOURCE) as image:
        source = image.convert("RGBA")
        background = Image.new("RGBA", source.size, (0, 0, 0, 255))
        background.alpha_composite(source)
        image = background.convert("RGB").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        indexed = image.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    rgb = indexed.getpalette()
    palette = [((rgb[i] >> 3) << 11) | ((rgb[i + 1] >> 2) << 5) | (rgb[i + 2] >> 3)
               for i in range(0, 96, 3)]
    pixels = indexed.tobytes()
    offsets = [0]
    runs = []
    for y in range(SIZE):
        row = pixels[y * SIZE:(y + 1) * SIZE]
        x = 0
        while x < SIZE:
            end = x + 1
            while end < SIZE and row[end] == row[x] and end - x < 255:
                end += 1
            runs.extend((end - x, row[x]))
            x = end
        offsets.append(len(runs))
    if len(runs) > 65535:
        raise ValueError("Logo run offsets exceed uint16_t capacity")

    # values: Immutable encoded values in output order.
    # width: Number of values per source row.
    # render: Formatter for one encoded value.
    def lines(values, width, render):
        return "\n".join("    " + ", ".join(render(v) for v in values[i:i + width]) + ","
                         for i in range(0, len(values), width))

    content = (
        "// Generated from the repository VibeWare logo by device.ps1 generate -Generator cody-vibeware-logo.\n"
        "#pragma once\n#include <cstdint>\nnamespace CODY::VibeWareLogo {\n"
        f"inline constexpr uint16_t size = {SIZE};\n"
        "inline constexpr uint16_t palette[] = {\n" + lines(palette, 8, lambda v: f"0x{v:04X}") + "\n};\n"
        "inline constexpr uint16_t offsets[] = {\n" + lines(offsets, 12, str) + "\n};\n"
        "inline constexpr uint8_t runs[] = {\n" + lines(runs, 20, str) + "\n};\n}\n"
    )
    DESTINATION.write_text(content, encoding="ascii", newline="\n")
    print(f"Generated {DESTINATION.relative_to(ROOT)} ({len(runs)} run bytes)")


if __name__ == "__main__":
    main()
