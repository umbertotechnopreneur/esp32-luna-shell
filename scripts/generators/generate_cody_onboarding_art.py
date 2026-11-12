"""Prepare the two authored onboarding backgrounds for the native display."""

from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "cody-device/assets/cody/onboarding"
SIDE = 466


# No parameters: converts retained source artwork into indexed RGB565 resources.
# Exception: Propagates source, image conversion, and output failures.
def main():
    for name in ("welcome", "sofia"):
        with Image.open(ASSETS / "source" / f"{name}.png") as source:
            image = source.convert("RGB").resize((SIDE, SIDE), Image.Resampling.LANCZOS)
        indexed = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        palette = indexed.getpalette()
        pixels = bytearray()
        for index in range(256):
            red, green, blue = palette[index * 3:index * 3 + 3]
            color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            pixels.extend((color >> 8, color & 0xff))
        pixels.extend(indexed.tobytes())
        output = ASSETS / "generated" / f"{name}.rgb565"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(pixels)
        print(f"Generated {output.relative_to(ROOT)} ({len(pixels)} bytes)")


if __name__ == "__main__":
    main()
