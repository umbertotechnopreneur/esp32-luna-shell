"""Package the approved button illustration for the native onboarding page."""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "cody-device/assets/cody/onboarding"
SIDE = 136


# No parameters: converts the retained master into indexed RGB565 for the display.
# Exception: Propagates source, image conversion and output errors.
def main():
    with Image.open(ASSETS / "source" / "button-press-detail.png") as source:
        image = source.convert("RGB").resize((SIDE, SIDE), Image.Resampling.LANCZOS)
    indexed = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = indexed.getpalette()
    output_bytes = bytearray()
    for index in range(256):
        red, green, blue = palette[index * 3:index * 3 + 3]
        color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output_bytes.extend((color >> 8, color & 0xff))
    output_bytes.extend(indexed.tobytes())
    output = ASSETS / "generated" / "button.rgb565"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(output_bytes)
    print(f"Generated {output.relative_to(ROOT)} ({len(output_bytes)} bytes)")


if __name__ == "__main__":
    main()
