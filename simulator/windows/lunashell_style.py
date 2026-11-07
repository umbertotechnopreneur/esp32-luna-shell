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

"""Palette and logical geometry shared by Luna Shell simulator pages."""

from __future__ import annotations

import math


DISPLAY_SIZE = 466
LEG_FADE_IN_SECONDS = 0.25
LEG_FADE_OUT_SECONDS = 1.5
LEG_PULSE_SECONDS = LEG_FADE_IN_SECONDS + LEG_FADE_OUT_SECONDS
SAFE_ORIGIN = 73
SAFE_SIZE = 320
CIRCULAR_CONTENT_INSET = 55
BLUE = "#247fd6"
NAVY = "#071a2c"
MUTED = "#8aa7b6"
MUTED_BRIGHT = "#9fb1c1"
BACKGROUND = "#010609"
SURFACE = "#111922"
TEXT = "#f3f7fa"
TRACK = "#263744"
STATUS_EDGE = "#245363"
STATUS_GLOW = "#063f52"
STATUS_READY = "#20dfc1"
STATUS_WARNING = "#f0b44d"
STATUS_MISSING = "#eb4d5c"
STATUS_FAULT = "#ff3859"


PRIMARY_TOP = "#247fd6"
PRIMARY_BOTTOM = "#0a346b"
PRIMARY_BORDER = "#65c8ff"
PRIMARY_GLOW = "#0b55d9"
SUCCESS_TOP = "#0ab49d"
SUCCESS_BOTTOM = "#045a51"
SUCCESS_BORDER = "#38f1d7"
SUCCESS_GLOW = "#009c9d"
WARNING_TOP = "#e58b11"
WARNING_BOTTOM = "#754008"
WARNING_BORDER = "#ffc45c"
WARNING_GLOW = "#d97706"
DANGER_TOP = "#e54b59"
DANGER_BOTTOM = "#741827"
DANGER_BORDER = "#ff7185"
DANGER_GLOW = "#d92d50"
NEUTRAL_TOP = "#58778d"
NEUTRAL_BOTTOM = "#203443"
NEUTRAL_BORDER = "#86aabd"
NEUTRAL_GLOW = "#35596d"
CONTROL_BORDER_PX = 1
COMMAND_MAIN_DIAMETER = 61
COMMAND_DIAMETER_STEP = 6
COMMAND_MIN_DIAMETER = COMMAND_MAIN_DIAMETER - (2 * COMMAND_DIAMETER_STEP)
COMMAND_BUTTON_GAP = 9
EDGE_COMMAND_DIAMETER = 60
COMMAND_CENTER_X = 233
COMMAND_MAIN_CENTER_Y = 410
COMMAND_ARC_RISE_LINEAR = 10
COMMAND_ARC_RISE_QUADRATIC = 5
COMMAND_SCREENS = frozenset(
    {
        "home",
        "settings",
        "chrome-settings",
        "power-settings",
        "audio",
        "fft",
        "tof",
        "tof-settings",
        "result",
    }
)
WIDE_PAGE_SCREENS = frozenset(
    {
        "battery",
        "logs",
        "settings",
        "storage",
        "hardware",
        "chrome-settings",
        "power-settings",
        "tof-settings",
    }
)
FIXED_CANVAS_SCREENS = frozenset(
    {"audio", "fft", "fft-detail", "imu", "leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings", "gps", "tof"}
)
PAIRING_SCREENS = frozenset(
    {
        "pairing-intro",
        "pairing-bluetooth",
        "pairing-app",
        "pairing-app-qr",
        "pairing-device-qr",
        "pairing-credentials",
        "pairing-connecting",
        "pairing-success",
        "pairing-failed",
    }
)
FONT_DISPLAY = "Montserrat SemiBold"
FONT_BODY = "Montserrat"
FONT_TECH = "Consolas"


def command_diameter(count: int, index: int) -> int:
    right_center = count // 2
    if count % 2:
        distance = abs(index - right_center)
    else:
        left_center = right_center - 1
        distance = (
            left_center - index
            if index < left_center
            else index - right_center if index > right_center else 0
        )
    return max(
        COMMAND_MIN_DIAMETER,
        COMMAND_MAIN_DIAMETER - (distance * COMMAND_DIAMETER_STEP),
    )


def command_geometry(count: int, index: int) -> tuple[int, int, int]:
    if count < 1 or count > 5:
        raise ValueError("Command Arc requires between one and five commands")

    right_center = count // 2
    left_center = right_center - 1 if count % 2 == 0 else right_center
    diameters = [command_diameter(count, slot) for slot in range(count)]
    centers_y = []
    for slot in range(count):
        if count % 2:
            distance = abs(slot - right_center)
        else:
            distance = (
                left_center - slot
                if slot < left_center
                else slot - right_center if slot > right_center else 0
            )
        centers_y.append(
            COMMAND_MAIN_CENTER_Y
            - (COMMAND_ARC_RISE_LINEAR * distance)
            - (COMMAND_ARC_RISE_QUADRATIC * distance * distance)
        )

    centers_x = [0] * count
    if left_center == right_center:
        centers_x[left_center] = COMMAND_CENTER_X
    else:
        center_distance = (
            (diameters[left_center] + diameters[right_center]) // 2
        ) + COMMAND_BUTTON_GAP
        centers_x[left_center] = COMMAND_CENTER_X - (center_distance // 2)
        centers_x[right_center] = centers_x[left_center] + center_distance
    for slot in range(left_center, 0, -1):
        vertical_delta = abs(centers_y[slot] - centers_y[slot - 1])
        target_distance = (
            (diameters[slot] + diameters[slot - 1]) // 2
        ) + COMMAND_BUTTON_GAP
        horizontal_delta = int(
            math.sqrt(
                (target_distance * target_distance)
                - (vertical_delta * vertical_delta)
            )
            + 0.5
        )
        centers_x[slot - 1] = centers_x[slot] - horizontal_delta
    for slot in range(right_center + 1, count):
        vertical_delta = abs(centers_y[slot] - centers_y[slot - 1])
        target_distance = (
            (diameters[slot - 1] + diameters[slot]) // 2
        ) + COMMAND_BUTTON_GAP
        horizontal_delta = int(
            math.sqrt(
                (target_distance * target_distance)
                - (vertical_delta * vertical_delta)
            )
            + 0.5
        )
        centers_x[slot] = centers_x[slot - 1] + horizontal_delta

    diameter = diameters[index]
    return (
        centers_x[index] - (diameter // 2),
        centers_y[index] - (diameter // 2),
        diameter,
    )
