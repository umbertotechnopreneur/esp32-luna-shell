"""Quantize the preserved RGB565 surprise masters for flash-efficient rendering."""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "cody-device/assets/cody/surprise"
PREVIEWS = ROOT / "artifacts/cody-surprise-preview"
SIDE = 466


# path: Preserved big-endian RGB565 master with the authored display geometry.
# Exception: Rejects a missing or incorrectly sized master without inventing artwork.
def read_master(path):
    raw = path.read_bytes()
    if len(raw) != SIDE * SIDE * 2:
        raise ValueError(f"Unexpected RGB565 master size: {path.name}")
    rgb = bytearray(SIDE * SIDE * 3)
    for pixel in range(SIDE * SIDE):
        color = (raw[pixel * 2] << 8) | raw[pixel * 2 + 1]
        red, green, blue = (color >> 11) & 31, (color >> 5) & 63, color & 31
        rgb[pixel * 3:pixel * 3 + 3] = bytes(((red << 3) | (red >> 2),
            (green << 2) | (green >> 4), (blue << 3) | (blue >> 2)))
    return Image.frombytes("RGB", (SIDE, SIDE), bytes(rgb))


# name: One explicitly curated surprise image, not an arbitrary filesystem path.
# Exception: Propagates image conversion, validation and file output errors.
def generate(name):
    source = read_master(ASSETS / f"{name}.rgb565")
    indexed = source.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = indexed.getpalette()
    colors = bytearray()
    display_palette = bytearray()
    for index in range(256):
        red, green, blue = palette[index * 3:index * 3 + 3]
        color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        colors.extend((color >> 8, color & 0xff))
        r, g, b = (color >> 11) & 31, (color >> 5) & 63, color & 31
        display_palette.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    output = ASSETS / "generated" / f"{name}.indexed565"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(colors + indexed.tobytes())
    # The preview uses the exact quantized RGB565 colors that the device will draw.
    indexed.putpalette(bytes(display_palette))
    preview = indexed.convert("RGB")
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    preview.save(PREVIEWS / f"{name}.png")
    source_bytes, preview_bytes = source.tobytes(), preview.tobytes()
    total_error = sum(abs(a - b) for a, b in zip(source_bytes, preview_bytes))
    maximum_error = max(abs(a - b) for a, b in zip(source_bytes, preview_bytes))
    print(f"{name}: {len(output.read_bytes())} bytes, mean channel error "
          f"{total_error / len(source_bytes):.2f}/255, max {maximum_error}/255; "
          f"preview {PREVIEWS / (name + '.png')}")


# No parameters: convert exactly the three retained authored surprise masters.
# Exception: Stops on the first invalid master or failed output.
def main():
    for name in ("portrait", "vietnam", "italy"):
        generate(name)


if __name__ == "__main__":
    main()
