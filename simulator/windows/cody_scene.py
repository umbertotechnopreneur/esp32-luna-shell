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

"""Reusable cloud/title presentation using the same authored scenes and flash glyphs."""

import math

from scripts.common.cody_scene_contract import load_generated_scene_fonts, load_scenes, scene_source_hash
from scripts.common.cody_rig_types import FaceChannel


class ScenePainter:
    # self: Host-only owner of immutable authoring and glyph coverage.
    # root: Current checkout; neither font discovery nor hardware access is allowed.
    # rig: Already-loaded current rig providing the canonical facial channel limits.
    # Raises OSError for missing assets and ValueError for stale or invalid generated data.
    def __init__(self, root, rig):
        self.native = None
        self.document = load_scenes(root, rig)
        self.scenes = {scene["id"]: scene for scene in self.document["scenes"]}
        generated = root / "assets/cody/scenes/generated"
        self.font, self.coverage, self.body_coverage = load_generated_scene_fonts(generated)
        font = self.font
        explicit_format = font["schema"] == "cody.scene-font.v2"
        self.coverage_bits = font["coverage_bits"] if explicit_format else 2
        self.body_coverage_bits = font["body_font"]["coverage_bits"] if explicit_format else 2
        if scene_source_hash(root) != self.font["scenes_sha256"]:
            raise ValueError("Scene assets are stale; regenerate with device.ps1 generate -Generator cody-scenes")
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
        if self.native is not None:
            return int(self.native.scene_timing(identity, active_ms)[6])
        if type(active_ms) not in (int, float) or not math.isfinite(active_ms) or active_ms < 0:
            raise ValueError(f"Invalid scene clock: {active_ms!r}")
        scene = self.definition(identity)
        entrance = scene["entrance"]
        text_start_ms = sum(scene["intro"].values()) + entrance["move_ms"] + entrance["text_delay_ms"]
        if active_ms < text_start_ms:
            return 0
        title = scene["title"]
        interval = title["character_interval_ms"]
        return len(title["text"]) if interval == 0 else min(len(title["text"]), int((active_ms - text_start_ms) / interval))

    # self: Immutable scene registry shared with the firmware authoring contract.
    # identity: Stable scene selecting its from/to placement and duration.
    # active_ms: Active presentation time, excluding standby and pause.
    # Raises ValueError for invalid time or an unknown scene identity.
    def cloud_transform(self, identity, active_ms):
        if self.native is not None:
            return self.native.scene_timing(identity, active_ms)[:3]
        if type(active_ms) not in (int, float) or not math.isfinite(active_ms) or active_ms < 0:
            raise ValueError(f"Invalid scene entrance clock: {active_ms!r}")
        scene = self.definition(identity)
        entrance = scene["entrance"]
        elapsed_ms = max(0, active_ms - sum(scene["intro"].values()))
        duration_ms = entrance["move_ms"]
        if duration_ms == 0 or elapsed_ms >= duration_ms:
            return tuple(scene["cloud"][key] for key in ("x", "y", "scale"))
        progress = elapsed_ms / duration_ms
        eased = progress * progress * (3 - 2 * progress)
        return tuple(entrance["from"][key] + (scene["cloud"][key] - entrance["from"][key])*eased
                     for key in ("x", "y", "scale"))

    # self: Registry with the same sequential title/body character clock as firmware.
    # identity: Stable scene identity.
    # active_ms: Active presentation clock in milliseconds.
    # length: Counted printable-ASCII characters in this line.
    # preceding_characters: Title and preceding caption characters.
    # Raises ValueError for invalid counts, identity or time.
    def content_characters(self, identity, active_ms, length, preceding_characters):
        if self.native is not None:
            return int(self.native.scene_timing(identity, active_ms, length, preceding_characters)[7])
        if any(type(value) is not int or not 0 <= value <= 65535 for value in (length, preceding_characters)):
            raise ValueError("Invalid counted scene content")
        if not self.presentation(identity, active_ms)[2]:
            return 0
        scene = self.definition(identity)
        interval = scene["title"]["character_interval_ms"]
        if interval == 0:
            return length
        entrance = scene["entrance"]
        start_ms = sum(scene["intro"].values()) + entrance["move_ms"] + entrance["text_delay_ms"]
        return min(length, max(0, int((active_ms-start_ms)/interval)-preceding_characters))

    # self: Shared immutable scene authoring.
    # identity: Scene selecting the two sequential intro durations.
    # active_ms: Nonnegative active intro clock; no sensor sample is generated.
    # Returns the selected boot layer, global opacity and content admission.
    # Raises ValueError for invalid time or identity.
    def presentation(self, identity, active_ms):
        if self.native is not None:
            values = self.native.scene_timing(identity, active_ms)
            return bool(values[3]), int(values[4]), bool(values[5])
        if type(active_ms) not in (int, float) or not math.isfinite(active_ms) or active_ms < 0:
            raise ValueError(f"Invalid scene intro clock: {active_ms!r}")
        intro = self.definition(identity)["intro"]
        fade_out_ms = intro["bitmap_fade_out_ms"]
        if active_ms < fade_out_ms:
            return True, 255 - int(active_ms * 255 / fade_out_ms + .5), False
        fade_in_ms = intro["cloud_fade_in_ms"]
        elapsed_ms = active_ms - fade_out_ms
        if elapsed_ms < fade_in_ms:
            return False, int(elapsed_ms * 255 / fade_in_ms + .5), False
        entrance = self.definition(identity)["entrance"]
        return False, 255, elapsed_ms >= fade_in_ms + entrance["move_ms"] + entrance["text_delay_ms"]

    # self: Stateless host mirror of the native tile fade.
    # image: Whole synthetic RGB framebuffer, never a firmware allocation.
    # opacity: Valid global opacity in 0..255, quantized in native RGB565 channels.
    # Raises ValueError for unsupported input; padding/rotation happen after this layer.
    def fade_to_black(self, image, opacity):
        if self.native is not None:
            return self.native.fade_to_black(image, opacity)
        if image.mode != "RGB" or image.size != (466, 466) or type(opacity) is not int or not 0 <= opacity <= 255:
            raise ValueError("Scene fade requires a 466x466 RGB image and 0..255 opacity")
        if opacity == 255:
            return
        red = [(((value >> 3) * opacity + 127) // 255) * 255 // 31 for value in range(256)]
        green = [(((value >> 2) * opacity + 127) // 255) * 255 // 63 for value in range(256)]
        image.paste(image.point(red + green + red))

    # self: Host-only immutable glyph and layout owner.
    # image: RGB framebuffer already containing the same logical cloud scene.
    # identity: Scene choosing the counted title and centered layout.
    # active_ms: Active typewriter time, never a sensor measurement.
    # Raises ValueError for a rejected clock, identity, or generated font extent.
    def paint_title(self, image, identity, active_ms):
        if image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Scene text requires a 466x466 RGB framebuffer")
        title = self.definition(identity)["title"]
        if not title["visible"]:
            return
        visible = self.visible_characters(identity, active_ms)
        self.paint_text(image, title["text"], title["top"], title["color_rgb565"], visible=visible)

    # self: Owner of the same immutable title/caption glyphs used by the native renderer.
    # image: Complete RGB logical framebuffer.
    # text: Bounded printable-ASCII content; its complete width is reserved during reveal.
    # top: Logical top row of this centered line.
    # color: Native RGB565 foreground.
    # body: Select the authored caption font instead of the title font.
    # visible: Optional visible prefix; omitted means the complete line.
    # center_x: Shared logical horizontal center for a bounded event label or action caption.
    # max_width: Optional explicit slot width; default preserves the authored title/caption slot.
    # Raises ValueError for unsupported pixels, text, extent or glyph counts.
    def paint_text(self, image, text, top, color=65535, *, body=False, visible=None,
                   center_x=233, max_width=None):
        if self.native is not None:
            return self.native.paint_text(image, text, top, color, body=body, visible=visible,
                center_x=center_x, max_width=max_width)
        if image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Scene text requires a 466x466 RGB framebuffer")
        if not isinstance(text, str) or any(not 32 <= ord(character) <= 126 for character in text):
            raise ValueError("Scene text must be printable ASCII")
        if not text:
            return
        if type(top) is not int or type(color) is not int or not 0 <= color <= 65535:
            raise ValueError("Scene text position/color is invalid")
        font = {**self.font, **self.font["body_font"]} if body else self.font
        coverage = self.body_coverage if body else self.coverage
        coverage_bits = self.body_coverage_bits if body else self.coverage_bits
        samples_per_byte = 8 // coverage_bits
        maximum_coverage = (1 << coverage_bits) - 1
        width = (len(text) - 1) * font["advance"] + font["width"]
        if max_width is None:
            max_width = 232 if body else 320
        if type(center_x) is not int or type(max_width) is not int or not 1 <= max_width <= 320:
            raise ValueError("Scene text slot geometry is invalid")
        origin_x = center_x - (width + 1) // 2
        if width > max_width or any(
                (x - 233) ** 2 + (y - 233) ** 2 > 233 ** 2
                for x in (origin_x, origin_x + width - 1)
                for y in (top, top + font["height"] - 1)):
            raise ValueError("Scene text exceeds its slot or circular chord")
        if visible is None:
            visible = len(text)
        if type(visible) is not int or not 0 <= visible <= len(text):
            raise ValueError("Scene text visible prefix is invalid")
        pixels = image.load()
        source = color
        for index, character in enumerate(text[:visible]):
            glyph = (ord(character) - font["first_character"]) * font["height"] * font["row_bytes"]
            for row in range(font["height"]):
                offset = glyph + row * font["row_bytes"]
                for column in range(font["width"]):
                    shift = 8 - coverage_bits - column % samples_per_byte * coverage_bits
                    level = (coverage[offset + column // samples_per_byte] >> shift) & maximum_coverage
                    if level == 0:
                        continue
                    x = origin_x + index * font["advance"] + column
                    y = top + row
                    red, green, blue = pixels[x, y]
                    background = (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3
                    word = 0
                    for shift, mask in ((11, 31), (5, 63), (0, 31)):
                        foreground = source >> shift & mask
                        beneath = background >> shift & mask
                        word |= ((foreground * level + beneath * (maximum_coverage - level) +
                                  maximum_coverage // 2) // maximum_coverage) << shift
                    pixels[x, y] = ((word >> 11 & 31) * 255 // 31,
                                    (word >> 5 & 63) * 255 // 63, (word & 31) * 255 // 31)
