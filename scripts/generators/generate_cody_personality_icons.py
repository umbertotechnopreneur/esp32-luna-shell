"""Isolate the approved ImageGen symbol groups into fixed-palette flash sprites."""

from collections import deque
from pathlib import Path
import hashlib
import json

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/cody/personality-icons"
OUTPUT = ASSETS / "generated"
EXTENT = 64

# Stable identities and explicit group windows in each equal 5 x 3 sheet cell.
CATALOG = (
    ("sleep", "Sleep", 1, (.25, .20, .97, 1.0)),
    ("fear", "Fear", 2, (.20, .22, .80, 1.0)),
    ("joy", "Joy", 3, (.14, .20, .90, 1.0)),
    ("hunger", "Hunger", 4, (.06, .20, .91, 1.0)),
    ("thirst", "Thirst", 5, (.08, .30, .94, 1.0)),
    ("heat", "Heat", 6, (.15, .08, .97, .98)),
    ("cold", "Cold", 7, (.12, .12, .97, .98)),
    ("low-battery", "LowBattery", 8, (.14, .30, .97, .77)),
    ("charging", "Charging", 9, (.10, .30, .97, .78)),
    ("no-wifi", "NoWifi", 10, (.08, .18, .97, .93)),
    ("curiosity", "Curiosity", 11, (.28, .04, .89, .83)),
    ("confusion", "Confusion", 12, (.18, .12, .96, .85)),
    ("affection", "Affection", 13, (.13, .09, .96, .85)),
    ("frustration", "Frustration", 14, (.16, .09, .91, .91)),
)


# data: Exact bytes included in the reproducible provenance manifest.
def digest(data):
    return hashlib.sha256(data).hexdigest()


# image: Explicitly cropped RGBA symbol group from the preserved ImageGen sheet.
# minimum_pixels: Authored speckle cutoff in original sheet pixels, scaled by cell area.
# Raises ValueError when no meaningful authored component remains.
def retain_components(image, minimum_pixels):
    width, height = image.size
    alpha = image.getchannel("A")
    coverage = list(alpha.tobytes())
    seen = bytearray(width * height)
    retained = bytearray(width * height)
    components = []
    for origin, opacity in enumerate(coverage):
        if opacity < 8 or seen[origin]:
            continue
        queue = deque([origin])
        seen[origin] = 1
        pixels = []
        while queue:
            offset = queue.popleft()
            pixels.append(offset)
            x, y = offset % width, offset // width
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                nx, ny = x + dx, y + dy
                if not 0 <= nx < width or not 0 <= ny < height:
                    continue
                neighbor = ny * width + nx
                if not seen[neighbor] and coverage[neighbor] >= 8:
                    seen[neighbor] = 1
                    queue.append(neighbor)
        keep = len(pixels) >= minimum_pixels
        xs = [offset % width for offset in pixels]
        ys = [offset // width for offset in pixels]
        components.append({"pixels": len(pixels), "retained": keep,
                           "bounds": [min(xs), min(ys), max(xs) + 1, max(ys) + 1]})
        if keep:
            for offset in pixels:
                retained[offset] = coverage[offset]
    clean_alpha = Image.frombytes("L", image.size, bytes(retained))
    if clean_alpha.getbbox() is None:
        raise ValueError("Symbol crop has no meaningful retained component")
    image.putalpha(clean_alpha)
    return image.crop(clean_alpha.getbbox()), components


# data: Immutable index/coverage bytes to emit as a flash-owned C++ array.
def cpp_bytes(data):
    return "\n".join("    " + ", ".join(str(value) for value in data[start:start + 24]) + ","
                     for start in range(0, len(data), 24))


# No parameters: package the preserved fixed sheet and palette into deterministic assets.
# Raises OSError for source/output failures and ValueError for malformed source identities/palette.
def main():
    palette_path = ROOT / "assets/cody/palette/palette256.json"
    # Palette authoring is UTF-8 text; Windows line endings must not change provenance.
    palette_bytes = palette_path.read_text(encoding="utf-8").encode("utf-8")
    palette = json.loads(palette_bytes)
    entries = palette["entries"]
    if len(entries) != 256 or [entry["index"] for entry in entries] != list(range(256)):
        raise ValueError("Palette must retain all 256 entries in canonical slot order")
    if len({entry["id"] for entry in entries}) != 256:
        raise ValueError("Palette has duplicate domain identities")
    colors = [tuple(bytes.fromhex(entry["rgb888"].removeprefix("#"))) for entry in entries]
    allowed = [entry["index"] for entry in entries if not entry["reserved"]]
    if not allowed:
        raise ValueError("Palette has no non-reserved color slots")
    source_path = ASSETS / "source/cody-manga-symbols-v1.png"
    source_bytes = source_path.read_bytes()
    with Image.open(source_path) as original:
        if original.mode != "RGBA":
            raise ValueError("ImageGen symbol source must have explicit RGBA transparency")
        sheet = original.copy()
    cell_width, cell_height = sheet.width / 5, sheet.height / 3
    if abs(cell_width - cell_height) > 1:
        raise ValueError("Symbol source must be the authored 5 x 3 square-cell sheet")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "palette.rgb565.bin").write_bytes(b"".join(
        int(entry["rgb565"], 16).to_bytes(2, "little") for entry in entries))
    preview = Image.new("RGBA", (5 * 96, 3 * 96), (0, 0, 0, 0))
    source_lines = ['// Generated by scripts/device.ps1 generate -Generator cody-personality-icons.',
                    '#include "personality_icons.hpp"', 'namespace CODY::PersonalityAssets {',
                    'const uint16_t palette[256] = {',
                    "    " + ", ".join(entry["rgb565"] for entry in entries), "};"]
    descriptors = []
    manifest = {"schema": "cody.personality-icons.v1", "source": source_path.relative_to(ROOT).as_posix(),
                "source_sha256": digest(source_bytes), "source_size": list(sheet.size),
                "palette_source": palette_path.relative_to(ROOT).as_posix(), "palette_sha256": digest(palette_bytes),
                "quantization": "nearest squared sRGB distance; lowest non-reserved slot breaks ties; no dithering",
                "coverage": "separate unpremultiplied alpha8; no reserved transparency color",
                "component_policy": "explicit group windows; 4-connected alpha >= 8; minimum 100 pixels per 324x324 cell",
                "master_extent": EXTENT, "icons": []}
    minimum_pixels = max(1, round(100 * cell_width * cell_height / (324 * 324)))
    identities = [item[2] for item in CATALOG]
    if len(set(identities)) != 14 or len({item[0] for item in CATALOG}) != 14:
        raise ValueError("Symbol catalog identities must be unique")
    for offset, (name, cpp_id, identity, window) in enumerate(CATALOG):
        column, row = offset % 5, offset // 5
        crop = [round((column + window[0]) * cell_width), round((row + window[1]) * cell_height),
                round((column + window[2]) * cell_width), round((row + window[3]) * cell_height)]
        isolated, components = retain_components(sheet.crop(crop), minimum_pixels)
        isolated.thumbnail((EXTENT - 4, EXTENT - 4), Image.Resampling.LANCZOS)
        master = Image.new("RGBA", (EXTENT, EXTENT), (0, 0, 0, 0))
        master.paste(isolated, ((EXTENT - isolated.width) // 2, (EXTENT - isolated.height) // 2))
        indices = bytearray()
        coverage = bytearray()
        quantized = []
        cache = {}
        master_bytes = master.tobytes()
        for pixel_offset in range(0, len(master_bytes), 4):
            red, green, blue, alpha = master_bytes[pixel_offset:pixel_offset + 4]
            rgb = (red, green, blue)
            if alpha == 0:
                slot = allowed[0]
            elif rgb in cache:
                slot = cache[rgb]
            else:
                slot = min(allowed, key=lambda candidate: sum(
                    (channel - reference) ** 2 for channel, reference in zip(rgb, colors[candidate])))
                cache[rgb] = slot
            indices.append(slot)
            coverage.append(alpha)
            quantized.append((*colors[slot], alpha))
        master.putdata(quantized)
        master.save(OUTPUT / f"{name}.png", optimize=False)
        (OUTPUT / f"{name}.indices.bin").write_bytes(indices)
        (OUTPUT / f"{name}.coverage.bin").write_bytes(coverage)
        indexed_png = Image.frombytes("P", (EXTENT, EXTENT), bytes(indices))
        indexed_png.putpalette([channel for rgb in colors for channel in rgb])
        indexed_png.save(OUTPUT / f"{name}.indexed.png", optimize=False)
        Image.frombytes("L", (EXTENT, EXTENT), bytes(coverage)).save(OUTPUT / f"{name}.coverage.png")
        preview.paste(master, (column * 96 + 16, row * 96 + 16))
        symbol = cpp_id.lower()
        source_lines += [f"static const uint8_t {symbol}_indices[4096] = {{", cpp_bytes(indices), "};",
                         f"static const uint8_t {symbol}_coverage[4096] = {{", cpp_bytes(coverage), "};"]
        descriptors.append(f"    {{PersonalityCue::{cpp_id}, 64, 64, {symbol}_indices, {symbol}_coverage}},")
        manifest["icons"].append({"id": name, "enum": cpp_id, "identity": identity,
                                   "sheet_crop": crop, "components": components,
                                   "indices_sha256": digest(indices), "coverage_sha256": digest(coverage),
                                   "used_slots": sorted(set(indices)), "data_bytes": len(indices) + len(coverage)})
    source_lines += ["const Sprite sprites[14] = {", *descriptors, "};", "} // namespace CODY::PersonalityAssets"]
    (OUTPUT / "personality_icons.cpp").write_text("\n".join(source_lines) + "\n", encoding="utf-8")
    (OUTPUT / "personality_icons.hpp").write_text(
        '// Generated by scripts/device.ps1 generate -Generator cody-personality-icons.\n'
        '#pragma once\n#include "CODY_personality.hpp"\nnamespace CODY::PersonalityAssets {\n'
        'struct Sprite { PersonalityCue cue; uint16_t width; uint16_t height; '
        'const uint8_t *indices; const uint8_t *coverage; };\n'
        'extern const uint16_t palette[256];\nextern const Sprite sprites[14];\n}\n', encoding="utf-8")
    preview.save(OUTPUT / "preview.png")
    manifest["flash_payload_bytes"] = 14 * EXTENT * EXTENT * 2 + 512
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated 14 shared 64x64 indexed-alpha sprites; flash payload {manifest['flash_payload_bytes']} bytes")


if __name__ == "__main__":
    main()
