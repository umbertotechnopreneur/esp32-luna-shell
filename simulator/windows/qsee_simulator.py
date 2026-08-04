"""QSee.ai round-device UI simulator for Windows.

The simulator mirrors the firmware flow without pretending to emulate physical
drivers. Mouse clicks stand in for touch and deterministic scenarios provide
synthetic PMU, IMU, TOF, GPS, Wi-Fi, BLE, SD, and logger data.
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
    {"audio", "fft", "fft-detail", "imu", "gps", "tof"}
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
    battery_percent: int | None = 83
    battery_mv: int | None = 4021
    usb_present: bool | None = False
    usb_mv: int | None = 0
    system_mv: int | None = 3708
    die_temperature_c: float | None = 31.6
    battery_present: bool | None = True
    battery_state: str = "DISCHARGING"
    battery_health: str = "READY"
    battery_error: str = "ESP_OK"
    battery_sample_age_s: float | None = 0.4
    charger_phase: str | None = "STOPPED"
    batfet_enabled: bool | None = True
    battery_active_mode: bool | None = False
    thermal_regulation_active: bool | None = False
    input_current_limit_active: bool | None = False


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
    "battery-low": Scenario(
        "BATTERY LOW",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Ready with synthetic low-battery warning",
        battery_percent=8,
        battery_mv=3475,
        system_mv=3628,
        die_temperature_c=32.4,
    ),
    "battery-charging": Scenario(
        "BATTERY CHARGING",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Synthetic hardware ready; USB charging",
        battery_percent=54,
        battery_mv=3860,
        usb_present=True,
        usb_mv=5052,
        system_mv=4215,
        die_temperature_c=33.1,
        battery_state="CHARGING",
        charger_phase="CC CHARGE",
    ),
    "battery-unavailable": Scenario(
        "BATTERY UNAVAILABLE",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Ready with synthetic PMIC telemetry unavailable",
        battery_percent=None,
        battery_mv=None,
        usb_present=None,
        usb_mv=None,
        system_mv=None,
        die_temperature_c=None,
        battery_present=None,
        battery_state="UNAVAILABLE",
        battery_health="ERROR",
        battery_error="ESP_ERR_TIMEOUT",
        battery_sample_age_s=None,
        charger_phase=None,
        batfet_enabled=None,
        battery_active_mode=None,
        thermal_regulation_active=None,
        input_current_limit_active=None,
    ),
}


@dataclass(frozen=True)
class PairingPreview:
    title: str
    step: str
    message: str
    detail: str
    progress: int
    show_qr: bool = False
    result: str = "normal"


PAIRING_PREVIEWS = {
    "pairing-intro": PairingPreview(
        "PAIR DEVICE",
        "STEP 1 OF 5",
        "Connect QSee to your Wi-Fi network.",
        "Bluetooth carries the Wi-Fi setup details.",
        1,
    ),
    "pairing-bluetooth": PairingPreview(
        "BLUETOOTH",
        "STEP 2 OF 5",
        "Turn on Bluetooth on your phone.",
        "Allow Nearby devices when prompted.",
        2,
    ),
    "pairing-app": PairingPreview(
        "GET THE APP",
        "STEP 3 OF 5",
        "Install ESP BLE Provisioning on your phone.",
        "Next shows the official Google Play download.",
        3,
    ),
    "pairing-app-qr": PairingPreview(
        "INSTALL APP",
        "STEP 4 OF 5",
        "Scan to install ESP BLE Provisioning.",
        "Then return here and press Next.",
        4,
        show_qr=True,
    ),
    "pairing-device-qr": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "In the app, scan this device QR.",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-credentials": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "Wi-Fi details received. Preparing the connection...",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-connecting": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "Wi-Fi details received. Connecting...",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-success": PairingPreview(
        "WI-FI READY",
        "CONNECTED",
        "Network: QSEE-SYNTHETIC\nIP: 192.0.2.24",
        "Setup complete. Next returns to Settings.",
        5,
        result="success",
    ),
    "pairing-failed": PairingPreview(
        "SETUP FAILED",
        "ACTION REQUIRED",
        "Wi-Fi password was rejected. Check it and try again.",
        "Press Next to try again.",
        0,
        result="failed",
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
        # The simulator uses pixel-based firmware geometry, so keep Tk font
        # sizing deterministic instead of inheriting Windows display scaling.
        self.root.tk.call("tk", "scaling", 1.0)
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
        self.wireless_running = False
        self.battery_return_screen = "home"
        self.tof_settings_return_screen = "tof"
        self.tof_calibration_complete = False
        self.ready = False
        self.safe_guide = False
        self.timeline_index = 0
        self.boot_image: tk.PhotoImage | None = None
        self.atmosphere_image = self._build_atmosphere()
        self.button_images = {
            (role, diameter): self._build_command_button(role, diameter)
            for role in ("primary", "success", "warning", "danger", "neutral")
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
                "profile": 170,
                "performance": 194,
                "diagnostics": 236,
                "diagnostics_subtitle": None,
                "cadence": 258,
                "cadence_control": 272,
                "cadence_state": 324,
                "gps": 356,
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
                "gps": 423,
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
            "gps": 430,
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
        role: str,
        circle_size: int,
    ) -> ImageTk.PhotoImage:
        scale = 4
        sprite_size = circle_size + 24
        pad = (sprite_size - circle_size) // 2
        top, bottom, border, glow = {
            "primary": (
                PRIMARY_TOP,
                PRIMARY_BOTTOM,
                PRIMARY_BORDER,
                PRIMARY_GLOW,
            ),
            "success": (
                SUCCESS_TOP,
                SUCCESS_BOTTOM,
                SUCCESS_BORDER,
                SUCCESS_GLOW,
            ),
            "warning": (
                WARNING_TOP,
                WARNING_BOTTOM,
                WARNING_BORDER,
                WARNING_GLOW,
            ),
            "danger": (
                DANGER_TOP,
                DANGER_BOTTOM,
                DANGER_BORDER,
                DANGER_GLOW,
            ),
            "neutral": (
                NEUTRAL_TOP,
                NEUTRAL_BOTTOM,
                NEUTRAL_BORDER,
                NEUTRAL_GLOW,
            ),
        }[role]

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
        if self.screen == "pairing-success":
            return [STATUS_READY] * 6
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
        colors = [
            STATUS_READY,
            STATUS_READY,
            STATUS_READY,
            STATUS_WARNING,
            STATUS_READY,
            STATUS_READY,
        ]
        if self.scenario.battery_percent is None:
            colors[1] = STATUS_FAULT
        elif self.scenario.battery_percent <= 12:
            colors[1] = STATUS_FAULT
        elif self.scenario.battery_percent <= 30:
            colors[1] = STATUS_WARNING
        return colors

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
            if self.scenario.battery_percent is not None:
                fill_width = round(13 * self.scenario.battery_percent / 100)
                if fill_width > 0:
                    self.canvas.create_rectangle(
                        x - 8,
                        y - 5,
                        x - 8 + fill_width,
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
        role: str | bool,
        diameter: int = EDGE_COMMAND_DIAMETER,
    ) -> None:
        if isinstance(role, bool):
            role = "success" if role else "primary"
        icon_x, icon_y = {
            "➤": (1, 0),
            "i": (0, -1),
            "‹": (0, -2),
        }.get(icon, (0, 0))
        self.canvas.create_image(
            x + (diameter / 2),
            y + (diameter / 2),
            image=self.button_images[(role, diameter)],
            tags="ui",
        )
        if icon == "battery":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_rectangle(
                center_x - 12,
                center_y - 8,
                center_x + 9,
                center_y + 8,
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_rectangle(
                center_x + 10,
                center_y - 4,
                center_x + 13,
                center_y + 4,
                fill="#ffffff",
                outline="#ffffff",
                tags="ui",
            )
            self.canvas.create_rectangle(
                center_x - 9,
                center_y - 5,
                center_x + 3,
                center_y + 5,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            return
        if icon == "wifi":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2) + 4
            for radius in (5, 10, 15):
                self.canvas.create_arc(
                    center_x - radius,
                    center_y - radius,
                    center_x + radius,
                    center_y + radius,
                    start=35,
                    extent=110,
                    style=tk.ARC,
                    outline="#ffffff",
                    width=2,
                    tags="ui",
                )
            self.canvas.create_oval(
                center_x - 2,
                center_y - 2,
                center_x + 2,
                center_y + 2,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            return
        if icon == "power":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_arc(
                center_x - 12,
                center_y - 12,
                center_x + 12,
                center_y + 12,
                start=40,
                extent=280,
                style=tk.ARC,
                outline="#ffffff",
                width=3,
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y - 14,
                center_x,
                center_y,
                fill="#ffffff",
                width=3,
                capstyle=tk.ROUND,
                tags="ui",
            )
            return
        if icon == "gps":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_oval(
                center_x - 10,
                center_y - 10,
                center_x + 10,
                center_y + 10,
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_oval(
                center_x - 3,
                center_y - 3,
                center_x + 3,
                center_y + 3,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y - 15,
                center_x,
                center_y - 8,
                fill="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y + 8,
                center_x,
                center_y + 15,
                fill="#ffffff",
                width=2,
                tags="ui",
            )
            return
        if icon == "sd":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_polygon(
                center_x - 10,
                center_y - 14,
                center_x + 5,
                center_y - 14,
                center_x + 11,
                center_y - 8,
                center_x + 11,
                center_y + 14,
                center_x - 10,
                center_y + 14,
                fill="",
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            for offset in (-5, 0, 5):
                self.canvas.create_line(
                    center_x + offset,
                    center_y - 11,
                    center_x + offset,
                    center_y - 6,
                    fill="#ffffff",
                    width=2,
                    tags="ui",
                )
            return
        if icon in {"‹", "›"}:
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            direction = -1 if icon == "‹" else 1
            tip_x = center_x + (direction * 6)
            tail_x = center_x - (direction * 5)
            self.canvas.create_line(
                tail_x,
                center_y - 10,
                tip_x,
                center_y,
                tail_x,
                center_y + 10,
                fill="#ffffff",
                width=3,
                capstyle=tk.ROUND,
                joinstyle=tk.ROUND,
                tags="ui",
            )
            return
        if icon in {"fft", "dual"}:
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            channel_offsets = (-5, 5) if icon == "dual" else (0,)
            for channel_offset in channel_offsets:
                for index, height in enumerate((7, 14, 10)):
                    bar_x = center_x + channel_offset + ((index - 1) * 4)
                    self.canvas.create_line(
                        bar_x,
                        center_y + (height / 2),
                        bar_x,
                        center_y - (height / 2),
                        fill="#ffffff",
                        width=2,
                        capstyle=tk.ROUND,
                        tags="ui",
                    )
            return
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
            ("battery", False),
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

    def _draw_edge_next(self) -> None:
        self._draw_command_button(396, 203, "›", True)

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

    def _battery_timeline_message(self) -> str:
        if self.scenario.battery_percent is None:
            return (
                "PMU AXP2101: synthetic telemetry unavailable, "
                f"{self.scenario.battery_error}"
            )
        source = (
            f"USB {self.scenario.usb_mv}mV"
            if self.scenario.usb_present
            else "USB disconnected"
        )
        return (
            "PMU AXP2101: synthetic battery "
            f"{self.scenario.battery_percent}%, "
            f"{self.scenario.battery_state.lower()}, "
            f"{self.scenario.battery_mv}mV, {source}"
        )

    # Replay deterministic hardware events without emulating device drivers.
    def _advance_timeline(self) -> None:
        events = [
            ("qsee_logger", "Canonical logger online: console + RAM ring"),
            ("qsee_main", self.scenario.sd_message),
            ("qsee_boot", "Boot screen online: QSee.ai + canonical live log"),
            ("qsee_main", "Touch CST9217: synthetic input ready"),
            ("qsee_hw", self._battery_timeline_message()),
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
            and self.screen in PAIRING_SCREENS
        ):
            self.back()
        elif (
            396 <= event.x <= 456
            and 203 <= event.y <= 263
            and self.screen in PAIRING_SCREENS
            and self.screen not in {
                "pairing-device-qr",
                "pairing-credentials",
                "pairing-connecting",
            }
        ):
            self.pairing_next()
        elif (
            10 <= event.x <= 70
            and 203 <= event.y <= 263
            and self.screen in {
                "audio",
                "fft",
                "fft-detail",
                "imu",
                "gps",
                "tof",
                "tof-settings",
                "battery",
                "logs",
                "settings",
                "storage",
                "chrome-settings",
                "power-settings",
                "hardware",
            }
        ):
            self.back()
        elif command_arc_visible and self.screen == "home":
            for index in range(5):
                x, y, diameter = command_geometry(5, index)
                if not (
                    x <= event.x <= x + diameter
                    and y <= event.y <= y + diameter
                ):
                    continue
                if index == 0:
                    self.show_battery_demo(return_screen="home")
                elif index == 1:
                    self.show_audio_demo()
                elif index == 2:
                    self.show_hardware_demo()
                elif index == 3:
                    self.show_imu_demo()
                else:
                    self.show_settings_demo()
                return
        elif command_arc_visible and self.screen == "audio":
            for index in range(2):
                x, y, diameter = command_geometry(2, index)
                if x <= event.x <= x + diameter and y <= event.y <= y + diameter:
                    if index == 0:
                        self.show_audio_fft_demo()
                    else:
                        self.show_audio_detail_demo()
                    return
        elif (
            command_arc_visible
            and 204 <= event.x <= 262
            and 360 <= event.y <= 418
            and self.screen == "fft"
        ):
            self.show_audio_detail_demo()
        elif self.screen == "hardware":
            battery_row_y = 173 + self._wide_page_offset_y()
            tof_row_y = 173 + self._wide_page_offset_y() + (5 * 42)
            if (
                88 <= event.x <= 378
                and battery_row_y - 12 <= event.y <= battery_row_y + 30
            ):
                self.logger.info(
                    "qsee_nav",
                    "Synthetic AXP2101 hardware row selected",
                )
                self.show_battery_demo(return_screen="hardware")
            elif (
                88 <= event.x <= 378
                and tof_row_y - 12 <= event.y <= tof_row_y + 30
            ):
                self.logger.info(
                    "qsee_nav",
                    "Synthetic TOF hardware row selected",
                )
                self.show_tof_demo()
        elif self.screen == "tof" and command_arc_visible:
            x, y, diameter = command_geometry(1, 0)
            if x <= event.x <= x + diameter and y <= event.y <= y + diameter:
                self.logger.info(
                    "qsee_nav",
                    f"Synthetic {self.screen.upper()} settings gear selected",
                )
                self.show_tof_settings_demo(return_screen=self.screen)
        elif self.screen == "tof-settings" and command_arc_visible:
            x, y, diameter = command_geometry(1, 0)
            if x <= event.x <= x + diameter and y <= event.y <= y + diameter:
                self.tof_calibration_complete = True
                self.logger.info(
                    "qsee_tof",
                    "Synthetic calibration completed; no hardware accessed",
                )
                self.show_tof_settings_demo()
        elif self.screen == "settings":
            settings_layout = self._settings_layout()
            performance_y = int(settings_layout["performance"])
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
                for index in range(5):
                    x, y, diameter = command_geometry(5, index)
                    if (
                        x <= event.x <= x + diameter
                        and y <= event.y <= y + diameter
                    ):
                        if index == 0:
                            self.show_logs_demo()
                        elif index == 1:
                            self.show_pairing_demo("pairing-intro")
                        elif index == 2:
                            self.wireless_running = not self.wireless_running
                            self.logger.info(
                                "qsee_radios",
                                "Synthetic wireless services "
                                + (
                                    "started"
                                    if self.wireless_running
                                    else "stopped"
                                )
                                + "; no hardware accessed",
                            )
                            self.show_settings_demo()
                        elif index == 3:
                            self.logger.info(
                                "qsee_nav",
                                "Synthetic Settings GPS command selected",
                            )
                            self.show_gps_demo()
                        else:
                            self.logger.info(
                                "qsee_nav",
                                "Synthetic Settings SD command selected",
                            )
                            self.show_storage_demo()
                        return
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
        *,
        title_size: int = 21,
        subtitle_baseline: int = 136,
        divider_y: int = 154,
    ) -> None:
        if y_offset is None:
            y_offset = self._content_offset_y()
        self.canvas.create_text(
            94,
            110 + y_offset,
            anchor="w",
            text=title,
            fill=TEXT,
            font=(FONT_DISPLAY, title_size),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            subtitle_baseline + y_offset,
            anchor="w",
            text=subtitle,
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 12),
            tags="ui",
        )
        self.canvas.create_line(
            94,
            divider_y + y_offset,
            126,
            divider_y + y_offset,
            fill=STATUS_READY,
            width=2,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self.canvas.create_line(
            134,
            divider_y + y_offset,
            372,
            divider_y + y_offset,
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

    def _battery_level_color(self) -> str:
        level = self.scenario.battery_percent
        if level is None or self.scenario.battery_health != "READY":
            return STATUS_FAULT
        if level <= 12:
            return STATUS_FAULT
        if level <= 30:
            return STATUS_WARNING
        return STATUS_READY

    def _draw_battery_metric(
        self,
        y: int,
        label: str,
        value: str,
        color: str,
    ) -> None:
        self.canvas.create_text(
            94,
            y,
            anchor="w",
            text=f"SYN | {label}",
            fill=MUTED_BRIGHT,
            font=(FONT_DISPLAY, 8),
            tags="ui",
        )
        self.canvas.create_text(
            372,
            y,
            anchor="e",
            text=value,
            fill=color,
            font=(FONT_TECH, 9, "bold"),
            tags="ui",
        )
        self.canvas.create_line(
            94,
            y + 9,
            372,
            y + 9,
            fill="#173545",
            width=1,
            tags="ui",
        )

    def show_battery_demo(self, return_screen: str | None = None) -> None:
        if return_screen is not None:
            self.battery_return_screen = return_screen
        self.screen = "battery"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        self._draw_page_heading(
            "BATTERY",
            "AXP2101 | SYNTHETIC",
            self._wide_page_offset_y(),
        )

        level = self.scenario.battery_percent
        level_color = self._battery_level_color()
        telemetry_valid = (
            level is not None
            and self.scenario.battery_mv is not None
            and self.scenario.battery_present is True
            and self.scenario.battery_health == "READY"
        )
        charging = telemetry_valid and self.scenario.battery_state == "CHARGING"

        body_x = 112
        body_y = 164
        body_width = 230
        body_height = 126
        self._draw_panel(
            body_x,
            body_y,
            body_width,
            body_height,
            top="#0d2735",
            bottom="#071018",
            border=level_color,
            glow=level_color,
            glow_alpha=52,
            radius=18,
        )
        self.canvas.create_rectangle(
            body_x + body_width,
            body_y + 37,
            body_x + body_width + 18,
            body_y + 91,
            fill=level_color,
            outline=level_color,
            width=1,
            tags="ui",
        )

        if not telemetry_valid:
            state_text = "TELEMETRY UNAVAILABLE"
            health_text = (
                f"SYNTHETIC | {self.scenario.battery_health} | "
                f"{self.scenario.battery_error}"
            )
        elif charging:
            state_text = "CHARGING | USB CONNECTED"
            health_text = "SYNTHETIC | PMIC READY | CELL PRESENT"
        elif self.scenario.battery_state == "DISCHARGING":
            state_text = "DISCHARGING | ON BATTERY"
            health_text = "SYNTHETIC | PMIC READY | CELL PRESENT"
        else:
            state_text = f"{self.scenario.battery_state} | POWER IDLE"
            health_text = "SYNTHETIC | PMIC READY | CELL PRESENT"

        self.canvas.create_text(
            227,
            body_y + 19,
            text=state_text,
            fill=level_color,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            227,
            body_y + 47,
            text=f"{level}%" if telemetry_valid else "--",
            fill=TEXT if telemetry_valid else MUTED,
            font=(FONT_DISPLAY, 38),
            tags="ui",
        )
        self.canvas.create_text(
            227,
            body_y + 78,
            text="SYNTHETIC PMIC LEVEL",
            fill="#80dfff" if telemetry_valid else MUTED,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        if charging:
            self.canvas.create_text(
                310,
                body_y + 54,
                text="⚡",
                fill="#f4ffff",
                font=("Segoe UI Symbol", 19, "bold"),
                tags="ui",
            )

        progress_x = body_x + 18
        progress_y = body_y + 88
        progress_width = 194
        self.canvas.create_rectangle(
            progress_x,
            progress_y,
            progress_x + progress_width,
            progress_y + 18,
            fill=TRACK,
            outline="#3f5869",
            width=1,
            tags="ui",
        )
        if telemetry_valid:
            fill_width = round(progress_width * level / 100)
            if fill_width > 0:
                self.canvas.create_rectangle(
                    progress_x + 2,
                    progress_y + 2,
                    progress_x + max(2, fill_width - 1),
                    progress_y + 16,
                    fill=level_color,
                    outline="",
                    tags="ui",
                )

        band_y = body_y + 114
        critical_width = round(progress_width * 0.12)
        warning_width = round(progress_width * 0.18)
        self.canvas.create_rectangle(
            progress_x,
            band_y,
            progress_x + critical_width,
            band_y + 5,
            fill=STATUS_FAULT,
            outline="",
            tags="ui",
        )
        self.canvas.create_rectangle(
            progress_x + critical_width + 1,
            band_y,
            progress_x + critical_width + warning_width,
            band_y + 5,
            fill=STATUS_WARNING,
            outline="",
            tags="ui",
        )
        self.canvas.create_rectangle(
            progress_x + critical_width + warning_width + 1,
            band_y,
            progress_x + progress_width,
            band_y + 5,
            fill=STATUS_READY,
            outline="",
            tags="ui",
        )

        self.canvas.create_text(
            233,
            301,
            text=health_text,
            fill=level_color,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )

        unavailable = "UNAVAILABLE"
        cell_value = (
            f"{self.scenario.battery_mv} mV"
            if telemetry_valid
            else unavailable
        )
        if self.scenario.usb_present is True and self.scenario.usb_mv is not None:
            usb_value = f"ON | {self.scenario.usb_mv} mV"
            usb_color = STATUS_READY
        elif self.scenario.usb_present is False:
            usb_value = "DISCONNECTED"
            usb_color = MUTED_BRIGHT
        else:
            usb_value = unavailable
            usb_color = STATUS_FAULT
        system_value = (
            f"{self.scenario.system_mv} mV"
            if self.scenario.system_mv is not None
            else unavailable
        )
        temperature_value = (
            f"{self.scenario.die_temperature_c:.1f} C"
            if self.scenario.die_temperature_c is not None
            else unavailable
        )
        policy_value = (
            f"4.20 V | 400 mA | {self.scenario.charger_phase}"
            if self.scenario.battery_health == "READY"
            else "UNCONFIRMED"
        )
        flags_ok = (
            self.scenario.batfet_enabled is True
            and self.scenario.battery_active_mode is False
            and self.scenario.thermal_regulation_active is False
            and self.scenario.input_current_limit_active is False
        )
        sample_value = (
            f"FRESH {self.scenario.battery_sample_age_s:.1f}s | "
            f"BATFET {'ON' if self.scenario.batfet_enabled else 'OFF'} | "
            f"{'FLAGS OK' if flags_ok else 'FLAGS ACTIVE'}"
            if self.scenario.battery_sample_age_s is not None
            else "NO SAMPLE"
        )
        metrics = (
            (318, "CELL", cell_value, level_color),
            (334, "USB", usb_value, usb_color),
            (
                350,
                "SYSTEM",
                system_value,
                STATUS_READY if self.scenario.system_mv is not None else STATUS_FAULT,
            ),
            (
                366,
                "PMIC TEMP",
                temperature_value,
                STATUS_READY
                if self.scenario.die_temperature_c is not None
                else STATUS_FAULT,
            ),
            (
                382,
                "POLICY",
                policy_value,
                STATUS_READY
                if self.scenario.battery_health == "READY"
                else STATUS_FAULT,
            ),
            (
                398,
                "SAMPLE",
                sample_value,
                STATUS_READY
                if self.scenario.battery_sample_age_s is not None
                else STATUS_FAULT,
            ),
        )
        for y, label, value, color in metrics:
            self._draw_battery_metric(y, label, value, color)

        self._draw_edge_back()
        self._draw_safe_guide()

    def show_logs_demo(self) -> None:
        self.screen = "logs"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        self._draw_page_heading(
            "LOGS",
            "SYNTHETIC · RAM RING",
            self._wide_page_offset_y(),
        )
        self._draw_panel(
            92,
            166,
            282,
            226,
            top="#0b2230",
            bottom="#061018",
            border="#24536a",
            glow=STATUS_GLOW,
            glow_alpha=28,
            radius=12,
        )
        battery_log = (
            f"SYN PMIC {self.scenario.battery_percent}% "
            f"{self.scenario.battery_state}"
            if self.scenario.battery_percent is not None
            else f"SYN PMIC {self.scenario.battery_error}"
        )
        lines = (
            "001245 LOGGER SYN RING ONLINE",
            f"001676 POWER  {battery_log}",
            "001921 TOUCH  SYN CST9217 READY",
            "002118 IMU    SYN QMI8658 READY",
            "002406 GPS    SYN LC76G 3D FIX",
            "002734 RADIO  SYN BLE ADVERTISING",
            "003012 APP    SYN SNAPSHOT PUBLISHED",
        )
        for index, line in enumerate(lines):
            y = 181 + (index * 28)
            self.canvas.create_text(
                104,
                y,
                anchor="nw",
                text=line,
                fill="#d8ecff",
                font=(FONT_TECH, 7),
                tags="ui",
            )
        self.canvas.create_text(
            233,
            404,
            text="SYNTHETIC ONLY · NO DEVICE / SERIAL",
            fill=MUTED,
            font=(FONT_TECH, 6, "bold"),
            tags="ui",
        )
        self._draw_edge_back()
        self._draw_safe_guide()

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
        gps_y = int(layout["gps"])
        self.canvas.create_text(
            94,
            gps_y - 14,
            anchor="w",
            text=(
                "WIRELESS ACTIVE"
                if self.wireless_running
                else "WIRELESS OFF"
            ),
            fill=(STATUS_READY if self.wireless_running else STATUS_WARNING),
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        if self._command_arc_visible():
            gps_role = (
                "warning"
                if self.scenario.name == "GPS SEARCHING"
                else "success"
            )
            sd_role = (
                "warning"
                if self.scenario.name == "SD UNFORMATTED"
                else "success"
            )
            commands = (
                ("≡", "primary"),
                ("➤", "success"),
                (
                    "power" if self.wireless_running else "wifi",
                    "success" if self.wireless_running else "warning",
                ),
                ("gps", gps_role),
                ("sd", sd_role),
            )
            for index, (icon, role) in enumerate(commands):
                x, y, diameter = command_geometry(len(commands), index)
                self._draw_command_button(x, y, icon, role, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def _draw_synthetic_qr(self, x: int, y: int, size: int = 140) -> None:
        self.canvas.create_rectangle(
            x,
            y,
            x + size,
            y + size,
            fill="#ffffff",
            outline="#d8ecff",
            width=2,
            tags="ui",
        )
        module = 5
        origin_x = x + 17
        origin_y = y + 17
        for row in range(21):
            for column in range(21):
                in_finder = (
                    (row < 7 and column < 7)
                    or (row < 7 and column >= 14)
                    or (row >= 14 and column < 7)
                )
                if in_finder:
                    local_row = row if row < 7 else row - 14
                    local_column = column if column < 7 else column - 14
                    filled = (
                        local_row in {0, 6}
                        or local_column in {0, 6}
                        or (
                            2 <= local_row <= 4
                            and 2 <= local_column <= 4
                        )
                    )
                else:
                    filled = ((row * 11) + (column * 7) + (row * column)) % 5 < 2
                if filled:
                    left = origin_x + (column * module)
                    top = origin_y + (row * module)
                    self.canvas.create_rectangle(
                        left,
                        top,
                        left + module,
                        top + module,
                        fill="#061017",
                        outline="",
                        tags="ui",
                    )

    def show_pairing_demo(self, screen: str) -> None:
        preview = PAIRING_PREVIEWS[screen]
        self.screen = screen
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        self.canvas.create_text(
            DISPLAY_SIZE // 2,
            77,
            text="SYNTHETIC PREVIEW",
            fill=MUTED,
            font=(FONT_DISPLAY, 7),
            tags="ui",
        )

        accent = (
            STATUS_FAULT
            if preview.result == "failed"
            else STATUS_READY
        )
        def create_label(
            text: str,
            width: int,
            font: tuple[str, int],
            color: str,
        ) -> tuple[int, int]:
            item = self.canvas.create_text(
                DISPLAY_SIZE // 2,
                0,
                anchor="n",
                width=width,
                text=text,
                justify="center",
                fill=color,
                font=font,
                tags="ui",
            )
            bounds = self.canvas.bbox(item)
            if bounds is None:
                raise RuntimeError(f"Unable to measure pairing label {text!r}")
            return item, bounds[3] - bounds[1]

        step_item, step_height = create_label(
            preview.step,
            272,
            (FONT_DISPLAY, 5),
            accent,
        )
        title_item, title_height = create_label(
            preview.title,
            292,
            (FONT_DISPLAY, 11),
            TEXT,
        )
        message_item, message_height = create_label(
            preview.message,
            286,
            (FONT_BODY, 7),
            TEXT,
        )
        detail_item, detail_height = create_label(
            preview.detail,
            272,
            (FONT_BODY, 6),
            MUTED_BRIGHT,
        )

        elements: list[tuple[str, int | None, int]] = [
            ("label", step_item, step_height),
        ]
        if preview.result == "normal":
            elements.append(("progress", None, 4))
        elements.extend(
            [
                ("label", title_item, title_height),
                ("label", message_item, message_height),
            ]
        )
        if preview.show_qr:
            elements.append(("qr", None, 140))
        elements.append(("label", detail_item, detail_height))

        gap = 4
        viewport_top = 84
        viewport_height = 264
        content_height = (
            sum(height for _, _, height in elements)
            + gap * (len(elements) - 1)
        )
        if content_height > viewport_height:
            raise RuntimeError(
                f"Pairing preview {screen!r} exceeds its viewport: "
                f"{content_height}px > {viewport_height}px"
            )

        cursor = viewport_top + ((viewport_height - content_height) / 2)
        for kind, item, height in elements:
            top = round(cursor)
            if kind == "label":
                if item is None:
                    raise RuntimeError("Pairing label is missing its canvas item")
                bounds = self.canvas.bbox(item)
                if bounds is None:
                    raise RuntimeError("Pairing label lost its measured bounds")
                self.canvas.move(item, 0, top - bounds[1])
            elif kind == "progress":
                left = 171
                right = left + 124
                self.canvas.create_rectangle(
                    left,
                    top,
                    right,
                    top + height,
                    fill="#173545",
                    outline="",
                    tags="ui",
                )
                self.canvas.create_rectangle(
                    left,
                    top,
                    left + int(124 * preview.progress / 5),
                    top + height,
                    fill=STATUS_READY,
                    outline="",
                    tags="ui",
                )
            elif kind == "qr":
                self._draw_synthetic_qr(163, top)
            cursor += height + gap

        self._draw_edge_back()
        if screen not in {
            "pairing-device-qr",
            "pairing-credentials",
            "pairing-connecting",
        }:
            self._draw_edge_next()
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
            ("DISTANCE", "2 × TOF400F", "SYNTHETIC READY"),
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
            commands = (("fft", "success"), ("dual", "success"))
            for index, (icon, role) in enumerate(commands):
                x, y, diameter = command_geometry(len(commands), index)
                self._draw_command_button(x, y, icon, role, diameter)
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
            else "Mic 1 or mean of Mic 1 + Mic 2 | 16 bands",
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
            319,
            text="SYNTHETIC ACC   X +0.02   Y -0.04   Z +9.79 m/s²",
            anchor="w",
            fill="#61d8ff",
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            72,
            337,
            text="SYNTHETIC GYRO  X +0.10   Y -0.03   Z +0.02 dps",
            anchor="w",
            fill="#ff9d5c",
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            92,
            355,
            anchor="w",
            text="SYNTHETIC CAL READY",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            374,
            355,
            anchor="e",
            text="COMPASS N/A",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "⚙", False, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_gps_demo(self) -> None:
        self.screen = "gps"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "GPS LIVE",
            "LC76G | SYNTHETIC DEVICE SNAPSHOT",
            0,
            title_size=18,
            subtitle_baseline=132,
            divider_y=150,
        )

        searching = self.scenario.name == "GPS SEARCHING"
        accent = STATUS_WARNING if searching else STATUS_READY
        fix_state = "NO FIX" if searching else "FIX ACTIVE"
        self.canvas.create_text(
            84,
            166,
            anchor="w",
            text="LC76G READY | RECEIVER ONLINE",
            fill=accent,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            382,
            166,
            anchor="e",
            text=fix_state,
            fill=accent,
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self._draw_panel(
            81,
            178,
            304,
            100,
            top="#0d2636",
            bottom="#06121b",
            border=accent,
            glow=accent,
            glow_alpha=34,
            radius=12,
        )
        position = (
            ("NO FIX", "NO FIX")
            if searching
            else ("10.776900 N", "106.700900 E")
        )
        self.canvas.create_text(
            96,
            192,
            anchor="w",
            text="LATITUDE",
            fill=STATUS_READY,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            96,
            211,
            anchor="w",
            text=position[0],
            fill=TEXT,
            font=(FONT_TECH, 12, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            96,
            235,
            anchor="w",
            text="LONGITUDE",
            fill="#51c7f2",
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            96,
            254,
            anchor="w",
            text=position[1],
            fill=TEXT,
            font=(FONT_TECH, 12, "bold"),
            tags="ui",
        )

        radar_x = 331
        radar_y = 228
        self._draw_glow_disc(radar_x, radar_y, 38, accent, 36)
        self.canvas.create_oval(
            radar_x - 36,
            radar_y - 36,
            radar_x + 36,
            radar_y + 36,
            fill="#081b27",
            outline="#2c6077",
            width=1,
            tags="ui",
        )
        self.canvas.create_line(
            radar_x - 27,
            radar_y,
            radar_x + 27,
            radar_y,
            fill="#2c6077",
            tags="ui",
        )
        self.canvas.create_line(
            radar_x,
            radar_y - 27,
            radar_x,
            radar_y + 27,
            fill="#2c6077",
            tags="ui",
        )
        self.canvas.create_text(
            radar_x,
            radar_y - 27,
            text="N",
            fill=STATUS_WARNING,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        if not searching:
            satellite_points = ((-18, -12), (15, -19), (22, 13), (-11, 21))
            for dx, dy in satellite_points:
                self.canvas.create_oval(
                    radar_x + dx - 2,
                    radar_y + dy - 2,
                    radar_x + dx + 2,
                    radar_y + dy + 2,
                    fill="#80efff",
                    outline="",
                    tags="ui",
                )
        self.canvas.create_oval(
            radar_x - 4,
            radar_y - 4,
            radar_x + 4,
            radar_y + 4,
            fill=accent,
            outline="#e7ffff",
            width=1,
            tags="ui",
        )

        self.canvas.create_text(
            233,
            294,
            text=(
                "SYNTHETIC | RECEIVER READY | NO FIX"
                if searching
                else "SYNTHETIC | RECEIVER READY | FIX ACTIVE"
            ),
            fill=accent,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        metrics = (
            (315, "SATELLITES", "3" if searching else "12", accent),
            (333, "HDOP", "NO FIX" if searching else "0.8", "#51c7f2"),
            (351, "ALTITUDE", "NO FIX" if searching else "12.4 m", STATUS_WARNING),
            (369, "SPEED", "NO FIX" if searching else "0.12 m/s", "#c168ff"),
            (387, "COURSE", "NO FIX" if searching else "185.4 deg", TEXT),
            (
                405,
                "GNSS TIME",
                "NO FIX" if searching else "09:42:18 UTC",
                TEXT,
            ),
            (
                423,
                "SAMPLE",
                "QUALITY 0 | SEARCHING"
                if searching
                else "QUALITY 1 | QUEUE 0 B",
                accent,
            ),
        )
        for y, label, value, color in metrics:
            self._draw_battery_metric(y, label, value, color)
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        self._draw_edge_back()
        self._draw_safe_guide()

    def _draw_tof_sensor_panel(
        self,
        x: int,
        label: str,
        distance: str,
        color: str,
    ) -> None:
        self._draw_panel(
            x,
            239,
            132,
            54,
            top="#103346",
            bottom="#081721",
            border="#2c6077",
            glow=color,
            glow_alpha=28,
            radius=12,
        )
        self.canvas.create_text(
            x + 66,
            252,
            text=f"{label} · SYNTHETIC",
            fill=MUTED_BRIGHT,
            font=(FONT_DISPLAY, 7),
            tags="ui",
        )
        self.canvas.create_text(
            x + 66,
            274,
            text=distance,
            fill=color,
            font=(FONT_TECH, 14, "bold"),
            tags="ui",
        )

    def show_tof_demo(self) -> None:
        self.screen = "tof"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "DISTANCE",
            "SYNTHETIC DUAL TOF",
            0,
            subtitle_baseline=142,
            divider_y=160,
        )
        self.canvas.create_text(
            233,
            172,
            text="LIVE SYNTHETIC SAMPLE",
            fill=STATUS_READY,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            201,
            text="1 024 mm",
            fill=STATUS_READY,
            font=(FONT_TECH, 23, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            227,
            text="SYNTHETIC VERTICAL DISTANCE",
            fill=MUTED_BRIGHT,
            font=(FONT_DISPLAY, 7),
            tags="ui",
        )
        self._draw_tof_sensor_panel(94, "TOF 1", "1 018 mm", "#51c7f2")
        self._draw_tof_sensor_panel(240, "TOF 2", "1 030 mm", "#7d9cff")
        self.canvas.create_text(
            233,
            310,
            text="SYNTHETIC · Δ 12 mm · SLANT 1024 mm",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            330,
            text="TILT +0.5° · COMPENSATED",
            fill=STATUS_WARNING,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            349,
            text="CONF 98% · CAL READY",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "⚙", False, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_tof_settings_demo(
        self,
        return_screen: str | None = None,
    ) -> None:
        if return_screen == "tof":
            self.tof_settings_return_screen = return_screen
        self.screen = "tof-settings"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        y_offset = self._wide_page_offset_y()
        self._draw_page_heading(
            "TOF SETTINGS",
            "SYNTHETIC DUAL TOF",
            y_offset,
            title_size=18,
            subtitle_baseline=142,
            divider_y=160,
        )
        calibration_state = (
            "SYNTHETIC CAL COMPLETE"
            if self.tof_calibration_complete
            else "SYNTHETIC CONFIG READY"
        )
        self.canvas.create_text(
            233,
            172 + y_offset,
            text=calibration_state,
            fill=STATUS_READY,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        controls = (
            (94, "SAMPLES", "500"),
            (240, "INTERVAL", "250 ms"),
        )
        for x, title, value in controls:
            self._draw_panel(
                x,
                190 + y_offset,
                132,
                58,
                top="#173145",
                bottom="#0d1a24",
                border="#3a6c83",
                glow="#0b55d9",
                glow_alpha=25,
                radius=10,
            )
            self.canvas.create_text(
                x + 66,
                203 + y_offset,
                text=title,
                fill=MUTED_BRIGHT,
                font=(FONT_DISPLAY, 7),
                tags="ui",
            )
            self.canvas.create_text(
                x + 66,
                223 + y_offset,
                text=value,
                fill=TEXT,
                font=(FONT_TECH, 11, "bold"),
                tags="ui",
            )
            self.canvas.create_text(
                x + 66,
                239 + y_offset,
                text="SYNTHETIC",
                fill=MUTED_BRIGHT,
                font=(FONT_TECH, 6, "bold"),
                tags="ui",
            )
        self.canvas.create_text(
            94,
            268 + y_offset,
            anchor="w",
            text="REFERENCE",
            fill=TEXT,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            372,
            268 + y_offset,
            anchor="e",
            text="1 000 mm",
            fill=STATUS_READY,
            font=(FONT_TECH, 10, "bold"),
            tags="ui",
        )
        self.canvas.create_line(
            104,
            296 + y_offset,
            362,
            296 + y_offset,
            fill=TRACK,
            width=7,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self.canvas.create_line(
            104,
            296 + y_offset,
            233,
            296 + y_offset,
            fill=STATUS_READY,
            width=7,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self._draw_glow_disc(
            233,
            296 + y_offset,
            12,
            STATUS_READY,
            80,
        )
        self.canvas.create_oval(
            224,
            287 + y_offset,
            242,
            305 + y_offset,
            fill="#eafffb",
            outline=STATUS_READY,
            width=2,
            tags="ui",
        )
        self.canvas.create_text(
            104,
            317 + y_offset,
            anchor="w",
            text="500 mm",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7),
            tags="ui",
        )
        self.canvas.create_text(
            362,
            317 + y_offset,
            anchor="e",
            text="1 500 mm",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            339 + y_offset,
            text="SYNTHETIC REFERENCE",
            fill=STATUS_WARNING,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            357 + y_offset,
            text="PRESS PLAY TO CALIBRATE",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▶", True, diameter)
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

    def pairing_next(self) -> None:
        target = {
            "pairing-intro": "pairing-bluetooth",
            "pairing-bluetooth": "pairing-app",
            "pairing-app": "pairing-app-qr",
            "pairing-app-qr": "pairing-device-qr",
            "pairing-success": "settings",
            "pairing-failed": "pairing-device-qr",
        }.get(self.screen)
        if target is None:
            return
        self.logger.info(
            "qsee_pairing",
            f"Synthetic pairing navigation: {self.screen} -> {target}",
        )
        if target == "settings":
            self.show_settings_demo()
        else:
            self.show_pairing_demo(target)

    def back(self) -> None:
        if self.screen == "battery":
            self.logger.info(
                "qsee_nav",
                f"Synthetic Battery closed; returning to {self.battery_return_screen}",
            )
            if self.battery_return_screen == "hardware":
                self.show_hardware_demo()
            else:
                self.show_home()
            return
        if self.screen == "logs":
            self.logger.info(
                "qsee_nav",
                "Synthetic Logs closed; returning to Settings",
            )
            self.show_settings_demo()
            return
        if self.screen == "gps":
            self.logger.info(
                "qsee_nav",
                "Synthetic GPS closed; returning to Settings",
            )
            self.show_settings_demo()
            return
        if self.screen == "tof-settings":
            self.logger.info(
                "qsee_nav",
                "Synthetic TOF settings closed; returning to TOF",
            )
            self.show_tof_demo()
            return
        if self.screen in PAIRING_SCREENS:
            target = {
                "pairing-bluetooth": "pairing-intro",
                "pairing-app": "pairing-bluetooth",
                "pairing-app-qr": "pairing-app",
                "pairing-device-qr": "pairing-app-qr",
                "pairing-credentials": "pairing-app-qr",
                "pairing-connecting": "pairing-app-qr",
                "pairing-success": "pairing-app-qr",
                "pairing-failed": "pairing-app-qr",
            }.get(self.screen)
            self.logger.info("qsee_pairing", "Synthetic pairing Back")
            if target is None:
                self.show_settings_demo()
            else:
                self.show_pairing_demo(target)
            return
        if self.screen in {
            "audio",
            "fft",
            "fft-detail",
            "imu",
            "tof",
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
        self.battery_return_screen = "home"
        self.tof_settings_return_screen = "tof"
        self.tof_calibration_complete = False
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
        if current_screen in PAIRING_SCREENS:
            self.show_pairing_demo(current_screen)
            self.apply_idle_overlay()
            return
        redraw = {
            "home": self.show_home,
            "battery": self.show_battery_demo,
            "logs": self.show_logs_demo,
            "settings": self.show_settings_demo,
            "storage": self.show_storage_demo,
            "chrome-settings": self.show_chrome_settings_demo,
            "power-settings": self.show_power_settings_demo,
            "hardware": self.show_hardware_demo,
            "audio": self.show_audio_demo,
            "fft": self.show_audio_fft_demo,
            "fft-detail": self.show_audio_detail_demo,
            "imu": self.show_imu_demo,
            "gps": self.show_gps_demo,
            "tof": self.show_tof_demo,
            "tof-settings": self.show_tof_settings_demo,
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
            "battery",
            "logs",
            "settings",
            "storage",
            "chrome-settings",
            "power-settings",
            "hardware",
            "audio",
            "fft",
            "fft-detail",
            "imu",
            "gps",
            "tof",
            "tof-settings",
            *sorted(PAIRING_SCREENS),
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
    parser.add_argument("--wireless-running", action="store_true")
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
    simulator.wireless_running = args.wireless_running
    simulator.safe_guide = args.safe_guide
    if args.capture is not None:
        from PIL import ImageGrab

        simulator.ready = True
        screen_drawers = {
            "home": simulator.show_home,
            "battery": simulator.show_battery_demo,
            "logs": simulator.show_logs_demo,
            "settings": simulator.show_settings_demo,
            "storage": simulator.show_storage_demo,
            "chrome-settings": simulator.show_chrome_settings_demo,
            "power-settings": simulator.show_power_settings_demo,
            "hardware": simulator.show_hardware_demo,
            "audio": simulator.show_audio_demo,
            "fft": simulator.show_audio_fft_demo,
            "fft-detail": simulator.show_audio_detail_demo,
            "imu": simulator.show_imu_demo,
            "gps": simulator.show_gps_demo,
            "tof": simulator.show_tof_demo,
            "tof-settings": simulator.show_tof_settings_demo,
        }
        if args.screen in PAIRING_SCREENS:
            simulator.show_pairing_demo(args.screen)
        else:
            screen_drawers[args.screen]()
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
