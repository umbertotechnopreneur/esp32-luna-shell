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

"""Pure host raster regression checks; no Tk window, device or serial access."""

from __future__ import annotations

import json
import math
from pathlib import Path
import sys

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "simulator/windows"))

from cody_coverage import FULL_COVERAGE, coverage_mask  # noqa: E402
from cody_simulator import CodyRenderer, rgb565  # noqa: E402


# Raises OSError or ValueError for unreadable canonical palette data.
def make_raster_context():
    entries = json.loads((ROOT / "assets/cody/palette/palette256.json").read_text(encoding="utf-8"))["entries"]
    # Isolate the actual compositor; this check deliberately creates no Tk root
    # and does not decode boot artwork or initialize synthetic services.
    renderer = CodyRenderer.__new__(CodyRenderer)
    renderer.native_palette = [int(entry["rgb565"], 16) for entry in entries]
    renderer.rgb_palette = [rgb565(word) for word in renderer.native_palette]
    renderer.flat_palette = [channel for color in renderer.rgb_palette for channel in color]
    return renderer


# vertices: Small fixture polygon whose complete edge list is the reference.
# scan_y: Exact subpixel sample ordinate shared by the native four-sample policy.
def reference_intersections(vertices, scan_y):
    hits = []
    for index, start in enumerate(vertices):
        end = vertices[(index + 1) % len(vertices)]
        if (start[1] <= scan_y < end[1]) or (end[1] <= scan_y < start[1]):
            hits.append(start[0] + (scan_y - start[1]) * (end[0] - start[0]) / (end[1] - start[1]))
    return sorted(hits)


# vertices: Subject polygon fixture, including fractional and off-canvas edges.
# size: Small reference canvas used to bound the check's CPU and memory cost.
# containing: Optional clip polygon intersected geometrically at each sample.
def reference_mask(vertices, size, containing):
    mask = Image.new("I", size, 0)
    pixels = mask.load()
    for row in range(size[1]):
        for sample in range(4):
            scan_y = row + (sample + .5) / 4
            hits = reference_intersections(vertices, scan_y)
            subject_spans = list(zip(hits[::2], hits[1::2]))
            if containing is None:
                spans = subject_spans
            else:
                clip_hits = reference_intersections(containing, scan_y)
                spans = [
                    (max(left, clip_left), min(right, clip_right))
                    for left, right in subject_spans
                    for clip_left, clip_right in zip(clip_hits[::2], clip_hits[1::2])
                ]
            for left, right in spans:
                left = max(0, left)
                right = min(size[0], right)
                if left >= right:
                    continue
                first = math.floor(left * 256 + .5)
                last = math.floor(right * 256 + .5)
                # This oracle adds overlap to individual pixels. It uses neither
                # row buckets, difference arrays nor the optimized run extractor.
                for column in range(first // 256, (last + 255) // 256):
                    overlap = max(0, min(last, (column + 1) * 256) - max(first, column * 256))
                    pixels[column, row] += overlap
    return mask


# renderer: Actual compositor with the canonical RGB565 palette loaded.
# Raises AssertionError if analytic coverage, sparse runs or final RGB pixels differ.
def check_raster(renderer):
    size = (32, 32)
    source = Image.frombytes("P", size, bytes((x * 7 + y * 11) % 256 for y in range(32) for x in range(32)))
    source.putpalette(renderer.flat_palette)
    background = Image.new("RGB", size)
    background.putdata([rgb565((x * 2113 + y * 1057) & 65535) for y in range(32) for x in range(32)])
    rectangle = [(.25, .25), (15.75, .25), (15.75, 17.75), (.25, 17.75)]
    cases = (
        ("fractional_rectangle", rectangle, None),
        ("concave_off_canvas", [(-3, -1), (20, .125), (20, 20), (10, 20), (10, 8), (-3, 8)], None),
        ("sloped_q8_edge", [(1 / 512, 0), (19.7, 4.9), (.2, 20.8)], None),
        ("thin_cap", [(0, .1), (2, .1), (2, .4), (0, .4)], None),
        ("near_full_1022", [(1 / 256, 0), (2, 0), (2, 1), (0, 1)], None),
        ("near_full_1023", [(1 / 384, 0), (2, 0), (2, 1), (0, 1)], None),
        ("empty_horizontal", [(3, 5), (7, 5), (9, 5)], None),
        ("outside_canvas", [(-100, -30), (-90, -30), (-90, -20), (-100, -20)], None),
        ("clipped_rectangle", rectangle, [(3.125, -3), (20, 4.5), (8.75, 21), (-2, 9)]),
    )
    comparisons = 0
    for name, vertices, containing in cases:
        runs = []
        mask = coverage_mask(vertices, size, containing, runs)
        assert mask.tobytes() == reference_mask(vertices, size, containing).tobytes(), f"{name}: reference coverage differs"
        reversed_mask = coverage_mask(list(reversed(vertices)), size, containing)
        assert mask.tobytes() == reversed_mask.tobytes(), f"{name}: winding changed coverage"
        reconstructed = Image.new("I", size, 0)
        draw = ImageDraw.Draw(reconstructed)
        for row, left, right, coverage in runs:
            draw.line((left, row, right - 1, row), fill=coverage)
        assert reconstructed.tobytes() == mask.tobytes(), f"{name}: sparse runs differ from the mask"
        if name == "fractional_rectangle":
            assert mask.getpixel((0, 0)) == 576, "Analytic fractional corner coverage changed"
            assert mask.getpixel((1, 0)) == 768, "Analytic fractional edge coverage changed"
            assert mask.getpixel((1, 1)) == FULL_COVERAGE, "Opaque interior coverage changed"
        if name == "thin_cap":
            assert mask.getpixel((0, 0)) == 512, "Thin horizontal sample coverage changed"
        if name == "near_full_1022":
            assert mask.getpixel((0, 0)) == 1022, "Near-full edge coverage changed"
        if name == "near_full_1023":
            assert mask.getpixel((0, 0)) == 1023, "Rounded opaque edge coverage changed"
        for opacity in (1, 127, 254, 255):
            reference = background.copy()
            optimized = background.copy()
            # The established per-pixel RGB565 implementation is the reference.
            # Vary both foreground and background to catch bad run-wide color assumptions.
            renderer.composite_fill(reference, source, mask, opacity)
            renderer.composite_fill(optimized, source, mask, opacity, runs)
            assert optimized.tobytes() == reference.tobytes(), f"{name}, alpha={opacity}: RGB565 pixels differ"
            comparisons += 1
    return comparisons


# Raises the original palette, coverage or pixel assertion error after recording failure.
def main():
    output = ROOT / "artifacts/checks/cody-preview-raster"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "pure host RGB565 raster; no GUI, benchmark or hardware"}
    try:
        report["pixel_comparisons"] = check_raster(make_raster_context())
        report["status"] = "passed"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Cody preview raster checks passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
