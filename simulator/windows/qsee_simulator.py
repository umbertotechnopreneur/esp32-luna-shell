"""QSee.ai round-device UI simulator for Windows.

The simulator mirrors the firmware flow without pretending to emulate physical
drivers. Mouse clicks stand in for touch and deterministic scenarios provide
synthetic PMU, IMU, GPS, Wi-Fi, BLE, SD, and logger data.
"""

from __future__ import annotations

import argparse
import ctypes
import math
import queue
import sys
import time
import tkinter as tk
import winsound
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageColor, ImageDraw, ImageFilter, ImageTk


DISPLAY_SIZE = 466
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
CONTROL_BORDER_PX = 1
COMMAND_MAIN_DIAMETER = 61
COMMAND_DIAMETER_STEP = 6
COMMAND_MIN_DIAMETER = COMMAND_MAIN_DIAMETER - (2 * COMMAND_DIAMETER_STEP)
COMMAND_BUTTON_GAP = 9
EDGE_COMMAND_DIAMETER = 54
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
        "result",
    }
)
WIDE_PAGE_SCREENS = frozenset(
    {"settings", "storage", "hardware", "chrome-settings", "power-settings"}
)
FIXED_CANVAS_SCREENS = frozenset({"audio", "fft", "fft-detail", "imu"})
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


# Resolve assets in both source runs and PyInstaller bundles.
def resource_path(relative: str) -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
    return base / relative


class CanonicalLogger:
    def __init__(self) -> None:
        self.lines: deque[str] = deque(maxlen=32)
        self.events: queue.SimpleQueue[str] = queue.SimpleQueue()

    def info(self, tag: str, message: str) -> None:
        line = f"I ({int(time.monotonic() * 1000):06d}) {tag}: {message}"
        self.lines.append(line)
        self.events.put(line)
        print(line, flush=True)

    def snapshot(self, count: int = 7, max_chars: int | None = None) -> str:
        lines = list(self.lines)[-count:]
        if max_chars is not None:
            lines = [line if len(line) <= max_chars else f"{line[: max_chars - 1]}…" for line in lines]
        return "\n".join(lines)


@dataclass(frozen=True)
class Scenario:
    name: str
    sd_message: str
    gps_message: str
    final_message: str


SCENARIOS = {
    "healthy": Scenario(
        "HEALTHY",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Synthetic hardware ready",
    ),
    "sd-unformatted": Scenario(
        "SD UNFORMATTED",
        "SD: mount failed; synthetic card requires FAT32 formatting",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Ready with SD degraded",
    ),
    "gps-searching": Scenario(
        "GPS SEARCHING",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: transport ready, waiting for synthetic fix",
        "Ready; GPS still searching",
    ),
}


class QSeeSimulator:
    def __init__(
        self,
        root: tk.Tk,
        scenario: Scenario,
        play_sound: bool = True,
        idle_state: str = "active",
    ) -> None:
        self.root = root
        self.scenario = scenario
        self.idle_state = idle_state
        self.logger = CanonicalLogger()
        self.canvas = tk.Canvas(
            root,
            width=DISPLAY_SIZE,
            height=DISPLAY_SIZE,
            highlightthickness=0,
            background="#101820",
        )
        self.canvas.pack(padx=18, pady=18)
        self.canvas.create_oval(
            0,
            0,
            465,
            465,
            fill=BACKGROUND,
            outline=STATUS_EDGE,
            width=1,
            tags="bezel",
        )
        self.screen = "boot"
        self.audio_phase = 0
        self.audio_timer: str | None = None
        self.performance_mode = False
        self.ready = False
        self.safe_guide = False
        self.timeline_index = 0
        self.boot_image: tk.PhotoImage | None = None
        self.atmosphere_image = self._build_atmosphere()
        self.button_images = {
            (success, diameter): self._build_command_button(success, diameter)
            for success in (False, True)
            for diameter in (
                COMMAND_MIN_DIAMETER,
                COMMAND_MAIN_DIAMETER - COMMAND_DIAMETER_STEP,
                EDGE_COMMAND_DIAMETER,
                COMMAND_MAIN_DIAMETER,
            )
        }
        self.effect_images: list[ImageTk.PhotoImage] = []
        self._load_assets(play_sound)
        self._bind_controls()
        self.show_boot()
        self.root.after(150, self._advance_timeline)
        self.root.after(120, self._refresh_log)

    def _load_assets(self, play_sound: bool) -> None:
        image_path = resource_path("assets/branding/qsee/generated/qsee-boot-466x466.png")
        if image_path.exists():
            self.boot_image = tk.PhotoImage(file=image_path)
        sound_path = resource_path("assets/sfx/generated/qsee-boot.wav")
        if play_sound and sound_path.exists():
            winsound.PlaySound(str(sound_path), winsound.SND_FILENAME | winsound.SND_ASYNC)

    def _bind_controls(self) -> None:
        self.canvas.bind("<Button-1>", self._on_click)
        self.root.bind("<Escape>", lambda _event: self.back())
        self.root.bind("<Return>", lambda _event: self.confirm())
        self.root.bind("c", lambda _event: self.show_confirmation())
        self.root.bind("r", lambda _event: self.reboot())
        self.root.bind("b", lambda _event: self.toggle_fullscreen())
        self.root.bind("p", lambda _event: self.toggle_standby())
        self.root.bind("<F2>", lambda _event: self.toggle_safe_guide())

    def _clear(self) -> None:
        self.canvas.delete("ui", "audio", "idle_overlay")
        self.effect_images.clear()

    def _status_arc_visible(self) -> bool:
        return self.idle_state in {"active", "command-hidden"}

    def _command_arc_visible(self) -> bool:
        return self.idle_state == "active" and self.screen in COMMAND_SCREENS

    def _content_viewport(self) -> tuple[int, int, int, int]:
        top = SAFE_ORIGIN if self._status_arc_visible() else 0
        bottom_inset = SAFE_ORIGIN if self._command_arc_visible() else 0
        return (
            SAFE_ORIGIN,
            top,
            SAFE_SIZE,
            DISPLAY_SIZE - top - bottom_inset,
        )

    def _content_offset_y(self) -> int:
        _, top, _, _ = self._content_viewport()
        return top - SAFE_ORIGIN

    def _wide_page_offset_y(self) -> int:
        _, top, _, _ = self._content_viewport()
        return -35 if top == 0 else 0

    def _fixed_canvas_offset_y(self) -> int:
        _, top, _, height = self._content_viewport()
        centered_top = top + ((height - SAFE_SIZE) // 2)
        return centered_top - SAFE_ORIGIN

    def _next_canvas_item_id(self) -> int:
        items = self.canvas.find_all()
        return items[-1] + 1 if items else 1

    def _move_canvas_items_from(self, first_item_id: int, y_offset: int) -> None:
        if y_offset == 0:
            return
        for item_id in self.canvas.find_all():
            if item_id >= first_item_id:
                self.canvas.move(item_id, 0, y_offset)

    def _scrollable_content_bounds(self) -> tuple[int, int]:
        _, top, _, height = self._content_viewport()
        bottom = top + height
        return (
            max(top, CIRCULAR_CONTENT_INSET),
            min(bottom, DISPLAY_SIZE - CIRCULAR_CONTENT_INSET),
        )

    def _settings_layout(self) -> dict[str, int | None]:
        _, viewport_top, _, viewport_height = self._content_viewport()
        if viewport_height == SAFE_SIZE:
            return {
                "heading_offset": 0,
                "profile": 174,
                "performance": 198,
                "diagnostics": 246,
                "diagnostics_subtitle": 266,
                "cadence": 290,
                "cadence_control": 304,
                "cadence_state": 357,
                "display": None,
                "brightness": None,
                "brightness_control": None,
                "rotation": None,
            }
        if viewport_top == SAFE_ORIGIN:
            return {
                "heading_offset": 0,
                "profile": None,
                "performance": 174,
                "diagnostics": 214,
                "diagnostics_subtitle": None,
                "cadence": 236,
                "cadence_control": 249,
                "cadence_state": 302,
                "display": 324,
                "brightness": 345,
                "brightness_control": 357,
                "rotation": None,
            }
        return {
            "heading_offset": -35,
            "profile": None,
            "performance": 133,
            "diagnostics": 174,
            "diagnostics_subtitle": None,
            "cadence": 196,
            "cadence_control": 209,
            "cadence_state": 263,
            "display": 285,
            "brightness": 306,
            "brightness_control": 318,
            "rotation": 384,
        }

    def _draw_safe_guide(self) -> None:
        if self.safe_guide:
            x, y, width, height = self._content_viewport()
            if (
                self.screen not in WIDE_PAGE_SCREENS
                and self.screen not in FIXED_CANVAS_SCREENS
                and (y != SAFE_ORIGIN or height != SAFE_SIZE)
            ):
                self.canvas.create_rectangle(
                    SAFE_ORIGIN,
                    SAFE_ORIGIN,
                    SAFE_ORIGIN + SAFE_SIZE - 1,
                    SAFE_ORIGIN + SAFE_SIZE - 1,
                    outline="#755c3b",
                    width=1,
                    dash=(2, 5),
                    tags="ui",
                )
            self.canvas.create_rectangle(
                x,
                y,
                x + width - 1,
                y + height - 1,
                outline="#f2994a",
                width=2,
                dash=(6, 4),
                tags="ui",
            )
            self.canvas.create_text(
                DISPLAY_SIZE - 14,
                y + (height // 2),
                text=f"VIEWPORT  {width} × {height}  ·  Y {y}",
                fill="#ffc879",
                font=(FONT_TECH, 8, "bold"),
                angle=90,
                tags="ui",
            )
            if self.screen in WIDE_PAGE_SCREENS:
                content_top, content_bottom = self._scrollable_content_bounds()
                if content_top != y or content_bottom != y + height:
                    self.canvas.create_rectangle(
                        x + 4,
                        content_top,
                        x + width - 5,
                        content_bottom,
                        outline="#63ffd0",
                        width=1,
                        dash=(3, 3),
                        tags="ui",
                    )
                    self.canvas.create_text(
                        x + 13,
                        content_top + ((content_bottom - content_top) // 2),
                        text=f"WIDE CONTENT  ·  Y {content_top}..{content_bottom}",
                        fill="#8fffe3",
                        font=(FONT_TECH, 7, "bold"),
                        angle=90,
                        tags="ui",
                    )
            elif self.screen in FIXED_CANVAS_SCREENS:
                fixed_top = SAFE_ORIGIN + self._fixed_canvas_offset_y()
                if fixed_top != y or height != SAFE_SIZE:
                    self.canvas.create_rectangle(
                        x + 4,
                        fixed_top,
                        x + width - 5,
                        fixed_top + SAFE_SIZE - 1,
                        outline="#8f7dff",
                        width=1,
                        dash=(3, 3),
                        tags="ui",
                    )
                    self.canvas.create_text(
                        x + 13,
                        fixed_top + (SAFE_SIZE // 2),
                        text=f"FIXED CANVAS  ·  Y {fixed_top}..{fixed_top + SAFE_SIZE}",
                        fill="#b8adff",
                        font=(FONT_TECH, 7, "bold"),
                        angle=90,
                        tags="ui",
                    )

    def _draw_atmosphere(self) -> None:
        self.canvas.create_image(
            0,
            0,
            image=self.atmosphere_image,
            anchor="nw",
            tags="ui",
        )

    def _build_atmosphere(self) -> ImageTk.PhotoImage:
        start = ImageColor.getrgb("#0d3547")
        end = ImageColor.getrgb("#010407")
        lower_blue = ImageColor.getrgb("#071b34")
        pixels: list[tuple[int, int, int, int]] = []
        for y in range(DISPLAY_SIZE):
            for x in range(DISPLAY_SIZE):
                screen_distance = math.hypot(x - 232.5, y - 232.5)
                if screen_distance >= 233:
                    alpha = max(0, int((234 - screen_distance) * 255))
                else:
                    alpha = 255

                progress = min(math.hypot(x - 233, y - 158) / 368, 1.0)
                progress = progress * progress * (3.0 - (2.0 * progress))
                upper_channels = tuple(
                    round(a + ((b - a) * progress))
                    for a, b in zip(start, end)
                )
                lower_strength = max(
                    0.0,
                    1.0 - (math.hypot(x - 278, y - 286) / 282),
                ) ** 2
                vignette = 1.0 - (
                    max(0.0, min((screen_distance - 152) / 82, 1.0)) * 0.72
                )
                channels = tuple(
                    round(
                        min(
                            255,
                            (channel * vignette) +
                            (lower * lower_strength * 0.42),
                        )
                    )
                    for channel, lower in zip(upper_channels, lower_blue)
                )
                pixels.append((*channels, alpha))

        image = Image.new("RGBA", (DISPLAY_SIZE, DISPLAY_SIZE))
        image.putdata(pixels)
        draw = ImageDraw.Draw(image)
        draw.ellipse(
            (2, 2, DISPLAY_SIZE - 3, DISPLAY_SIZE - 3),
            outline=(*ImageColor.getrgb(STATUS_EDGE), 190),
            width=1,
        )
        return ImageTk.PhotoImage(image)

    def _draw_panel(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        *,
        top: str = "#0d2735",
        bottom: str = "#07131c",
        border: str = "#24536a",
        glow: str = STATUS_GLOW,
        glow_alpha: int = 42,
        radius: int = 14,
    ) -> None:
        scale = 4
        pad = 10
        image_size = ((width + (pad * 2)) * scale, (height + (pad * 2)) * scale)
        image = Image.new("RGBA", image_size, (0, 0, 0, 0))
        bounds = (
            pad * scale,
            pad * scale,
            (pad + width - 1) * scale,
            (pad + height - 1) * scale,
        )

        if glow_alpha > 0:
            glow_layer = Image.new("RGBA", image_size, (0, 0, 0, 0))
            ImageDraw.Draw(glow_layer).rounded_rectangle(
                bounds,
                radius=radius * scale,
                fill=(*ImageColor.getrgb(glow), glow_alpha),
            )
            image.alpha_composite(
                glow_layer.filter(ImageFilter.GaussianBlur(7 * scale))
            )

        mask = Image.new("L", image_size, 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            bounds,
            radius=radius * scale,
            fill=255,
        )
        gradient = Image.new("RGBA", image_size, (0, 0, 0, 0))
        gradient_draw = ImageDraw.Draw(gradient)
        top_rgb = ImageColor.getrgb(top)
        bottom_rgb = ImageColor.getrgb(bottom)
        for row in range(height * scale):
            progress = row / max(1, (height * scale) - 1)
            color = tuple(
                round(a + ((b - a) * progress))
                for a, b in zip(top_rgb, bottom_rgb)
            )
            gradient_draw.line(
                (
                    (pad * scale, (pad * scale) + row),
                    ((pad + width) * scale, (pad * scale) + row),
                ),
                fill=(*color, 244),
            )
        image.alpha_composite(
            Image.composite(gradient, Image.new("RGBA", image_size), mask)
        )

        image = image.resize(
            (width + (pad * 2), height + (pad * 2)),
            Image.Resampling.LANCZOS,
        )
        ImageDraw.Draw(image).rounded_rectangle(
            (pad, pad, pad + width - 1, pad + height - 1),
            radius=radius,
            outline=(*ImageColor.getrgb(border), 225),
            width=CONTROL_BORDER_PX,
        )
        photo = ImageTk.PhotoImage(image)
        self.effect_images.append(photo)
        self.canvas.create_image(
            x - pad,
            y - pad,
            image=photo,
            anchor="nw",
            tags="ui",
        )

    def _build_command_button(
        self,
        success: bool,
        circle_size: int,
    ) -> ImageTk.PhotoImage:
        scale = 4
        sprite_size = circle_size + 24
        pad = (sprite_size - circle_size) // 2
        top = SUCCESS_TOP if success else PRIMARY_TOP
        bottom = SUCCESS_BOTTOM if success else PRIMARY_BOTTOM
        border = SUCCESS_BORDER if success else PRIMARY_BORDER
        glow = SUCCESS_GLOW if success else PRIMARY_GLOW

        canvas = Image.new(
            "RGBA",
            (sprite_size * scale, sprite_size * scale),
            (0, 0, 0, 0),
        )
        glow_layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        glow_draw = ImageDraw.Draw(glow_layer)
        glow_draw.ellipse(
            (
                pad * scale,
                pad * scale,
                (pad + circle_size) * scale,
                (pad + circle_size) * scale,
            ),
            fill=(*ImageColor.getrgb(glow), 175),
        )
        glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(7 * scale))
        canvas.alpha_composite(glow_layer)

        mask = Image.new("L", canvas.size, 0)
        mask_draw = ImageDraw.Draw(mask)
        bounds = (
            pad * scale,
            pad * scale,
            (pad + circle_size) * scale,
            (pad + circle_size) * scale,
        )
        mask_draw.ellipse(bounds, fill=255)

        gradient = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        gradient_draw = ImageDraw.Draw(gradient)
        top_rgb = ImageColor.getrgb(top)
        bottom_rgb = ImageColor.getrgb(bottom)
        for row in range(circle_size * scale):
            progress = row / max(1, (circle_size * scale) - 1)
            color = tuple(
                round(a + ((b - a) * progress))
                for a, b in zip(top_rgb, bottom_rgb)
            )
            y = (pad * scale) + row
            gradient_draw.line(
                ((pad * scale, y), ((pad + circle_size) * scale, y)),
                fill=(*color, 255),
                width=1,
            )
        canvas.alpha_composite(Image.composite(gradient, Image.new("RGBA", canvas.size), mask))

        canvas = canvas.resize(
            (sprite_size, sprite_size),
            Image.Resampling.LANCZOS,
        )
        ImageDraw.Draw(canvas).ellipse(
            (
                pad,
                pad,
                pad + circle_size,
                pad + circle_size,
            ),
            outline=(*ImageColor.getrgb(border), 255),
            width=CONTROL_BORDER_PX,
        )
        return ImageTk.PhotoImage(canvas)

    def _draw_glow_disc(
        self,
        x: int,
        y: int,
        radius: int,
        color: str,
        alpha: int = 115,
    ) -> None:
        blur = 8
        size = (radius + blur) * 2
        image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.ellipse(
            (blur, blur, size - blur, size - blur),
            fill=(*ImageColor.getrgb(color), alpha),
        )
        image = image.filter(ImageFilter.GaussianBlur(blur))
        photo = ImageTk.PhotoImage(image)
        self.effect_images.append(photo)
        self.canvas.create_image(x, y, image=photo, tags="ui")

    def _status_colors(self) -> list[str]:
        if self.scenario.name == "SD UNFORMATTED":
            return [
                STATUS_READY,
                STATUS_READY,
                STATUS_READY,
                STATUS_WARNING,
                STATUS_READY,
                STATUS_WARNING,
            ]
        if self.scenario.name == "GPS SEARCHING":
            return [
                STATUS_READY,
                STATUS_READY,
                STATUS_READY,
                STATUS_WARNING,
                STATUS_WARNING,
                STATUS_READY,
            ]
        return [
            STATUS_READY,
            STATUS_READY,
            STATUS_READY,
            STATUS_WARNING,
            STATUS_READY,
            STATUS_READY,
        ]

    def _draw_status_icon(self, kind: str, x: int, y: int, color: str) -> None:
        if kind == "usb":
            self.canvas.create_line(x, y - 10, x, y + 8, fill=color, width=3, tags="ui")
            self.canvas.create_line(x - 7, y - 6, x + 7, y - 6, fill=color, width=3, tags="ui")
            self.canvas.create_line(x - 5, y - 12, x - 5, y - 5, fill=color, width=2, tags="ui")
            self.canvas.create_line(x + 5, y - 12, x + 5, y - 5, fill=color, width=2, tags="ui")
            self.canvas.create_line(x - 4, y + 8, x + 4, y + 8, fill=color, width=3, tags="ui")
        elif kind == "battery":
            self.canvas.create_rectangle(
                x - 11,
                y - 8,
                x + 10,
                y + 8,
                outline=color,
                width=2,
                tags="ui",
            )
            self.canvas.create_rectangle(
                x + 11,
                y - 4,
                x + 14,
                y + 4,
                fill=color,
                outline=color,
                tags="ui",
            )
            self.canvas.create_rectangle(
                x - 8,
                y - 5,
                x + 5,
                y + 5,
                fill=color,
                outline="",
                tags="ui",
            )
        elif kind == "ble":
            points = (x, y - 13, x + 8, y - 6, x, y, x + 8, y + 7, x, y + 13, x, y - 13)
            self.canvas.create_line(*points, fill=color, width=2, tags="ui")
            self.canvas.create_line(x - 7, y - 7, x, y, x - 7, y + 7, fill=color, width=2, tags="ui")
        elif kind == "wifi":
            for inset in (0, 6, 12):
                self.canvas.create_arc(
                    x - 15 + inset,
                    y - 12 + inset,
                    x + 15 - inset,
                    y + 14 - inset,
                    start=35,
                    extent=110,
                    style=tk.ARC,
                    outline=color,
                    width=2,
                    tags="ui",
                )
            self.canvas.create_oval(x - 2, y + 8, x + 2, y + 12, fill=color, outline="", tags="ui")
        elif kind == "gps":
            self.canvas.create_oval(x - 10, y - 12, x + 10, y + 8, outline=color, width=2, tags="ui")
            self.canvas.create_oval(x - 3, y - 5, x + 3, y + 1, fill=color, outline="", tags="ui")
            self.canvas.create_line(x - 7, y + 5, x, y + 14, x + 7, y + 5, fill=color, width=2, tags="ui")
        elif kind == "sd":
            self.canvas.create_polygon(
                x - 11,
                y - 13,
                x + 4,
                y - 13,
                x + 11,
                y - 6,
                x + 11,
                y + 13,
                x - 11,
                y + 13,
                outline=color,
                fill="",
                width=2,
                tags="ui",
            )
            for offset in (-5, 0, 5):
                self.canvas.create_line(
                    x + offset,
                    y - 9,
                    x + offset,
                    y - 3,
                    fill=color,
                    width=2,
                    tags="ui",
                )

    def _draw_status_bar(self) -> None:
        if not self._status_arc_visible():
            return
        positions = (
            ("usb", 106, 53, 78),
            ("battery", 156, 33, 58),
            ("ble", 206, 22, 47),
            ("wifi", 256, 22, 47),
            ("gps", 306, 33, 58),
            ("sd", 356, 53, 78),
        )
        for (kind, x, icon_y, dot_y), color in zip(positions, self._status_colors()):
            self._draw_glow_disc(x, icon_y, 12, color, 105)
            self._draw_glow_disc(x, dot_y, 5, color, 150)
            self._draw_status_icon(kind, x, icon_y, color)
            self.canvas.create_oval(
                x - 4,
                dot_y - 4,
                x + 4,
                dot_y + 4,
                fill=color,
                outline="",
                tags="ui",
            )

    def _draw_command_button(
        self,
        x: int,
        y: int,
        icon: str,
        success: bool,
        diameter: int = EDGE_COMMAND_DIAMETER,
    ) -> None:
        icon_x, icon_y = {
            "➤": (1, 0),
            "i": (0, -1),
            "‹": (0, -2),
        }.get(icon, (0, 0))
        self.canvas.create_image(
            x + (diameter / 2),
            y + (diameter / 2),
            image=self.button_images[(success, diameter)],
            tags="ui",
        )
        self.canvas.create_text(
            x + (diameter / 2) + icon_x,
            y + (diameter / 2) + icon_y,
            text=icon,
            fill="#ffffff",
            font=("Segoe UI Symbol", 18, "bold"),
            tags="ui",
        )

    def _draw_command_arc(self) -> None:
        if not self._command_arc_visible():
            return
        commands = (
            ("≡", False),
            ("♫", False),
            ("▰", True),
            ("➤", True),
            ("⚙", False),
        )
        for index, (icon, success) in enumerate(commands):
            x, y, diameter = command_geometry(len(commands), index)
            self._draw_command_button(x, y, icon, success, diameter)

    def _draw_edge_back(self) -> None:
        self._draw_command_button(10, 203, "‹", True)

    def _draw_scenario_label(self) -> None:
        self.canvas.create_text(
            DISPLAY_SIZE // 2,
            108,
            text=f"SYNTHETIC · {self.scenario.name}",
            fill=MUTED,
            font=(FONT_DISPLAY, 8),
            tags="ui",
        )

    def show_boot(self) -> None:
        self.screen = "boot"
        self._clear()
        if self.boot_image is not None:
            self.canvas.create_image(233, 233, image=self.boot_image, tags="ui")
        else:
            self.canvas.create_text(233, 135, text="QSee.ai", fill=BLUE, font=(FONT_DISPLAY, 34), tags="ui")
        self._draw_scenario_label()
        self.canvas.create_rectangle(73, 222, 393, 374, fill=NAVY, outline=BLUE, width=1, tags="ui")
        self.canvas.create_text(
            87,
            234,
            anchor="nw",
            width=292,
            text=self.logger.snapshot(count=5, max_chars=58),
            fill="#d8ecff",
            font=("Consolas", 8),
            tags=("ui", "boot_log"),
        )
        self.canvas.create_text(
            233,
            430,
            text="TOUCH TO CONTINUE" if self.ready else "INITIALIZING",
            fill=BLUE,
            font=("Segoe UI", 11, "bold"),
            tags=("ui", "continue"),
        )
        self._draw_safe_guide()

    def _refresh_log(self) -> None:
        if self.screen == "boot":
            self.canvas.itemconfigure("boot_log", text=self.logger.snapshot(count=5, max_chars=58))
        self.root.after(120, self._refresh_log)

    # Replay deterministic hardware events without emulating device drivers.
    def _advance_timeline(self) -> None:
        events = [
            ("qsee_logger", "Canonical logger online: console + RAM ring"),
            ("qsee_main", self.scenario.sd_message),
            ("qsee_boot", "Boot screen online: QSee.ai + canonical live log"),
            ("qsee_main", "Touch CST9217: synthetic input ready"),
            ("qsee_hw", "PMU AXP2101: synthetic battery 83%, discharging, 4021mV"),
            ("qsee_hw", "IMU QMI8658: synthetic orientation x=12 y=-18 z=998mg"),
            ("qsee_hw", self.scenario.gps_message),
            ("qsee_radios", "Wi-Fi: synthetic STA ready, credentials not configured"),
            ("qsee_radios", "BLE: synthetic advertising as QSEE-SIM1"),
            ("qsee_main", self.scenario.final_message),
        ]
        if self.timeline_index < len(events):
            tag, message = events[self.timeline_index]
            self.logger.info(tag, message)
            self.timeline_index += 1
            self.root.after(430, self._advance_timeline)
            return

        self.ready = True
        self.logger.info("qsee_boot", "Startup complete; touch to continue")
        self.canvas.itemconfigure("continue", text="TOUCH TO CONTINUE")

    def _on_click(self, event: tk.Event) -> None:
        if self.screen == "boot":
            if self.ready:
                self.logger.info("qsee_boot", "Synthetic touch confirmed; opening QSee.ai")
                self.show_home()
            else:
                self.logger.info("qsee_boot", "Synthetic touch received; startup still in progress")
            return

        if self.idle_state == "standby":
            self.logger.info(
                "qsee_power_manager",
                "Synthetic touch ignored in standby; use PWR or IMU wake",
            )
            return
        if self.idle_state not in {"active", "fullscreen"}:
            self.idle_state = "active"
            self.logger.info("qsee_ui", "Synthetic touch restored Adaptive Chrome")
            self._redraw_current_screen()
            return

        # Hit boxes mirror the drawn controls on the fixed 466 px canvas.
        command_arc_visible = self._command_arc_visible()
        if 6 <= event.x <= 68 and 190 <= event.y <= 276 and self.screen == "confirmation":
            self.cancel()
        elif 398 <= event.x <= 460 and 190 <= event.y <= 276 and self.screen == "confirmation":
            self.confirm()
        elif (
            10 <= event.x <= 70
            and 203 <= event.y <= 263
            and self.screen in {
                "audio",
                "fft",
                "fft-detail",
                "imu",
                "settings",
                "storage",
                "chrome-settings",
                "power-settings",
                "hardware",
            }
        ):
            self.back()
        elif (
            command_arc_visible
            and 147 <= event.x <= 203
            and 346 <= event.y <= 402
            and self.screen == "home"
        ):
            self.show_audio_demo()
        elif (
            command_arc_visible
            and 263 <= event.x <= 319
            and 346 <= event.y <= 402
            and self.screen == "home"
        ):
            self.show_imu_demo()
        elif (
            command_arc_visible
            and 316 <= event.x <= 370
            and 322 <= event.y <= 376
            and self.screen == "home"
        ):
            self.show_settings_demo()
        elif (
            command_arc_visible
            and 204 <= event.x <= 262
            and 360 <= event.y <= 418
            and self.screen == "audio"
        ):
            self.show_audio_fft_demo()
        elif (
            command_arc_visible
            and 204 <= event.x <= 262
            and 360 <= event.y <= 418
            and self.screen == "fft"
        ):
            self.show_audio_detail_demo()
        elif self.screen == "settings":
            performance_y = int(self._settings_layout()["performance"])
            if (
                300 <= event.x <= 380
                and performance_y - 14 <= event.y <= performance_y + 32
            ):
                self.performance_mode = not self.performance_mode
                self.logger.info(
                    "qsee_performance",
                    "Synthetic Performance Mode "
                    + ("enabled" if self.performance_mode else "disabled"),
                )
                self.show_settings_demo()
                return
            if command_arc_visible:
                for index in range(2):
                    x, y, diameter = command_geometry(2, index)
                    if (
                        x <= event.x <= x + diameter
                        and y <= event.y <= y + diameter
                    ):
                        self.show_settings_demo()
                        break
        elif (
            self.screen == "storage"
            and 128 <= event.x <= 338
            and 358 <= event.y <= 402
        ):
            if self.scenario.name == "SD UNFORMATTED":
                self.logger.info(
                    "qsee_storage",
                    "Synthetic explicit format confirmation requested",
                )
                self.show_confirmation()
            else:
                self.logger.info(
                    "qsee_storage",
                    "Synthetic format rejected: no verified format-required state",
                )

    def _draw_page_heading(
        self,
        title: str,
        subtitle: str,
        y_offset: int | None = None,
    ) -> None:
        if y_offset is None:
            y_offset = self._content_offset_y()
        self.canvas.create_text(
            94,
            110 + y_offset,
            anchor="w",
            text=title,
            fill=TEXT,
            font=(FONT_DISPLAY, 21),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            136 + y_offset,
            anchor="w",
            text=subtitle,
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 12),
            tags="ui",
        )
        self.canvas.create_line(
            94,
            154 + y_offset,
            126,
            154 + y_offset,
            fill=STATUS_READY,
            width=2,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self.canvas.create_line(
            134,
            154 + y_offset,
            372,
            154 + y_offset,
            fill="#163545",
            width=1,
            tags="ui",
        )

    def _draw_performance_disabled(
        self,
        service: str,
        show_next_command: bool = False,
        y_offset: int = 0,
    ) -> None:
        self.canvas.create_text(
            233,
            190 + y_offset,
            text="PERFORMANCE MODE",
            fill=STATUS_WARNING,
            font=(FONT_DISPLAY, 14),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            226 + y_offset,
            text=f"{service} DISABLED",
            fill=TEXT,
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            254 + y_offset,
            text="CONNECTIVITY REMAINS AVAILABLE",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        if show_next_command and self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▥", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def _draw_settings_control(self, y: int, value: str) -> None:
        self._draw_panel(
            92,
            y,
            282,
            44,
            top="#173145",
            bottom="#0d1a24",
            border="#3a6c83",
            glow="#0b55d9",
            glow_alpha=25,
            radius=10,
        )
        self.canvas.create_text(
            106,
            y + 22,
            anchor="w",
            text=value,
            fill=TEXT,
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self.canvas.create_text(
            356,
            y + 22,
            text="⌄",
            fill="#d8ecff",
            font=("Segoe UI Symbol", 15, "bold"),
            tags="ui",
        )

    def _draw_settings_switch(
        self,
        y: int,
        title: str,
        subtitle: str | None,
        checked: bool,
    ) -> None:
        self.canvas.create_text(
            94,
            y,
            anchor="w",
            text=title,
            fill=TEXT,
            font=(FONT_DISPLAY, 11),
            tags="ui",
        )
        if subtitle is not None:
            self.canvas.create_text(
                94,
                y + 20,
                anchor="w",
                text=subtitle,
                fill=MUTED_BRIGHT,
                font=(FONT_BODY, 8),
                tags="ui",
            )
        self.canvas.create_oval(
            318,
            y - 4,
            372,
            y + 26,
            fill=STATUS_READY if checked else "#243744",
            outline="#63ffd0" if checked else "#506675",
            width=2,
            tags="ui",
        )
        knob_x = 356 if checked else 334
        self.canvas.create_oval(
            knob_x - 10,
            y + 1,
            knob_x + 10,
            y + 21,
            fill="#ffffff",
            outline="",
            tags="ui",
        )

    def _draw_shell(self, title: str, message: str, primary: bool, actions: bool) -> None:
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        self._draw_scenario_label()
        self.canvas.create_text(
            118,
            132,
            anchor="w",
            text=title.upper(),
            fill=TEXT,
            font=(FONT_DISPLAY, 19),
            tags="ui",
        )
        self.canvas.create_text(
            118,
            160,
            anchor="nw",
            text=message,
            width=300 if primary else 258,
            justify="left",
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 9),
            tags="ui",
        )
        if primary:
            self.canvas.create_line(
                118,
                188,
                150,
                188,
                fill=STATUS_READY,
                width=2,
                capstyle=tk.ROUND,
                tags="ui",
            )
            self.canvas.create_line(
                158,
                188,
                350,
                184,
                fill="#163545",
                width=1,
                tags="ui",
            )
            self._draw_home_telemetry()
            self._draw_command_arc()
        if actions:
            self.canvas.create_oval(6, 190, 68, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(37, 233, text="CANCEL", fill="white", font=("Segoe UI", 8, "bold"), tags="ui")
            self.canvas.create_oval(398, 190, 460, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(429, 233, text="OK", fill="white", font=("Segoe UI", 9, "bold"), tags="ui")
        self._draw_safe_guide()

    def _draw_home_telemetry(self) -> None:
        telemetry = (
            ("INTERNAL RAM", "246/320K L62K F74", 0.77, STATUS_READY),
            ("PSRAM", "3350/4160K L2560K F42", 0.80, STATUS_READY),
            ("FLASH APP", "5901/14476 KB · 40%", 0.40, BLUE),
            ("CORE 0", "18%", 0.18, STATUS_READY),
            ("CORE 1", "34%", 0.34, BLUE),
        )
        self.canvas.create_text(
            116,
            193,
            anchor="w",
            text="SYNTHETIC MEMORY / CPU",
            fill="#51c7f2",
            font=(FONT_DISPLAY, 7),
            tags="ui",
        )
        y = 207
        for label, value, ratio, color in telemetry:
            label_font_size = 8 if label == "INTERNAL RAM" else 9
            self.canvas.create_text(
                116,
                y,
                anchor="w",
                text=label,
                fill=TEXT,
                font=(FONT_DISPLAY, label_font_size),
                tags="ui",
            )
            self.canvas.create_text(
                370,
                y,
                anchor="e",
                text=value,
                fill=TEXT,
                font=(FONT_TECH, 6),
                tags="ui",
            )
            self.canvas.create_line(
                116,
                y + 18,
                350,
                y + 18,
                fill=TRACK,
                width=5,
                capstyle=tk.ROUND,
                tags="ui",
            )
            self.canvas.create_line(
                116,
                y + 18,
                116 + int(234 * ratio),
                y + 18,
                fill=color,
                width=5,
                capstyle=tk.ROUND,
                tags="ui",
            )
            y += 25

        wifi_color = STATUS_WARNING
        gps_color = STATUS_WARNING if self.scenario.name == "GPS SEARCHING" else STATUS_READY
        self.canvas.create_text(
            116,
            337,
            anchor="w",
            text="WIFI  OFF",
            fill=wifi_color,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            350,
            337,
            anchor="e",
            text=(
                "GPS  SEARCHING"
                if self.scenario.name == "GPS SEARCHING"
                else "GPS  3D FIX"
            ),
            fill=gps_color,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )

    def show_home(self) -> None:
        self.screen = "home"
        self._draw_shell(
            "Welcome",
            "Device console · one snapshot",
            primary=True,
            actions=False,
        )

    def show_settings_demo(self) -> None:
        self.screen = "settings"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        layout = self._settings_layout()
        self._draw_page_heading(
            "SETTINGS",
            "All controls · swipe to scroll",
            int(layout["heading_offset"]),
        )
        profile_y = layout["profile"]
        if profile_y is not None:
            self.canvas.create_text(
                94,
                profile_y,
                anchor="w",
                text=(
                    "Performance profile is active"
                    if self.performance_mode
                    else "Standard service profile"
                ),
                fill="#51c7f2",
                font=(FONT_BODY, 10),
                tags="ui",
            )
        self._draw_settings_switch(
            int(layout["performance"]),
            "Performance mode",
            "Reduces background checks",
            self.performance_mode,
        )
        self.canvas.create_text(
            94,
            int(layout["diagnostics"]),
            anchor="w",
            text="DIAGNOSTICS",
            fill=STATUS_READY,
            font=(FONT_DISPLAY, 14),
            tags="ui",
        )
        diagnostics_subtitle_y = layout["diagnostics_subtitle"]
        if diagnostics_subtitle_y is not None:
            self.canvas.create_text(
                94,
                diagnostics_subtitle_y,
                anchor="w",
                text="Live snapshots and diagnostics",
                fill=MUTED_BRIGHT,
                font=(FONT_BODY, 11),
                tags="ui",
            )
        self.canvas.create_text(
            94,
            int(layout["cadence"]),
            anchor="w",
            text="Full Snapshot cadence",
            fill=TEXT,
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self._draw_settings_control(int(layout["cadence_control"]), "1 Hz")
        self.canvas.create_text(
            94,
            int(layout["cadence_state"]),
            anchor="w",
            text="1 Hz updates are active",
            fill="#51c7f2",
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        display_y = layout["display"]
        if display_y is not None:
            self.canvas.create_text(
                94,
                display_y,
                anchor="w",
                text="DISPLAY & MOTION",
                fill=STATUS_READY,
                font=(FONT_DISPLAY, 14),
                tags="ui",
            )
            self.canvas.create_text(
                94,
                int(layout["brightness"]),
                anchor="w",
                text="AMOLED brightness",
                fill=TEXT,
                font=(FONT_DISPLAY, 11),
                tags="ui",
            )
            self._draw_settings_control(
                int(layout["brightness_control"]),
                "Balanced · 70%",
            )
        rotation_y = layout["rotation"]
        if rotation_y is not None:
            self._draw_settings_switch(
                rotation_y,
                "Auto rotation",
                None,
                True,
            )
        if self._command_arc_visible():
            commands = (("➤", True), ("i", False))
            for index, (icon, success) in enumerate(commands):
                x, y, diameter = command_geometry(len(commands), index)
                self._draw_command_button(x, y, icon, success, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_hardware_demo(self) -> None:
        self.screen = "hardware"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        y_offset = self._wide_page_offset_y()
        self._draw_page_heading(
            "HARDWARE",
            "Tap a row for device details",
            y_offset,
        )
        rows = (
            ("BATTERY", "AXP2101", "READY"),
            ("RTC", "PCF85063", "READY"),
            ("IMU", "QMI8658", "READY"),
            ("GPS", "LC76G", "3D FIX"),
            ("WIFI", "ESP32-S3", "NOT CONFIGURED"),
        )
        for index, (name, code, state) in enumerate(rows):
            y = 173 + y_offset + index * 42
            state_color = (
                STATUS_WARNING if state == "NOT CONFIGURED" else STATUS_READY
            )
            self.canvas.create_text(
                94,
                y,
                anchor="w",
                text=name,
                fill=TEXT,
                font=(FONT_DISPLAY, 11),
                tags="ui",
            )
            self.canvas.create_text(
                230,
                y,
                anchor="w",
                text=code,
                fill="#80dfff",
                font=(FONT_TECH, 9, "bold"),
                tags="ui",
            )
            self.canvas.create_text(
                94,
                y + 18,
                anchor="w",
                text=state,
                fill=state_color,
                font=(FONT_TECH, 8, "bold"),
                tags="ui",
            )
            if index < len(rows) - 1:
                self.canvas.create_line(
                    94,
                    y + 33,
                    372,
                    y + 33,
                    fill="#173545",
                    width=1,
                    tags="ui",
                )
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_storage_demo(self) -> None:
        self.screen = "storage"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        y_offset = self._wide_page_offset_y()
        self._draw_page_heading(
            "SD CARD",
            "Status and maintenance",
            y_offset,
        )

        format_required = self.scenario.name == "SD UNFORMATTED"
        status = "FORMAT REQUIRED" if format_required else "READY"
        status_color = STATUS_WARNING if format_required else STATUS_READY
        detail = (
            "Verified card needs FAT formatting."
            if format_required
            else "FAT filesystem mounted."
        )
        self.canvas.create_text(
            94,
            174 + y_offset,
            anchor="w",
            text="CURRENT STATUS",
            fill=STATUS_READY,
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            198 + y_offset,
            anchor="w",
            text=status,
            fill=status_color,
            font=(FONT_DISPLAY, 17),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            224 + y_offset,
            anchor="w",
            text=detail,
            fill="#d6efff",
            font=(FONT_BODY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            250 + y_offset,
            anchor="w",
            text="NOT MOUNTED" if format_required else "MOUNTED | LOG MIRROR ON",
            fill="#51c7f2",
            font=(FONT_TECH, 9, "bold"),
            tags="ui",
        )

        total = "UNAVAILABLE" if format_required else "7812 MB"
        free = "UNAVAILABLE" if format_required else "6420 MB"
        for row_y, label, value in (
            (278, "TOTAL SPACE", total),
            (302, "FREE SPACE", free),
        ):
            self.canvas.create_text(
                94,
                row_y + y_offset,
                anchor="w",
                text=label,
                fill=MUTED_BRIGHT,
                font=(FONT_DISPLAY, 10),
                tags="ui",
            )
            self.canvas.create_text(
                372,
                row_y + y_offset,
                anchor="e",
                text=value,
                fill=TEXT,
                font=(FONT_TECH, 10, "bold"),
                tags="ui",
            )

        if not format_required:
            self.canvas.create_rectangle(
                94,
                320 + y_offset,
                372,
                330 + y_offset,
                fill="#0b1e2c",
                outline="",
                tags="ui",
            )
            self.canvas.create_rectangle(
                94,
                320 + y_offset,
                144,
                330 + y_offset,
                fill=STATUS_READY,
                outline="",
                tags="ui",
            )
        self.canvas.create_text(
            233,
            344 + y_offset,
            text=(
                "Space unavailable until mounted"
                if format_required
                else "USED 1392 MB | 17%"
            ),
            fill=MUTED,
            font=(FONT_TECH, 9),
            tags="ui",
        )

        button_enabled = format_required
        self.canvas.create_rectangle(
            128,
            358 + y_offset,
            338,
            402 + y_offset,
            fill="#b52b3a" if button_enabled else TRACK,
            outline="#ff8a96" if button_enabled else "#546874",
            width=1,
            tags="ui",
        )
        self.canvas.create_text(
            233,
            380 + y_offset,
            text="FORMAT SD CARD" if button_enabled else "FORMAT NOT REQUIRED",
            fill="#ffffff" if button_enabled else MUTED_BRIGHT,
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self._draw_edge_back()
        self._draw_safe_guide()

    def _draw_audio_bars(
        self,
        baseline: int,
        max_height: int,
        color_start: str,
        color_end: str,
        offset: int,
    ) -> None:
        profile = (
            0.18, 0.24, 0.31, 0.39, 0.48, 0.61, 0.74, 0.88,
            1.00, 0.94, 0.84, 0.70, 0.58, 0.46, 0.36, 0.27,
        )
        for index, base in enumerate(profile):
            motion = 0.08 * math.sin(
                (self.audio_phase + (index * 3) + offset) / 4.0
            )
            value = max(4, int(max_height * max(0.08, min(1.0, base + motion))))
            x = 102 + (index * 16)
            self.canvas.create_rectangle(
                x,
                baseline - max_height,
                x + 9,
                baseline,
                fill="#0b2632",
                outline="",
                tags="audio",
            )
            self.canvas.create_rectangle(
                x,
                baseline - value,
                x + 9,
                baseline,
                fill=color_start if index < 8 else color_end,
                outline="",
                tags="audio",
            )
            self.canvas.create_line(
                x + 1,
                baseline - value,
                x + 8,
                baseline - value,
                fill="#c9f7ff",
                width=1,
                tags="audio",
            )

    def show_audio_demo(self) -> None:
        self.screen = "audio"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO SCOPE",
            "Microphones · diagnostics"
            if self.performance_mode
            else "Synthetic PCM16 · 22.05 kHz",
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "AUDIO CAPTURE",
                True,
                y_offset,
            )
            return
        self.canvas.create_text(
            102,
            180,
            text="MIC 1",
            anchor="w",
            fill="#61d8ff",
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self.canvas.create_text(
            364,
            180,
            text="-12.4 dBFS",
            anchor="e",
            fill=TEXT,
            font=(FONT_TECH, 10, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            102,
            254,
            text="MIC 2",
            anchor="w",
            fill="#ff9d5c",
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self.canvas.create_text(
            364,
            254,
            text="-18.1 dBFS",
            anchor="e",
            fill=TEXT,
            font=(FONT_TECH, 10, "bold"),
            tags="ui",
        )
        self.canvas.create_line(
            102,
            246,
            364,
            246,
            fill="#183949",
            width=1,
            tags="ui",
        )
        self._draw_audio_bars(239, 43, "#27bdf2", "#c168ff", 0)
        self._draw_audio_bars(313, 43, "#ffad3d", "#77e37c", 9)

        self.canvas.create_text(
            233,
            347,
            text="PCM16 · 2 CHANNELS     CPU UNAVAILABLE",
            fill=STATUS_WARNING,
            font=(FONT_TECH, 9, "bold"),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▥", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()
        self._animate_audio()

    def show_audio_fft_demo(self) -> None:
        if self.audio_timer is not None:
            self.root.after_cancel(self.audio_timer)
            self.audio_timer = None
        self.screen = "fft"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO FFT",
            "Spectrum diagnostics"
            if self.performance_mode
            else "Mean of Mic 1 + Mic 2 · 16 bands",
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "FFT SAMPLING",
                True,
                y_offset,
            )
            return
        self.canvas.create_text(
            104,
            179,
            anchor="w",
            text="DOMINANT",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            104,
            194,
            anchor="w",
            text="1.72 kHz",
            fill=STATUS_READY,
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            362,
            179,
            anchor="e",
            text="PEAK",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            362,
            194,
            anchor="e",
            text="-8.4 dBFS",
            fill="#f0b44d",
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        for y, label in ((218, "0"), (263, "-30"), (308, "-60")):
            self.canvas.create_line(
                108,
                y,
                360,
                y,
                fill="#173746",
                width=1,
                tags="ui",
            )
            self.canvas.create_text(
                102,
                y,
                anchor="e",
                text=label,
                fill="#55778a",
                font=(FONT_TECH, 8),
                tags="ui",
            )
        heights = (
            12, 20, 31, 45, 62, 82, 102, 92,
            75, 60, 49, 39, 31, 24, 18, 13,
        )
        for index, height in enumerate(heights):
            x = 108 + index * 15
            color = "#27bdf2" if index < 6 else "#2be7d0" if index < 11 else "#f0b44d"
            self.canvas.create_rectangle(
                x,
                309 - height,
                x + 9,
                309,
                fill=color,
                outline="",
                tags="ui",
            )
            self.canvas.create_line(
                x + 1,
                309 - height,
                x + 8,
                309 - height,
                fill="#d5fbff",
                width=1,
                tags="ui",
            )
        self.canvas.create_text(
            108,
            325,
            anchor="w",
            text="125 Hz",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            234,
            325,
            text="1 kHz",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            360,
            325,
            anchor="e",
            text="8 kHz",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            352,
            text="SELECTED-BIN SPECTRUM · CANONICAL SNAPSHOT",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▥", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def _show_timeout_settings(
        self,
        screen: str,
        section: str,
        description: str,
        rows: tuple[tuple[str, str], tuple[str, str]],
    ) -> None:
        self.screen = screen
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        y_offset = self._wide_page_offset_y()
        self._draw_page_heading(
            "SETTINGS",
            "Idle policy · swipe to scroll",
            y_offset,
        )
        self.canvas.create_text(
            94,
            174 + y_offset,
            anchor="w",
            text=section,
            fill=STATUS_READY,
            font=(FONT_DISPLAY, 14),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            196 + y_offset,
            anchor="w",
            text=description,
            width=282,
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 10),
            tags="ui",
        )
        for label_y, control_y, (label, value) in zip(
            (216 + y_offset, 286 + y_offset),
            (230 + y_offset, 300 + y_offset),
            rows,
        ):
            self.canvas.create_text(
                94,
                label_y,
                anchor="w",
                text=label,
                fill=TEXT,
                font=(FONT_DISPLAY, 11),
                tags="ui",
            )
            self._draw_settings_control(control_y, value)

        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "⚙", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_chrome_settings_demo(self) -> None:
        self._show_timeout_settings(
            "chrome-settings",
            "ADAPTIVE CHROME",
            "Touch or motion wakes",
            (
                ("Hide Command Arc after", "1 minute"),
                ("Hide Status Arc after", "2 minutes"),
            ),
        )

    def show_power_settings_demo(self) -> None:
        self._show_timeout_settings(
            "power-settings",
            "POWER",
            "Dim, then screen sleep",
            (
                ("Dim display to 50% after", "5 minutes"),
                ("Full sleep after display dim", "5 minutes"),
            ),
        )

    def show_audio_detail_demo(self) -> None:
        self.screen = "fft-detail"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO DETAIL",
            "Two-channel spectrum"
            if self.performance_mode
            else "Mic 1 + Mic 2 · 16 DFT bands",
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "AUDIO DETAIL",
                y_offset=y_offset,
            )
            return
        self.canvas.create_text(
            104,
            179,
            anchor="w",
            text="MIC 1  ·  BLUE → MAGENTA",
            fill="#78cfff",
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self.canvas.create_text(
            104,
            265,
            anchor="w",
            text="MIC 2  ·  ORANGE → GREEN",
            fill="#ffca70",
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        mic1_colors = (
            "#078dff", "#0b9dff", "#10afff", "#12c5ff",
            "#16d9ff", "#28e4f2", "#45d9ee", "#5abcfb",
            "#709dff", "#8780ff", "#a466ff", "#bd51ff",
            "#d342ff", "#e834f8", "#f62de7", "#ff35cf",
        )
        mic2_colors = (
            "#ff8700", "#ff9500", "#ffa400", "#ffb300",
            "#ffc000", "#ffcb19", "#ffd43b", "#f4dd4b",
            "#e6e45a", "#d4e660", "#bfe365", "#a8df6b",
            "#8fdb77", "#72d98b", "#4dd6a3", "#25d3bb",
        )
        mic1_heights = (16, 28, 22, 39, 31, 48, 42, 27, 35, 43, 30, 52, 45, 34, 25, 38)
        mic2_heights = (43, 34, 51, 28, 39, 47, 32, 55, 45, 37, 29, 41, 48, 35, 24, 31)
        for baseline, colors, heights in (
            (250, mic1_colors, mic1_heights),
            (336, mic2_colors, mic2_heights),
        ):
            for index, height in enumerate(heights):
                x = 108 + index * 15
                self.canvas.create_rectangle(
                    x,
                    baseline - height,
                    x + 9,
                    baseline,
                    fill=colors[index],
                    outline="",
                    tags="ui",
                )
                self.canvas.create_line(
                    x + 1,
                    baseline - height,
                    x + 8,
                    baseline - height,
                    fill="#f3ffff",
                    width=1,
                    tags="ui",
                )
        self.canvas.create_text(
            233,
            370,
            text="LIVE DETAIL · TWO MICS",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 9),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_imu_demo(self) -> None:
        self.screen = "imu"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "IMU LEVEL",
            "QMI8658 diagnostics"
            if self.performance_mode
            else "QMI8658 · synthetic attitude",
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "IMU SAMPLING",
                y_offset=y_offset,
            )
            return
        self.canvas.create_text(
            126,
            180,
            anchor="w",
            text="ROLL",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            126,
            195,
            anchor="w",
            text="+0.1°",
            fill="#61d8ff",
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            340,
            180,
            anchor="e",
            text="PITCH",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            340,
            195,
            anchor="e",
            text="-0.3°",
            fill="#ff9d5c",
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        center_x = 233
        center_y = 261
        radius = 58
        self._draw_glow_disc(center_x, center_y, radius, "#0a8fc7", 42)
        self.canvas.create_oval(
            center_x - radius,
            center_y - radius,
            center_x + radius,
            center_y + radius,
            outline="#2d708c",
            width=1,
            fill="#081821",
            tags="ui",
        )
        for angle in range(0, 360, 45):
            radians = math.radians(angle)
            inner = radius - 6
            outer = radius
            self.canvas.create_line(
                center_x + (math.cos(radians) * inner),
                center_y + (math.sin(radians) * inner),
                center_x + (math.cos(radians) * outer),
                center_y + (math.sin(radians) * outer),
                fill="#5f91a5",
                width=1,
                tags="ui",
            )
        self.canvas.create_line(
            center_x - 48,
            center_y,
            center_x + 48,
            center_y,
            fill="#294957",
            width=1,
            tags="ui",
        )
        self.canvas.create_line(
            center_x,
            center_y - 48,
            center_x,
            center_y + 48,
            fill="#294957",
            width=1,
            tags="ui",
        )
        self.canvas.create_line(
            center_x - 46,
            center_y + 3,
            center_x + 46,
            center_y - 3,
            fill=STATUS_READY,
            width=2,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self._draw_glow_disc(center_x + 5, center_y - 7, 15, "#5be4ff", 100)
        self.canvas.create_oval(
            center_x - 9,
            center_y - 21,
            center_x + 19,
            center_y + 7,
            fill="#5be4ff",
            outline="#d8faff",
            width=1,
            tags="ui",
        )
        self.canvas.create_text(
            72,
            356,
            text="ACC   X +0.02   Y -0.04   Z +9.79 m/s²",
            anchor="w",
            fill="#61d8ff",
            font=(FONT_TECH, 9),
            tags="ui",
        )
        self.canvas.create_text(
            72,
            374,
            text="GYRO  X +0.10   Y -0.03   Z +0.02 dps",
            anchor="w",
            fill="#ff9d5c",
            font=(FONT_TECH, 9),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            394,
            text="CALIBRATED  |  COMPASS UNAVAILABLE",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        self._draw_edge_back()
        self._draw_safe_guide()

    def _animate_audio(self) -> None:
        if self.screen != "audio" or self.idle_state in {"off", "standby"}:
            return
        self.audio_phase += 1
        self.canvas.delete("audio")
        y_offset = self._fixed_canvas_offset_y()
        self._draw_audio_bars(
            239 + y_offset,
            43,
            "#27bdf2",
            "#c168ff",
            0,
        )
        self._draw_audio_bars(
            313 + y_offset,
            43,
            "#ffad3d",
            "#77e37c",
            9,
        )
        self.audio_timer = self.root.after(70, self._animate_audio)

    def show_confirmation(self) -> None:
        if self.screen == "boot":
            return
        self.screen = "confirmation"
        self.logger.info("qsee_nav", "Synthetic confirmation screen presented")
        self._draw_shell(
            "Confirm action?",
            "The side margins are reserved for deterministic Cancel and OK actions.",
            primary=False,
            actions=True,
        )

    def confirm(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("qsee_nav", "Synthetic confirmation accepted")
        self.screen = "result"
        self._draw_shell("QSee.ai", "Confirmed", primary=True, actions=False)

    def cancel(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("qsee_nav", "Synthetic confirmation cancelled")
        self.show_home()

    def back(self) -> None:
        if self.screen in {
            "audio",
            "fft",
            "fft-detail",
            "imu",
            "settings",
            "storage",
            "chrome-settings",
            "power-settings",
            "hardware",
        }:
            if self.audio_timer is not None:
                self.root.after_cancel(self.audio_timer)
                self.audio_timer = None
            self.logger.info("qsee_ui", "Diagnostic page closed")
            if self.screen == "storage":
                self.show_settings_demo()
            else:
                self.show_home()
        elif self.screen in {"confirmation", "result"}:
            self.logger.info("qsee_nav", "Synthetic back navigation")
            self.show_home()

    def reboot(self) -> None:
        self.idle_state = "active"
        self.ready = False
        self.timeline_index = 0
        self.logger = CanonicalLogger()
        self.show_boot()
        self.root.after(150, self._advance_timeline)

    def toggle_fullscreen(self) -> None:
        if self.screen == "boot":
            return
        if self.idle_state == "standby":
            self.logger.info(
                "qsee_power_manager",
                "Synthetic BOOT ignored in standby; use PWR or IMU wake",
            )
            return
        if self.idle_state == "fullscreen":
            self.idle_state = "active"
            self.logger.info(
                "qsee_power_manager",
                "Synthetic BOOT exited Fullscreen; Adaptive Chrome visible",
            )
        elif self.idle_state == "active":
            self.idle_state = "fullscreen"
            self.logger.info(
                "qsee_power_manager",
                "Synthetic BOOT entered Fullscreen; Adaptive Chrome locked hidden",
            )
        else:
            previous_state = self.idle_state
            self.idle_state = "active"
            self.logger.info(
                "qsee_power_manager",
                f"Synthetic BOOT woke {previous_state}; press again for Fullscreen",
            )
        self._redraw_current_screen()

    def toggle_standby(self) -> None:
        if self.screen == "boot":
            return
        if self.idle_state == "standby":
            self.idle_state = "active"
            self.logger.info(
                "qsee_power_manager",
                "Synthetic PWR wake completed",
            )
        else:
            fullscreen_cancelled = self.idle_state == "fullscreen"
            self.idle_state = "standby"
            self.logger.info(
                "qsee_power_manager",
                "Synthetic PWR standby entered; audio suspended, IMU active"
                + ("; Fullscreen cancelled" if fullscreen_cancelled else ""),
            )
        self._redraw_current_screen()

    def toggle_safe_guide(self) -> None:
        self.safe_guide = not self.safe_guide
        if self.screen == "boot":
            self.show_boot()
        else:
            self._redraw_current_screen()

    def _redraw_current_screen(self) -> None:
        current_screen = self.screen
        if self.audio_timer is not None:
            self.root.after_cancel(self.audio_timer)
            self.audio_timer = None
        redraw = {
            "home": self.show_home,
            "settings": self.show_settings_demo,
            "storage": self.show_storage_demo,
            "chrome-settings": self.show_chrome_settings_demo,
            "power-settings": self.show_power_settings_demo,
            "hardware": self.show_hardware_demo,
            "audio": self.show_audio_demo,
            "fft": self.show_audio_fft_demo,
            "fft-detail": self.show_audio_detail_demo,
            "imu": self.show_imu_demo,
            "confirmation": self.show_confirmation,
        }.get(current_screen)
        if redraw is not None:
            redraw()
        elif current_screen == "result":
            self._draw_shell("QSee.ai", "Confirmed", primary=True, actions=False)
            self.screen = "result"
        else:
            self.show_home()
        self.apply_idle_overlay()

    def apply_idle_overlay(self) -> None:
        if self.idle_state == "dimmed":
            self.canvas.create_rectangle(
                0,
                0,
                DISPLAY_SIZE,
                DISPLAY_SIZE,
                fill="#000000",
                stipple="gray50",
                outline="",
                tags="idle_overlay",
            )
        elif self.idle_state in {"off", "standby"}:
            self.canvas.create_rectangle(
                0,
                0,
                DISPLAY_SIZE,
                DISPLAY_SIZE,
                fill="#000000",
                outline="",
                tags="idle_overlay",
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=SCENARIOS, default="healthy")
    parser.add_argument(
        "--screen",
        choices=(
            "home",
            "settings",
            "storage",
            "chrome-settings",
            "power-settings",
            "hardware",
            "audio",
            "fft",
            "fft-detail",
            "imu",
        ),
        default="home",
    )
    parser.add_argument(
        "--idle-state",
        choices=(
            "active",
            "command-hidden",
            "chrome-hidden",
            "fullscreen",
            "dimmed",
            "off",
            "standby",
        ),
        default="active",
    )
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--safe-guide", action="store_true")
    parser.add_argument("--no-sound", action="store_true")
    parser.add_argument("--performance-mode", action="store_true")
    args = parser.parse_args()

    if sys.platform == "win32":
        ctypes.windll.shcore.SetProcessDpiAwareness(2)

    root = tk.Tk()
    root.title(f"QSee.ai Device Simulator · {SCENARIOS[args.scenario].name}")
    root.resizable(False, False)
    root.configure(background="#101820")
    simulator = QSeeSimulator(
        root,
        SCENARIOS[args.scenario],
        play_sound=not args.no_sound and args.capture is None,
        idle_state=args.idle_state,
    )
    simulator.performance_mode = args.performance_mode
    simulator.safe_guide = args.safe_guide
    if args.capture is not None:
        from PIL import ImageGrab

        simulator.ready = True
        {
            "home": simulator.show_home,
            "settings": simulator.show_settings_demo,
            "storage": simulator.show_storage_demo,
            "chrome-settings": simulator.show_chrome_settings_demo,
            "power-settings": simulator.show_power_settings_demo,
            "hardware": simulator.show_hardware_demo,
            "audio": simulator.show_audio_demo,
            "fft": simulator.show_audio_fft_demo,
            "fft-detail": simulator.show_audio_detail_demo,
            "imu": simulator.show_imu_demo,
        }[args.screen]()
        simulator.apply_idle_overlay()
        root.geometry("+100+100")
        root.deiconify()
        root.attributes("-topmost", True)
        root.lift()
        root.focus_force()
        root.update_idletasks()
        root.update()
        time.sleep(0.25)
        root.update()
        args.capture.parent.mkdir(parents=True, exist_ok=True)
        x = simulator.canvas.winfo_rootx()
        y = simulator.canvas.winfo_rooty()
        width = simulator.canvas.winfo_width()
        height = simulator.canvas.winfo_height()
        if sys.platform == "win32":
            # Capture the simulator window directly so validation also works
            # when the desktop session is occluded or not foreground-visible.
            captured = ImageGrab.grab(
                window=simulator.canvas.winfo_id(),
                include_layered_windows=True,
            )
        else:
            captured = ImageGrab.grab(
                bbox=(x, y, x + width, y + height),
                include_layered_windows=True,
            )
        captured.save(args.capture)
        root.destroy()
        return
    root.mainloop()


if __name__ == "__main__":
    main()
