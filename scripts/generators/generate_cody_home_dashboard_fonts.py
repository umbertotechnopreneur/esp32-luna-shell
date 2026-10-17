# VBWR B
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
# VibeWare: Human intent, AI execution, and plenty of tokens
# Modified with AI: OpenAI Codex; added isolated Home typography on 2026-10-14.
# Copyright (c) 2026 Umberto Giacobbi
# VBWR E

"""Bake the vendored OFL sans face into independent fixed-cell Home fonts."""

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "third_party/esp-brookesia/system/brookesia_system_super/resource/fonts/Telex-Regular.ttf"
OUTPUT = ROOT / "assets/cody/home-dashboard-fonts/generated"


# pixels: Explicit source raster size, independent of existing scene typography.
# first: First supported ASCII code, retained as a contiguous MonoFont range.
# last: Last supported ASCII code, including all numeric punctuation and K.
# Raises OSError or ValueError for a missing source, unexpected family or unsupported glyph bounds.
def bake(pixels, first, last):
    font = ImageFont.truetype(str(SOURCE), pixels)
    if font.getname()[0] != "Telex":
        raise ValueError("Home font source is not the licensed vendored Telex family")
    boxes = [font.getbbox(chr(code), anchor="ls") for code in range(first, last + 1)]
    left = min(0, min(box[0] for box in boxes))
    width = max(box[2] for box in boxes) - left
    ascent, descent = font.getmetrics()
    height = ascent + descent
    advance = max(width, int(max(font.getlength(chr(code)) for code in range(first, last + 1)) + 0.999))
    if not 1 <= width <= 32 or not 1 <= advance <= 32 or not 1 <= height <= 64:
        raise ValueError("Home font glyph extent exceeds native MonoFont bounds")
    row_bytes = (width + 1) // 2
    coverage = bytearray()
    for code, box in zip(range(first, last + 1), boxes):
        if box[1] + ascent < 0 or box[3] + ascent > height:
            raise ValueError("Home glyph leaves its declared cell")
        image = Image.new("L", (width, height))
        ImageDraw.Draw(image).text((-left, ascent), chr(code), font=font, anchor="ls", fill=255)
        samples = image.load()
        for y in range(height):
            for x in range(0, width, 2):
                high = (samples[x, y] * 15 + 127) // 255
                low = (samples[x + 1, y] * 15 + 127) // 255 if x + 1 < width else 0
                coverage.append((high << 4) | low)
    return bytes(coverage), dict(pixel_size=pixels, first_character=first,
        character_count=last - first + 1, width=width, height=height,
        advance=advance, row_bytes=row_bytes, coverage_bits=4)


# No parameters: the single public generator selects only the committed licensed font source.
# Raises OSError or ValueError before publishing assets when source/metrics are rejected.
def main():
    fonts = [("small_font", *bake(14, 32, 126)), ("heading_font", *bake(20, 32, 126)),
             ("value_font", *bake(32, 46, 75))]
    heading = fonts[1][2]
    for caption in ("Ready to team up", "Let's get to work", "Waiting for you", "Compacting"):
        if (len(caption) - 1) * heading['advance'] + heading['width'] > 400:
            raise ValueError("Home heading cannot fit the circular title chord")
    header = """// Cody Dashboard Sans; derived from Telex, SIL OFL 1.1. See ../OFL.txt.
#pragma once
#include "CODY2D_text.hpp"
namespace CODY::HomeFonts {
extern const CODY2D::MonoFont small_font;
extern const CODY2D::MonoFont heading_font;
extern const CODY2D::MonoFont value_font;
} // namespace CODY::HomeFonts
"""
    lines = ['// Cody Dashboard Sans; derived from Telex, SIL OFL 1.1. See ../OFL.txt.',
             '// Copyright 2011 The Telex Project Authors (info@andrestorresi.com.ar).',
             '#include "home_dashboard_fonts.hpp"', 'namespace CODY::HomeFonts {']
    manifest = dict(schema="cody.home-fonts.v1", derived_name="Cody Dashboard Sans",
        source_family="Telex", license="SIL-OFL-1.1",
        source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(), fonts={})
    for name, coverage, metrics in fonts:
        lines.append(f'alignas(4) static const uint8_t {name}_coverage[] = {{')
        for offset in range(0, len(coverage), 24):
            lines.append('    ' + ', '.join(f'0x{value:02x}' for value in coverage[offset:offset + 24]) + ',')
        lines.append('};')
        values = ', '.join(str(metrics[key]) for key in (
            'first_character', 'character_count', 'width', 'height', 'advance', 'row_bytes', 'coverage_bits'))
        lines.append(f'const CODY2D::MonoFont {name}{{{name}_coverage, sizeof({name}_coverage), {values}}};')
        manifest['fonts'][name] = {**metrics, 'coverage_bytes': len(coverage),
            'coverage_sha256': hashlib.sha256(coverage).hexdigest()}
    lines.append('} // namespace CODY::HomeFonts')
    # All glyph work succeeds before replacing the dedicated derived package.
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'home_dashboard_fonts.hpp').write_text(header, encoding='utf-8')
    (OUTPUT / 'home_dashboard_fonts.cpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Generated Cody Dashboard Sans: ' + ', '.join(
        f'{name}={len(coverage)} bytes' for name, coverage, _ in fonts))


if __name__ == '__main__':
    main()
