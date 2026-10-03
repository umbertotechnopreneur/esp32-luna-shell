"""Bake the current cloud/title scene and two-bit monochrome glyphs into flash data."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_scene_contract import SCENE_IDENTITIES, load_scenes, scene_source_hash
from scripts.common.cody_rig_types import FaceChannel

OUTPUT = ROOT / "assets/cody/scenes/generated"


# value: Authored finite number emitted without losing a C++ float suffix.
def cpp_float(value):
    return f"{float(value):.9f}f"


# font_path: Explicit existing JetBrains Mono font; no installed-font search or substitute.
# document: Validated current authoring schema.
# Raises OSError for a missing font and ValueError for non-monospace or clipped glyphs.
def bake_font(font_path: Path, document):
    font = ImageFont.truetype(str(font_path), document["font"]["pixel_size"])
    if "JetBrains" not in font.getname()[0]:
        raise ValueError("This asset's recorded license requires an explicit JetBrains Mono source")
    first = document["font"]["first_character"]
    last = document["font"]["last_character"]
    advances = {font.getlength(chr(code)) for code in range(first, last + 1)}
    if len(advances) != 1:
        raise ValueError("The source font is not monospace for printable ASCII")
    advance = round(advances.pop())
    if not 1 <= advance <= 32:
        raise ValueError(f"Unsupported monospace advance: {advance}")
    ascent, descent = font.getmetrics()
    height = ascent + descent
    # A monospace advance does not imply zero side-bearing. Keep real overhangs
    # instead of cropping '&' and neighbouring glyphs to a guessed cell width.
    boxes = [font.getbbox(chr(code), anchor="ls") for code in range(first, last + 1)]
    left_bearing = min(box[0] for box in boxes)
    width = max(box[2] for box in boxes) - left_bearing
    if not 1 <= width <= 32 or not 1 <= height <= 64:
        raise ValueError(f"Unsupported glyph cell: {width}x{height}")
    row_bytes = (width + 3) // 4
    coverage = bytearray()
    for code in range(first, last + 1):
        character = chr(code)
        left, top, right, bottom = font.getbbox(character, anchor="ls")
        if left < left_bearing or right - left_bearing > width or top + ascent < 0 or bottom + ascent > height:
            raise ValueError(f"Glyph {character!r} leaves its monospace cell")
        image = Image.new("L", (width, height))
        ImageDraw.Draw(image).text((-left_bearing, ascent), character, font=font, fill=255, anchor="ls")
        samples = image.load()
        for row in range(height):
            packed = bytearray(row_bytes)
            for column in range(width):
                level = (samples[column, row] * 3 + 127) // 255
                packed[column // 4] |= level << (6 - (column % 4) * 2)
            coverage.extend(packed)
    return bytes(coverage), width, height, advance, row_bytes


# argv: Literal generator arguments forwarded by scripts/device.ps1.
# Raises OSError for input/output failures and ValueError for invalid authoring or fonts.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-path", required=True, type=Path)
    args = parser.parse_args(argv)
    document = load_scenes(ROOT)
    coverage, width, height, advance, row_bytes = bake_font(args.font_path, document)
    count = document["font"]["last_character"] - document["font"]["first_character"] + 1
    header = [
        "// Generated via scripts/device.ps1 generate -Generator cody-scenes.",
        "#pragma once", '#include "CODY_scene.hpp"',
        "namespace CODY::SceneData {",
        f"inline constexpr uint32_t boot_delay_ms = {document['boot_delay_ms']}U;",
        "extern const CODY2D::MonoFont title_font;",
        "inline constexpr SceneDefinition definitions[] = {",
    ]
    for scene in document["scenes"]:
        cloud = scene["cloud"]
        intro = scene["intro"]
        title = scene["title"]
        text_width = (len(title["text"]) - 1) * advance + width
        origin_x = (466 - text_width) // 2
        if text_width > 320 or title["top"] + height > 466:
            raise ValueError(f"Scene {scene['id']} has an oversized title")
        for x in (origin_x, origin_x + text_width - 1):
            for y in (title["top"], title["top"] + height - 1):
                if (x - 233) ** 2 + (y - 233) ** 2 > 233 ** 2:
                    raise ValueError(f"Scene {scene['id']} title leaves the circular display")
        channels = ", ".join(cpp_float(scene["face"][channel.name.lower()]) for channel in FaceChannel)
        transform = ", ".join(cpp_float(cloud[key]) for key in ("x", "y", "scale"))
        name = SCENE_IDENTITIES[scene["id"]][1]
        flags = ", ".join(str(scene[key]).lower() for key in ("navigation_visible", "readout_visible"))
        header.extend([
            "    {", f'        SceneId::{name}, "{scene["id"]}", {{{transform}}},',
            f"        {{{intro['bitmap_fade_out_ms']}, {intro['cloud_fade_in_ms']}}},",
            f"        {{{{{channels}}}}}, &title_font,",
            f'        {{{json.dumps(title["text"])}, {len(title["text"])}}},',
            f"        {title['top']}, {title['color_rgb565']}, {title['character_interval_ms']}, {flags}",
            "    },",
        ])
    header.extend(["};", "} // namespace CODY::SceneData"])
    source = [
        "// Cody Title Mono coverage; derived from JetBrains Mono, SIL OFL 1.1.",
        "// Copyright 2020 The JetBrains Mono Project Authors. See ../OFL.txt.",
        '#include "scene_data.hpp"', "namespace CODY::SceneData {", "namespace {",
        f"alignas(4) const uint8_t coverage[{len(coverage)}] = {{",
    ]
    for offset in range(0, len(coverage), 24):
        source.append("    " + ", ".join(f"0x{value:02x}" for value in coverage[offset:offset + 24]) + ",")
    source.extend(["};", "} // namespace",
        f"const CODY2D::MonoFont title_font{{coverage, sizeof(coverage), 32, {count}, {width}, {height}, {advance}, {row_bytes}}};",
        "} // namespace CODY::SceneData"])
    manifest = {
        "schema": "cody.scene-font.v1", "font_name": "Cody Title Mono",
        "source_family": "JetBrains Mono", "license": "SIL-OFL-1.1",
        "font_source_sha256": hashlib.sha256(args.font_path.read_bytes()).hexdigest(),
        "scenes_sha256": scene_source_hash(ROOT),
        "first_character": 32, "character_count": count, "width": width, "height": height,
        "advance": advance, "row_bytes": row_bytes, "coverage_bytes": len(coverage),
        "coverage_sha256": hashlib.sha256(coverage).hexdigest(),
        "note": "Source asset generation only; no firmware or simulator validation.",
    }
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "scene_data.hpp").write_text("\n".join(header) + "\n", encoding="utf-8")
    (OUTPUT / "scene_font.cpp").write_text("\n".join(source) + "\n", encoding="utf-8")
    (OUTPUT / "scene_font.2bpp.bin").write_bytes(coverage)
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Scene asset packaged: {len(document['scenes'])} scene(s), {width}x{height} glyphs, {len(coverage)} flash bytes")


if __name__ == "__main__":
    main()
