# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Isolate the approved ImageGen symbol groups into fixed-palette flash sprites."""

from collections import deque
from pathlib import Path
import hashlib
import json

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/cody/personality-icons"
OUTPUT = ASSETS / "generated"
EXTENT = 256

# Stable domain identities shared by native firmware and the Windows preview.
CATALOG = (
    ("sleep", "Sleep", 1),
    ("fear", "Fear", 2),
    ("joy", "Joy", 3),
    ("hunger", "Hunger", 4),
    ("thirst", "Thirst", 5),
    ("heat", "Heat", 6),
    ("cold", "Cold", 7),
    ("low-battery", "LowBattery", 8),
    ("charging", "Charging", 9),
    ("no-wifi", "NoWifi", 10),
    ("curiosity", "Curiosity", 11),
    ("confusion", "Confusion", 12),
    ("affection", "Affection", 13),
    ("frustration", "Frustration", 14),
)


# data: Exact bytes included in the reproducible provenance manifest.
def digest(data):
    return hashlib.sha256(data).hexdigest()


# image: Single native RGBA symbol master preserved from ImageGen.
# minimum_pixels: Authored speckle cutoff scaled by native source area.
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


# indices: Full logical palette-index plane; slot zero has no alpha meaning.
# coverage: Full logical alpha8 plane; even alpha=1 must survive packaging.
# extent: Logical square dimension retained by rendering and previews.
# Raises ValueError for invalid dimensions or a completely transparent sprite.
def crop_transparent_bounds(indices, coverage, extent):
    if extent <= 0 or len(indices) != extent * extent or len(coverage) != len(indices):
        raise ValueError("Invalid personality sprite plane dimensions")
    bounds = Image.frombytes("L", (extent, extent), bytes(coverage)).getbbox()
    if bounds is None:
        raise ValueError("Personality sprite has no visible pixel")
    left, top, right, bottom = bounds
    # Store the original bytes in row order. No quantization, decoding, runtime
    # allocation or change to the sprite's logical coordinates is introduced.
    stored = tuple(b"".join(bytes(plane[y * extent + left:y * extent + right])
                            for y in range(top, bottom)) for plane in (indices, coverage))
    return bounds, stored[0], stored[1]


# No parameters: package preserved individual masters and palette into deterministic assets.
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
    source_path = ASSETS / "source/imagegen-masters-v2.json"
    # Text provenance remains stable across Windows and Git line-ending conversion.
    source_bytes = source_path.read_text(encoding="utf-8").encode("utf-8")
    authored = json.loads(source_bytes)
    if [item["id"] for item in authored["icons"]] != [item[0] for item in CATALOG]:
        raise ValueError("ImageGen masters must preserve the fourteen catalog identities and order")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "palette.rgb565.bin").write_bytes(b"".join(
        int(entry["rgb565"], 16).to_bytes(2, "little") for entry in entries))
    preview = Image.new("RGBA", (5 * 288, 3 * 288), (0, 0, 0, 0))
    source_lines = ['// Generated by scripts/device.ps1 generate -Generator cody-personality-icons.',
                    '#include "personality_icons.hpp"', 'namespace CODY::PersonalityAssets {',
                    'const uint16_t palette[256] = {',
                    "    " + ", ".join(entry["rgb565"] for entry in entries), "};"]
    descriptors = []
    manifest = {"schema": "cody.personality-icons.v2", "source": source_path.relative_to(ROOT).as_posix(),
                "source_sha256": digest(source_bytes),
                "palette_source": palette_path.relative_to(ROOT).as_posix(), "palette_sha256": digest(palette_bytes),
                "quantization": "nearest squared sRGB distance; lowest non-reserved slot breaks ties; no dithering",
                "coverage": "separate unpremultiplied alpha8; no reserved transparency color",
                "component_policy": "one native ImageGen master per identity; 4-connected alpha >= 8; minimum 100 pixels per 324x324 source area",
                "master_extent": EXTENT, "flash_encoding": "indexed-alpha8; exact nonzero-alpha bounds; logical extent unchanged",
                "icons": []}
    identities = [item[2] for item in CATALOG]
    if len(set(identities)) != 14 or len({item[0] for item in CATALOG}) != 14:
        raise ValueError("Symbol catalog identities must be unique")
    for offset, (name, cpp_id, identity) in enumerate(CATALOG):
        column, row = offset % 5, offset // 5
        authored_source = authored["icons"][offset]
        image_path = ROOT / authored_source["source"]
        image_bytes = image_path.read_bytes()
        if digest(image_bytes) != authored_source["sha256"]:
            raise ValueError(f"ImageGen master digest differs for {name}")
        with Image.open(image_path) as original:
            if original.mode != "RGBA" or original.width != original.height or original.width < EXTENT:
                raise ValueError(f"ImageGen master for {name} must be square RGBA at least {EXTENT}px")
            source_image = original.copy()
        minimum_pixels = max(1, round(100 * source_image.width * source_image.height / (324 * 324)))
        isolated, components = retain_components(source_image, minimum_pixels)
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
        preview.paste(master, (column * 288 + 16, row * 288 + 16))
        symbol = cpp_id.lower()
        stored_bounds, stored_indices, stored_coverage = crop_transparent_bounds(indices, coverage, EXTENT)
        left, top, right, bottom = stored_bounds
        stored_width, stored_height = right - left, bottom - top
        source_lines += [f"static const uint8_t {symbol}_indices[{len(stored_indices)}] = {{", cpp_bytes(stored_indices), "};",
                         f"static const uint8_t {symbol}_coverage[{len(stored_coverage)}] = {{", cpp_bytes(stored_coverage), "};"]
        descriptors.append(f"    {{PersonalityCue::{cpp_id}, {EXTENT}, {EXTENT}, {symbol}_indices, {symbol}_coverage, {left}, {top}, {stored_width}, {stored_height}}},")
        manifest["icons"].append({"id": name, "enum": cpp_id, "identity": identity,
                                   "source": authored_source["source"], "source_sha256": digest(image_bytes),
                                   "source_size": list(source_image.size), "components": components,
                                   "indices_sha256": digest(indices), "coverage_sha256": digest(coverage),
                                   "used_slots": sorted(set(indices)), "data_bytes": len(indices) + len(coverage),
                                   "stored_bounds": list(stored_bounds),
                                   "stored_indices_sha256": digest(stored_indices),
                                   "stored_coverage_sha256": digest(stored_coverage),
                                   "flash_data_bytes": len(stored_indices) + len(stored_coverage)})
    source_lines += ["const Sprite sprites[14] = {", *descriptors, "};", "} // namespace CODY::PersonalityAssets"]
    (OUTPUT / "personality_icons.cpp").write_text("\n".join(source_lines) + "\n", encoding="utf-8")
    (OUTPUT / "personality_icons.hpp").write_text(
        '// Generated by scripts/device.ps1 generate -Generator cody-personality-icons.\n'
        '#pragma once\n#include "CODY_personality.hpp"\nnamespace CODY::PersonalityAssets {\n'
        '// Immutable flash descriptor; stored geometry does not alter logical placement.\n'
        'struct Sprite {\n'
        '    PersonalityCue cue;\n    uint16_t width;\n    uint16_t height;\n'
        '    const uint8_t *indices;\n    const uint8_t *coverage;\n'
        '    uint16_t stored_left;\n    uint16_t stored_top;\n'
        '    uint16_t stored_width;\n    uint16_t stored_height;\n};\n'
        'extern const uint16_t palette[256];\nextern const Sprite sprites[14];\n}\n', encoding="utf-8")
    preview.save(OUTPUT / "preview.png")
    manifest["flash_payload_bytes"] = sum(icon["flash_data_bytes"] for icon in manifest["icons"]) + 512
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated 14 shared {EXTENT}x{EXTENT} indexed-alpha sprites; flash payload {manifest['flash_payload_bytes']} bytes")


if __name__ == "__main__":
    main()
