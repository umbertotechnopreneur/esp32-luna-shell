"""Reusable cloud/title presentation using the same authored scenes and flash glyphs."""

import hashlib
import math

from scripts.common.cody_scene_contract import load_scenes, require_keys, scene_source_hash
from scripts.common.cody_rig_contract import read_json
from scripts.common.cody_rig_types import FaceChannel


class ScenePainter:
    # self: Host-only owner of immutable authoring and glyph coverage.
    # root: Current checkout; neither font discovery nor hardware access is allowed.
    # rig: Already-loaded current rig providing the canonical facial channel limits.
    # Raises OSError for missing assets and ValueError for stale or invalid generated data.
    def __init__(self, root, rig):
        self.document = load_scenes(root, rig)
        self.scenes = {scene["id"]: scene for scene in self.document["scenes"]}
        generated = root / "assets/cody/scenes/generated"
        self.font = read_json(generated / "manifest.json")
        self.coverage = (generated / "scene_font.2bpp.bin").read_bytes()
        require_keys(self.font, {"schema", "font_name", "source_family", "license", "font_source_sha256",
            "scenes_sha256", "first_character", "character_count", "width", "height", "advance",
            "row_bytes", "coverage_bytes", "coverage_sha256", "note"}, "generated scene font")
        if self.font["schema"] != "cody.scene-font.v1":
            raise ValueError("Unsupported generated scene-font schema")
        font = self.font
        numeric = ("first_character", "character_count", "width", "height", "advance", "row_bytes", "coverage_bytes")
        if (any(type(font[key]) is not int for key in numeric) or
                font["first_character"] != 32 or font["character_count"] != 95 or
                not 1 <= font["width"] <= 32 or not 1 <= font["height"] <= 64 or
                not 1 <= font["advance"] <= 32 or font["row_bytes"] != (font["width"] + 3) // 4 or
                font["coverage_bytes"] != font["character_count"] * font["height"] * font["row_bytes"]):
            raise ValueError("Invalid printable-ASCII packed font extent")
        if scene_source_hash(root) != self.font["scenes_sha256"]:
            raise ValueError("Scene assets are stale; regenerate with device.ps1 generate -Generator cody-scenes")
        if (len(self.coverage) != self.font["coverage_bytes"] or
                hashlib.sha256(self.coverage).hexdigest() != self.font["coverage_sha256"]):
            raise ValueError("Scene glyph coverage is corrupt or incomplete")
        for scene in self.scenes.values():
            title = scene["title"]
            width = (len(title["text"]) - 1) * font["advance"] + font["width"]
            left = (466 - width) // 2
            if width > 320 or title["top"] + font["height"] > 466:
                raise ValueError(f"Oversized scene title: {scene['id']}")
            for x in (left, left + width - 1):
                for y in (title["top"], title["top"] + font["height"] - 1):
                    if (x - 233) ** 2 + (y - 233) ** 2 > 233 ** 2:
                        raise ValueError(f"Scene title leaves the circular display: {scene['id']}")

    # self: Current immutable registry.
    # identity: Stable scene name; there is no fallback to a table position.
    # Raises ValueError for an unknown scene identity.
    def definition(self, identity):
        if identity not in self.scenes:
            raise ValueError(f"Unknown scene identity: {identity!r}")
        return self.scenes[identity]

    # self: Current immutable registry.
    # identity: Stable scene whose authored face is requested.
    # Raises ValueError for an unknown scene identity.
    def face(self, identity):
        face = self.definition(identity)["face"]
        return [face[channel.name.lower()] for channel in FaceChannel]

    # self: Current immutable registry.
    # identity: Scene selecting its own typewriter interval.
    # active_ms: Active scene elapsed time; pause/standby time is excluded by the caller.
    # Raises ValueError for invalid time or scene identity.
    def visible_characters(self, identity, active_ms):
        if type(active_ms) not in (int, float) or not math.isfinite(active_ms) or active_ms < 0:
            raise ValueError(f"Invalid scene clock: {active_ms!r}")
        title = self.definition(identity)["title"]
        interval = title["character_interval_ms"]
        return len(title["text"]) if interval == 0 else min(len(title["text"]), int(active_ms / interval))

    # self: Host-only immutable glyph and layout owner.
    # image: RGB framebuffer already containing the same logical cloud scene.
    # identity: Scene choosing the counted title and centered layout.
    # active_ms: Active typewriter time, never a sensor measurement.
    # Raises ValueError for a rejected clock, identity, or generated font extent.
    def paint_title(self, image, identity, active_ms):
        if image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Scene text requires a 466x466 RGB framebuffer")
        title = self.definition(identity)["title"]
        visible = self.visible_characters(identity, active_ms)
        font = self.font
        width = (len(title["text"]) - 1) * font["advance"] + font["width"]
        origin_x = (466 - width) // 2
        pixels = image.load()
        source = title["color_rgb565"]
        for index, character in enumerate(title["text"][:visible]):
            glyph = (ord(character) - font["first_character"]) * font["height"] * font["row_bytes"]
            for row in range(font["height"]):
                offset = glyph + row * font["row_bytes"]
                for column in range(font["width"]):
                    level = (self.coverage[offset + column // 4] >> (6 - column % 4 * 2)) & 3
                    if level == 0:
                        continue
                    x = origin_x + index * font["advance"] + column
                    y = title["top"] + row
                    red, green, blue = pixels[x, y]
                    background = (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
                    word = 0
                    for shift, mask in ((11, 31), (5, 63), (0, 31)):
                        foreground = source >> shift & mask
                        beneath = background >> shift & mask
                        word |= ((foreground * level + beneath * (3 - level) + 1) // 3) << shift
                    pixels[x, y] = ((word >> 11 & 31) * 255 // 31,
                                    (word >> 5 & 63) * 255 // 63, (word & 31) * 255 // 31)
