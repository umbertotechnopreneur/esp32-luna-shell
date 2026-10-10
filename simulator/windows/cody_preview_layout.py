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

"""Desktop viewport geometry shared by Cody artwork, point labels and mouse input."""

from __future__ import annotations

from dataclasses import dataclass
import math


SIZE = 466
LABEL_MARGIN = 230
LABEL_PAGE_SIZE = 36
VIEW_PADDING = 12
CARDINAL_ROTATIONS = (0, 90, 180, 270)


# x is the horizontal coordinate in the native logical display.
# y is the vertical coordinate in the native logical display.
# rotation is the clockwise cardinal orientation of the displayed image.
# Raises ValueError when the orientation is not supported.
def physical_point(x, y, rotation):
    if rotation == 0:
        return x, y
    if rotation == 90:
        return SIZE - 1 - y, x
    if rotation == 180:
        return SIZE - 1 - x, SIZE - 1 - y
    if rotation == 270:
        return y, SIZE - 1 - x
    raise ValueError(f"Unsupported rotation: {rotation}")


# x is the horizontal coordinate in the native rotated display.
# y is the vertical coordinate in the native rotated display.
# rotation is the same cardinal orientation used to present the image.
# Raises ValueError when the orientation is not supported.
def logical_touch(x, y, rotation):
    if rotation not in CARDINAL_ROTATIONS:
        raise ValueError(f"Unsupported rotation: {rotation}")
    return physical_point(x, y, (-rotation) % 360)


@dataclass(frozen=True)
class PreviewLayout:
    """One presented frame's placement; resizing never changes the 466-pixel rig."""

    width: int
    height: int
    image_size: int
    left: int
    top: int
    margin: int
    scale: float

    # width is the available desktop canvas width in pixels.
    # height is the available desktop canvas height in pixels.
    # show_labels reserves readable side columns when point annotations are enabled.
    # Raises ValueError for invalid canvas dimensions or a non-boolean label option.
    @staticmethod
    def fit(width, height, show_labels):
        if any(type(value) is not int or value < 1 for value in (width, height)):
            raise ValueError(f"Preview dimensions must be positive integers: {width}, {height}")
        if type(show_labels) is not bool:
            raise ValueError("Preview label visibility must be boolean")
        side_reserve = min(LABEL_MARGIN, width * .23) if show_labels else 0
        # The square is fitted once; all annotation and input coordinates reuse it.
        available_width = width - 2 * (VIEW_PADDING + side_reserve)
        available_height = height - 2 * VIEW_PADDING
        image_size = max(1, math.floor(min(available_width, available_height)))
        left = (width - image_size) // 2
        top = (height - image_size) // 2
        return PreviewLayout(width, height, image_size, left, top, left, image_size / SIZE)

    # self holds the immutable placement of the last displayed frame.
    # x is a native logical point's horizontal coordinate, including fractional positions.
    # y is a native logical point's vertical coordinate.
    # rotation is the cardinal orientation used for the presented frame.
    # Raises ValueError when the orientation is not supported.
    def to_canvas(self, x, y, rotation):
        physical_x, physical_y = physical_point(x, y, rotation)
        return self.left + physical_x * self.scale, self.top + physical_y * self.scale

    # self holds the same placement used to draw the image and its point labels.
    # x is the mouse position along the desktop canvas's horizontal axis.
    # y is the mouse position along the desktop canvas's vertical axis.
    # rotation is the orientation of the last presented frame, not a pending UI target.
    # Raises ValueError when the orientation is not supported.
    def to_logical(self, x, y, rotation):
        if rotation not in CARDINAL_ROTATIONS:
            raise ValueError(f"Unsupported rotation: {rotation}")
        if not (self.left <= x < self.left + self.image_size and
                self.top <= y < self.top + self.image_size):
            return None
        return logical_touch((x - self.left) / self.scale, (y - self.top) / self.scale, rotation)

    # self supplies the available height of the current preview viewport.
    # row_height is the measured desktop font line height plus label spacing.
    # Raises ValueError for non-finite, zero or negative row heights.
    def label_capacity(self, row_height):
        if type(row_height) not in (int, float) or not math.isfinite(row_height) or row_height <= 0:
            raise ValueError(f"Invalid label row height: {row_height}")
        rows_per_side = max(1, math.floor((self.height - 2 * VIEW_PADDING) / row_height))
        return min(LABEL_PAGE_SIZE, rows_per_side * 2)
