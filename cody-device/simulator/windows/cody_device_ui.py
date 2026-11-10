"""Device-first Cody Studio preview, isolated from the legacy artwork editor."""

from __future__ import annotations

from enum import Enum, auto
from functools import lru_cache
import json
from math import isqrt
from pathlib import Path
from secrets import randbits
import time
import tkinter as tk
from tkinter import messagebox, ttk
import webbrowser

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageTk

from cody_ccp_event_view import EventView
from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_device_state import (
    InfoPage, LocalTimerModel, LocalTimerPhase, ScreensaverModel, SystemInfoModel, SystemInfoObservation,
    TIMER_MAX_MINUTES, TIMER_MIN_MINUTES,
)
from cody_fake_audio import FakeAudioDriver
from cody_muse_preview import DISPLAY_SIZE as MUSE_SIZE, NO_REACTION, SCREEN_OFFSETS_MS, MusePose, NativeMusePreview
from cody_preview_layout import PreviewLayout, SIZE, enlarge_framebuffer
from cody_screensaver import render_screensaver
from cody_ui_theme import install_theme
from vibeware_boot import VibeWareBoot


CENTER = SIZE // 2
MUSE_OFFSET = (SIZE - MUSE_SIZE) // 2
BACKGROUND = (8, 11, 22)
FOREGROUND = (239, 236, 224)
MUTED = (153, 168, 186)
ACCENT = (122, 218, 222)
SETUP_ACCENT = (144, 167, 255)
SETUP_AMBER = (255, 183, 74)
SETUP_AMBER_FIELD = (54, 36, 14)
CONTROL_PALETTE = ((16, 40, 58), (8, 24, 33), (41, 105, 123), (140, 226, 239))
SECONDARY_CONTROL_PALETTE = ((25, 28, 41), (16, 20, 25), (74, 81, 99), (189, 190, 189))
DESTRUCTIVE_CONTROL_PALETTE = ((41, 20, 33), (24, 16, 24), (123, 45, 90), (239, 170, 156))
CODEX_CONNECTION_COLOR = (186, 178, 255)
WIFI_CONNECTION_COLOR = (119, 233, 202)
OPTIONS_TITLE_TOP = 82
OPTIONS_ROW_TOP = 130
OPTIONS_ROW_STEP = 80
OPTIONS_ROW_WIDTH = 280
OPTIONS_ROW_HEIGHT = 72
OPTIONS_PAGE_COUNT = 3
QUICK_GUIDE = (
    ("Buttons 1/3", ACCENT, (
        ("BOOT", "Open Options"), ("POWER", "Standby or wake"), ("HOLD PWR 3S", "Power off"),
    ), None),
    ("Touch 2/3", (188, 164, 255), (
        ("TAP", "Show arrows"), ("SHAKE", "Return home"), ("DOUBLE TAP", "Record on stats"),
    ), "Dictation On + Codex link"),
    ("Offline 3/3", (133, 239, 195), (
        ("SAVED STATS", "Last update in UTC"), ("TIMER", "Works without PC"), ("RESTART", "Keeps settings"),
    ), None),
)
NAVIGATION_IDLE_SECONDS = 10
NAVIGATION_FADE_SECONDS = 0.35
HOME_MENU_TRANSITION_SECONDS = 0.5
SIMULATED_WIFI_KEYS = {
    "letters": (".-_@", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"),
    "numbers": ("01", "2", "3", "4", "5", "6", "7", "8", "9"),
    "symbols": ("!\"#$", "%&'(", ")*+,", "-./:", ";<=>", "?@[\\", "]^_`", "{|}~", "@_`"),
}
PAGES = (
    "Device", "Reset countdown", "Context", "Codex limits", "Today", "Last 7 days",
    "Token history", "Credits", "Codex account", "About",
)
VISIBILITY_ROWS = 3
VISIBILITY_PAGE_COUNT = (len(PAGES) - 1 + VISIBILITY_ROWS - 1) // VISIBILITY_ROWS
ABOUT_INFO = "Scan for more info"
ABOUT_URL = "https://umbertogiacobbi.biz/esp32-luna-shell"
# QR Model 2, error correction M, generated from ABOUT_URL with the atlas encoder.
ABOUT_QR = (
    "111111100001111011111011001111111",
    "100000100100011111000010101000001",
    "101110101111110111110001101011101",
    "101110101101001110001110101011101",
    "101110101100000100100100101011101",
    "100000101010010111101110101000001",
    "111111101010101010101010101111111",
    "000000001101101100100100000000000",
    "101111100110111101000011001111100",
    "010101000010101010111101001101101",
    "110011101100100110101100011010100",
    "110100000000101010011110010011101",
    "100110111010000000110100110111000",
    "100000000110001111001011011101011",
    "010000110101001000110010111001110",
    "011011011110111010011110011101100",
    "101010111100101001110011110110001",
    "010101011010011001111001011101111",
    "001111110101010101001010100110100",
    "101001001000000001110001111111111",
    "001000101100001010001110110111000",
    "110011000011000101100101001001001",
    "100010101000111111001010010110110",
    "101101001110101100100110010011100",
    "100011100111111111000010111111011",
    "000000001010100010111100100010111",
    "111111100100100110101101101010100",
    "100000101000000100000111100011100",
    "101110101000101000100100111111000",
    "101110101100111110001011110011101",
    "101110101010111001110101001101100",
    "100000100111001010111110001011100",
    "111111101110110001010011100100010",
)
# QR Model 2 for the same ESP BLE Provisioning Play Store URL as the firmware.
INSTALL_APP_QR = (
    "111111100010110001111111001111111",
    "100000101010011100101000001000001",
    "101110100000100010011101001011101",
    "101110101110111101011011101011101",
    "101110100100001010110101001011101",
    "100000101101100110001110101000001",
    "111111101010101010101010101111111",
    "000000000111000100110110000000000",
    "111110111101011001101000010101010",
    "111101000010101011011001001000111",
    "001000101010010110000010011011010",
    "000100001000100000001111111010100",
    "010111101110111111010010010111000",
    "010100011100010010111101001000011",
    "100111111101111111101100010010010",
    "001010011111001000000111011000100",
    "011111101001011111010011110110010",
    "010001010010101011111111101001011",
    "101011111000011111001100011111010",
    "010000001110100110000100000010100",
    "111100111100111101000011110110010",
    "101101011100000011111111001101011",
    "101111110001110100000100010001010",
    "101001011001000000001101010111100",
    "100011110101011111101011111111001",
    "000000001110110011111110100010101",
    "111111101010001101001001101010110",
    "100000100010100100101111100011111",
    "101110101000111111000000111110000",
    "101110101100000010111001000011000",
    "101110101111101110000010101000110",
    "100000101111001000001110011001100",
    "111111101011100111000011101100010",
)
HISTORY_WINDOWS = ("1 hour", "5 hours", "24 hours")
LEFT = (40, CENTER)
RIGHT = (SIZE - 40, CENTER)


class WifiSetupRoute(Enum):
    OFFLINE = auto()
    NOT_CONNECTED = auto()
    PHONE_GUIDE = auto()
    INSTALL_APP = auto()
    SCAN_TO_PAIR = auto()
    HOME_CHOICES = auto()
    HOME_INTRO = auto()
    HOME_PLATFORM_QR = auto()
    HOME_PAIR = auto()
    HOME_NETWORKS = auto()
    HOME_PASSWORD = auto()
    HOME_NETWORK_RESULT = auto()


@lru_cache(maxsize=8)
def font(size):
    for filename in ("consola.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf"):
        try:
            return ImageFont.truetype(filename, size)
        except OSError:
            continue
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


@lru_cache(maxsize=4)
# size: Requested pixel size of the vendored arcade face.
# Exception: Preserves the font loader error if the bundled face is unavailable.
def arcade_font(size):
    source = Path(__file__).resolve().parents[2] / "assets/cody/muse-fonts/unscii.ttf"
    return ImageFont.truetype(str(source), size)


@lru_cache(maxsize=3)
# caption: Uppercase setup heading or action.
# size: Pixel size of the bundled arcade face.
# advance: Horizontal pixel pitch after fitting the monospaced artwork to the menu.
# icon: Optional setup symbol sharing the wordmark's fill and depth.
# Exception: Preserves a failure to load the bundled face.
def setup_wordmark(caption, size, advance, icon=None):
    face = arcade_font(size)
    x0, y0, x1, y1 = face.getbbox(caption)
    height = max(2, y1 - y0)
    text = Image.new("L", (x1 - x0, height))
    ImageDraw.Draw(text).text((-x0, -y0), caption, font=face, fill=255)
    text = text.resize((round(text.width * advance / size), height), Image.Resampling.NEAREST)
    icon_width = height + 8 if icon else 0
    margin = 3
    mask = Image.new("L", (text.width + icon_width + 2 * margin, height + 2 * margin))
    if icon:
        center = margin + height // 2
        setup_icon(ImageDraw.Draw(mask), icon, (center, center), height, 255)
    mask.paste(text, (margin + icon_width, margin))
    mask = mask.point(lambda value: 255 if value >= 128 else 0)

    outline = mask.filter(ImageFilter.MaxFilter(3))
    width, rows = mask.size
    layer = Image.new("RGBA", (width + 2, rows + 3))
    layer.paste((47, 31, 128, 255), (2, 3, width + 2, rows + 3), outline)
    layer.paste((15, 11, 45, 255), (0, 0, width, rows), outline)

    face_fill = Image.new("RGBA", mask.size)
    fill = ImageDraw.Draw(face_fill)
    frost, lavender = ((192, 246, 255), (220, 153, 244)) if icon else ((218, 199, 255), (126, 91, 233))
    for y in range(rows):
        fraction = min(1.0, max(0.0, (y - margin) / (height - 1)))
        color = tuple(round(start + (end - start) * fraction) for start, end in zip(frost, lavender))
        fill.line((0, y, width - 1, y), fill=(*color, 255))
    face_fill.putalpha(mask)
    layer.alpha_composite(face_fill)
    return layer


def label(draw, text, top, size=20, color=FOREGROUND, x=CENTER, width=310):
    face = font(size)
    text = str(text)
    if draw.textlength(text, font=face) > width:
        while text and draw.textlength(text + "...", font=face) > width:
            text = text[:-1]
        text += "..."
    draw.text((x, top), text, font=face, fill=color, anchor="mt")


def blend(color, opacity):
    return tuple(round(base + (value - base) * opacity) for base, value in zip(BACKGROUND, color))


def arrow(draw, center, step, opacity=1):
    x, y = center
    draw.ellipse((x - 30, y - 30, x + 29, y + 29), fill=blend((43, 48, 59), opacity))
    draw.line(((x - 5 * step, y - 10), (x + 5 * step, y), (x - 5 * step, y + 10)),
              fill=blend(FOREGROUND, opacity), width=3, joint="curve")


# draw: Logical frame receiving the native-style capsule.
# bounds: Inclusive control rectangle.
# palette: Top fill, bottom fill, outline and icon RGB colors.
# corner_radius: Override for a settings row.
def draw_control_pill(draw, bounds, palette, corner_radius=None):
    left, top, right, bottom = bounds
    top_color, bottom_color, outline, _ = palette
    radius = corner_radius if corner_radius is not None else (bottom - top) // 2
    draw.rounded_rectangle(bounds, radius=radius, fill=outline)
    inner_radius = radius - 1
    for y in range(top + 1, bottom):
        dy = max(top + radius - y, y - (bottom - radius), 0)
        inset = inner_radius - isqrt(inner_radius * inner_radius - dy * dy)
        fraction = (y - top) / (bottom - top)
        color = tuple(round(start + (end - start) * fraction)
                      for start, end in zip(top_color, bottom_color))
        draw.line((left + 1 + inset, y, right - 1 - inset, y), fill=color)


@lru_cache(maxsize=3)
# url: Exact official application-store URL.
# Returns None when the optional ReportLab tooling is unavailable.
def store_qr_matrix(url):
    try:
        from reportlab.graphics.barcode.qr import QrCodeWidget
    except ImportError:
        return None
    code = QrCodeWidget(url, barLevel="M")
    code.qr.make()
    return tuple("".join("1" if value else "0" for value in row) for row in code.qr.modules)


# draw: Logical framebuffer receiving the white tile and exact square modules.
# matrix: Square tuple of binary module rows.
# top: Logical y coordinate of the tile.
# size: White tile side including a quiet zone of at least four modules.
def qr_tile(draw, matrix, top, size):
    module = size // (len(matrix) + 8)
    left = CENTER - size // 2
    quiet = (size - len(matrix) * module) // 2
    draw.rectangle((left, top, left + size - 1, top + size - 1), fill=(255, 255, 255))
    for row, modules in enumerate(matrix):
        y = top + quiet + row * module
        for column, value in enumerate(modules):
            if value == "1":
                x = left + quiet + column * module
                draw.rectangle((x, y, x + module - 1, y + module - 1), fill=(0, 0, 0))


# draw: Logical framebuffer receiving one outline symbol.
# icon: Semantic outline icon used by the setup screens.
# center: Logical icon center.
# size: Nominal square icon envelope.
# color: RGB stroke color.
def setup_icon(draw, icon, center, size, color):
    x, y = center
    half = size // 2
    stroke = max(2, size // 11)
    if icon == "clock":
        edge = half - stroke
        draw.ellipse((x - edge, y - edge, x + edge, y + edge), outline=color, width=stroke)
        draw.line(((x, y - edge // 2), (x, y), (x + edge // 2, y + edge // 3)),
                  fill=color, width=stroke, joint="curve")
    elif icon in ("wifi", "offline"):
        for radius in (half, half * 2 // 3, half // 3):
            cy = y + half // 2
            draw.arc((x - radius, cy - radius, x + radius, cy + radius),
                     220, 320, fill=color, width=stroke)
        cy = y + half // 2 + stroke
        draw.ellipse((x - stroke // 2, cy - stroke // 2, x + stroke // 2, cy + stroke // 2), fill=color)
        if icon == "offline":
            slash = ((x - half + stroke, y + half), (x + half - stroke, y - half))
            draw.line(slash, fill=SETUP_AMBER_FIELD, width=stroke * 3)
            draw.line(slash, fill=color, width=stroke)
    elif icon == "link":
        scale = size / 24
        loop = ((-9, 4), (-5, 0), (-3, -1), (-1, 0), (0, 2), (-1, 4),
                (-5, 8), (-7, 9), (-9, 8), (-10, 6), (-9, 4))
        for direction in (-1, 1):
            draw.line([(x + direction * dx * scale, y + direction * dy * scale) for dx, dy in loop],
                      fill=color, width=stroke, joint="curve")
        draw.line((x - 3 * scale, y + 3 * scale, x + 3 * scale, y - 3 * scale),
                  fill=color, width=stroke)
    elif icon == "motion":
        gap = size // 5
        for offset in (-gap, 0, gap):
            wave = ((x - half + stroke, y + offset), (x - half // 2, y + offset - stroke),
                    (x, y + offset), (x + half // 2, y + offset + stroke),
                    (x + half - stroke, y + offset))
            draw.line(wave, fill=color, width=stroke, joint="curve")
    elif icon == "retry":
        edge = half - stroke
        draw.arc((x - edge, y - edge, x + edge, y + edge), 38, 325, fill=color, width=stroke)
        draw.line(((x + edge - stroke * 2, y - edge), (x + edge, y - edge + stroke * 2),
                   (x + edge, y - edge - stroke)), fill=color, width=stroke, joint="curve")
    elif icon == "phone":
        side = half * 2 // 3
        draw.rounded_rectangle((x - side, y - half, x + side, y + half),
                               radius=stroke * 2, outline=color, width=stroke)
        tip = y + half // 2
        arrow_side = half // 3
        draw.line((x, y - half // 2, x, tip), fill=color, width=stroke)
        draw.line(((x - arrow_side, tip - arrow_side), (x, tip), (x + arrow_side, tip - arrow_side)),
                  fill=color, width=stroke, joint="curve")
    elif icon == "bluetooth":
        side = half * 2 // 3
        draw.line(((x, y - half), (x, y + half), (x + side, y + half // 2), (x - side, y - half // 2)),
                  fill=color, width=stroke, joint="curve")
        draw.line(((x, y - half), (x + side, y - half // 2), (x - side, y + half // 2)),
                  fill=color, width=stroke, joint="curve")
    elif icon == "qr":
        cell = size // 3
        for left, top in ((x - half, y - half), (x + half - cell, y - half), (x - half, y + half - cell)):
            draw.rectangle((left, top, left + cell, top + cell), outline=color, width=stroke)
        draw.rectangle((x + half - stroke * 2, y + half - stroke * 2, x + half, y + half), fill=color)
    elif icon == "chevron":
        span = size // 4
        draw.line(((x - span, y - span), (x + span, y), (x - span, y + span)),
                  fill=color, width=stroke, joint="curve")
    elif icon == "home":
        side = half - 2
        draw.line(((x - side, y), (x, y - side), (x + side, y)),
                  fill=color, width=stroke, joint="curve")
        draw.line(((x - side + 2, y - 2), (x - side + 2, y + side),
                   (x + side - 2, y + side), (x + side - 2, y - 2)),
                  fill=color, width=stroke, joint="curve")


def rotate(image, rotation):
    if rotation:
        return image.transpose({
            90: Image.Transpose.ROTATE_270,
            180: Image.Transpose.ROTATE_180,
            270: Image.Transpose.ROTATE_90,
        }[rotation])
    return image


class DeviceStudioWindow:
    """A round preview with device navigation and an optional authenticated loopback link."""

    # root: Tk window, or None for a deterministic capture with no endpoint.
    # args: Initial orientation, display guide and interactive CCP admission.
    # Raises ValueError when a capture requests a live CCP endpoint.
    def __init__(self, root, args):
        self.root = root
        self.restore_preview_defaults()
        self.rotation = args.rotation
        if args.rotation != 0:
            self.auto_rotate = False
        self.safe_guide = args.safe_guide
        self.endpoint = None
        self.events = None
        self.source_error = ""
        self.audio_error = ""
        self.audio = None
        self.muse = None
        self.setup_mascot = None
        self.muse_elapsed_ms = 0.0
        self.mascot_was_waking = False
        self.muse_last_frame_at = None
        self.muse_reaction_ms = None
        self.muse_seed = 0
        self.motion_until = 0.0
        self.motion_direction = (0.0, 0.0)
        self.motion_sample_at_us = 0
        self.motion_acceleration = (0.0, 0.0)
        self.touch_pressed_at = 0.0
        self.route = WifiSetupRoute.OFFLINE
        self.connections_return = "widgets"
        self.guide_return = WifiSetupRoute.NOT_CONNECTED
        self.pairing_return = WifiSetupRoute.PHONE_GUIDE
        self.home_pair_return = WifiSetupRoute.HOME_CHOICES
        self.home_menu_since = None
        self.wifi_networks_scanned = False
        self.wifi_scan_until = 0.0
        self.wifi_selected_network = ""
        self.wifi_selected_index = -1
        self.wifi_network_scroll_elapsed = 0.0
        self.wifi_network_scroll_last_draw = None
        self.wifi_password = ""
        self.wifi_key_mode = "letters"
        self.wifi_key_uppercase = False
        self.wifi_key_reveal = False
        self.wifi_pending_key = None
        self.wifi_pending_index = 0
        self.wifi_last_key_at = 0.0
        self.wifi_result = ""
        self.show_battery_percentage = True
        self.simulated_ble_name = f"AiGadget-{randbits(24):06X}"
        self.pairing_attempted = False
        self.page = 0
        self.options_page = 0
        self.quick_guide_page = 0
        self.visibility_page = 0
        self.standby = False
        self.touch = None
        self.event_visible = False
        self.available_pages = (0, PAGES.index("About"))
        self.chat_title_key = None
        self.chat_title_since = 0.0
        self.targets = {}
        self.acknowledged = ()
        self.photo = None
        self.timer = None
        self.started_at = self.last_navigation = time.monotonic()
        self.screensaver_idle_since = self.started_at
        if root is None:
            if args.ccp:
                raise ValueError("Cody Studio captures cannot start a CCP endpoint")
            return
        if args.ccp:
            endpoint = SimulatorEndpoint()
            self.route = "connections"
            try:
                endpoint.start()
                self.events = EventView()
                self.endpoint = endpoint
                snapshot, _ = endpoint.observe()
                self.pairing_attempted = snapshot.pending_pairing
                if snapshot.retained_pairing and not self.pairing_attempted:
                    self.route = "widgets"
                elif not snapshot.network_ready and not self.pairing_attempted:
                    self.route = WifiSetupRoute.OFFLINE
            except (OSError, RuntimeError, ValueError) as error:
                endpoint.close()
                self.source_error = str(error)

        try:
            self.audio = FakeAudioDriver(Path(__file__).resolve().parents[3])
        except (OSError, RuntimeError, AttributeError) as error:
            self.audio_error = f"Host effects unavailable: {error}"
        self.start_boot()
        phase = SCREEN_OFFSETS_MS.get(getattr(args, "screen", "rig"))
        if phase is not None:
            self.boot = self.boot_frame = None
            self.muse_elapsed_ms = float(phase)

        colors = install_theme(root)
        root.title("Cody Studio / SIMULATED device")
        root.configure(background=colors["background"])
        self.canvas = tk.Canvas(root, width=SIZE + 24, height=SIZE + 24, borderwidth=0,
                                highlightthickness=0, background=colors["canvas"])
        self.canvas.pack(fill="both", expand=True, padx=12, pady=(12, 0))
        self.image = self.canvas.create_image(0, 0, anchor="nw")
        self.layout = PreviewLayout.fit(SIZE + 24, SIZE + 24, False)
        controls = ttk.Frame(root, style="Studio.TFrame")
        controls.pack(padx=12, pady=12)
        ttk.Button(controls, text="BOOT / Options", command=self.options).pack(side="left", padx=4)
        ttk.Button(controls, text="PWR / Standby", command=self.toggle_standby).pack(side="left", padx=4)
        ttk.Button(controls, text="Simulate shake", command=self.shake).pack(side="left", padx=4)
        ttk.Button(controls, text="SIM Sophia", command=self.simulate_sophia).pack(side="left", padx=4)
        simulated = ttk.Frame(root, style="Studio.TFrame")
        simulated.pack(padx=12, pady=(0, 6))
        ttk.Label(simulated, text="SIMULATED motion", style="Status.TLabel").pack(side="left", padx=4)
        for caption, direction in (("Left", (-1, 0)), ("Right", (1, 0)),
                                   ("Up", (0, -1)), ("Down", (0, 1))):
            ttk.Button(simulated, text=caption, width=6,
                       command=lambda vector=direction: self.simulate_motion(vector)).pack(side="left", padx=2)
        ttk.Button(simulated, text="Turn 90°", width=9, command=self.turn_device).pack(side="left", padx=4)
        status_bar = ttk.Frame(root, style="Studio.TFrame")
        status_bar.pack(pady=(0, 12))
        self.status = ttk.Label(status_bar, style="Status.TLabel")
        self.status.pack(side="left", padx=4)
        if self.endpoint is not None:
            link_controls = ttk.Menubutton(status_bar, text="Simulated link")
            link_menu = tk.Menu(link_controls, tearoff=False)
            link_menu.add_command(label="Toggle simulated Wi-Fi", command=lambda: self.manage_simulated_link("network"))
            link_menu.add_command(label="Forget pairing...", command=lambda: self.manage_simulated_link("forget"))
            link_controls.configure(menu=link_menu)
            link_controls.pack(side="left", padx=4)
        root.minsize(SIZE + 48, SIZE + 140)
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.drag)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        root.bind("<Escape>", lambda _: self.back())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.tick()

    # self: Preview window loading the same visible defaults at startup and after reset.
    def restore_preview_defaults(self):
        self.rotation = 0
        self.device_rotation = 0
        self.auto_rotate = True
        self.history_window = 1
        self.visible = set(PAGES)
        self.brightness = 80
        self.shake_to_wake = False
        self.info = SystemInfoModel()
        self.screensaver = ScreensaverModel()
        self.local_timer = LocalTimerModel()
        self.timer_return = "options"

    # self: Preview window playing the standard startup animation.
    def start_boot(self):
        self.boot = self.boot_frame = None
        self.boot_error = ""
        self.muse_elapsed_ms = 0.0
        self.mascot_was_waking = False
        self.muse_last_frame_at = None
        self.muse_reaction_ms = None
        self.muse_seed = randbits(32)
        self.home_menu_since = None
        self.setup_mascot = None
        if self.muse is not None:
            self.muse.reset(self.muse_seed)
        self.open("mascot")
        project = Path(__file__).resolve().parents[2]
        try:
            manifest = project / "assets/branding/VibeWare/generated/manifest.json"
            self.boot_cycle_ms = json.loads(manifest.read_text(encoding="utf-8"))["cycle_duration_ms"]
            self.boot = VibeWareBoot(project, cody_companion=True)
            self.boot_started_at = time.monotonic()
        except (OSError, RuntimeError, ValueError, AttributeError, KeyError) as error:
            self.boot_error = f"VibeWare boot unavailable: {error}"

    def observe(self):
        return self.endpoint.observe() if self.endpoint else (SimulatorSnapshot(), "")

    # self: Local route owner cancelling screensaver presentation on user navigation.
    # route: Requested setup, options or dashboard route.
    def open(self, route):
        if self.route is WifiSetupRoute.HOME_PASSWORD and route is not self.route:
            self.clear_simulated_wifi_secret()
        if route != "mascot":
            self.hide_mascot()
        elif self.route != "mascot":
            self.home_menu_since = None
        self.route = route
        self.chat_title_key = None
        self.screensaver.active = False
        self.last_navigation = time.monotonic()
        self.screensaver_idle_since = self.last_navigation

    # self: Studio navigation owner preserving the menu that opened Connections.
    def open_connections(self):
        self.connections_return = self.route
        self.open("connections")

    # self: Studio navigation owner opening the first group of visibility rows.
    def open_visibility(self):
        self.visibility_page = 0
        self.open("visibility")

    # self: Studio navigation owner.
    # previous: Actual caller when leaving the phone guide with Back.
    def open_phone_guide(self, previous):
        self.guide_return = previous
        self.open(WifiSetupRoute.PHONE_GUIDE)

    # self: Home owner opening the connectivity choices.
    def open_home_wifi(self):
        self.open(WifiSetupRoute.HOME_CHOICES)

    # self: Open the Bluetooth guide before a simulated pairing session.
    # previous: Route to restore when leaving the guide.
    def open_home_pairing(self, previous="mascot"):
        self.home_pair_return = previous
        self.open(WifiSetupRoute.HOME_INTRO)

    # self: Display the selected platform's explicitly simulated app QR.
    # apple: True selects Apple; false selects Android.
    def open_platform_qr(self, apple):
        self.home_apple_selected = apple
        self.open(WifiSetupRoute.HOME_PLATFORM_QR)

    # self: Open the synthetic network picker and begin a new simulated scan.
    def open_home_networks(self):
        self.wifi_selected_index = -1
        self.open(WifiSetupRoute.HOME_NETWORKS)
        self.scan_simulated_networks()

    # self: Begin another explicitly synthetic scan and retire the old selection.
    def scan_simulated_networks(self):
        self.wifi_networks_scanned = False
        self.wifi_scan_until = time.monotonic() + 2.4
        self.wifi_selected_index = -1
        self.wifi_network_scroll_elapsed = 0.0
        self.wifi_network_scroll_last_draw = None

    # self: Drop the transient draft when its keyboard is hidden or dismissed.
    def clear_simulated_wifi_secret(self):
        self.wifi_password = ""
        self.wifi_key_reveal = False
        self.wifi_pending_key = None
        self.wifi_pending_index = 0
        self.wifi_last_key_at = 0.0

    # index: Stable identity of the row within this synthetic scan.
    # name: A fixed demonstration network, never a detected SSID.
    def select_simulated_network(self, index, name):
        if index != self.wifi_selected_index:
            self.wifi_selected_index = index
            self.wifi_selected_network = name
            self.wifi_network_scroll_elapsed = 0.0
            self.wifi_network_scroll_last_draw = None
            return
        self.wifi_selected_index = -1
        self.clear_simulated_wifi_secret()
        self.wifi_key_mode = "letters"
        self.wifi_key_uppercase = False
        self.open(WifiSetupRoute.HOME_PASSWORD)

    # key: Same three-column multitap keyboard positions as CODY_text_entry.
    def activate_simulated_wifi_key(self, key):
        now = time.monotonic()
        if key < 9:
            group = SIMULATED_WIFI_KEYS[self.wifi_key_mode][key]
            cycle = (len(group) > 1 and self.wifi_pending_key == key and self.wifi_password and
                     now - self.wifi_last_key_at <= 1.0)
            if not cycle and len(self.wifi_password) >= 64:
                self.wifi_pending_key = None
                self.wifi_pending_index = 0
                return
            self.wifi_pending_index = (self.wifi_pending_index + 1) % len(group) if cycle else 0
            character = group[self.wifi_pending_index]
            if self.wifi_key_uppercase and self.wifi_key_mode == "letters":
                character = character.upper()
            self.wifi_password = (self.wifi_password[:-1] if cycle else self.wifi_password) + character
            self.wifi_pending_key = key if len(group) > 1 else None
        else:
            self.wifi_pending_key = None
            if key == 9:
                if self.wifi_key_mode == "letters":
                    self.wifi_key_uppercase = not self.wifi_key_uppercase
                else:
                    self.wifi_key_mode = "symbols" if self.wifi_key_mode == "numbers" else "numbers"
            elif key == 10 and len(self.wifi_password) < 64:
                self.wifi_password += " "
            elif key == 11:
                self.wifi_password = self.wifi_password[:-1]
            elif key == 12:
                self.wifi_key_mode = "numbers" if self.wifi_key_mode == "letters" else "letters"
            elif key == 13:
                self.wifi_key_reveal = not self.wifi_key_reveal
            elif key == 14 and self.wifi_password:
                self.connect_simulated_network()
        self.wifi_last_key_at = now

    # self: Update only the optional loopback state; never send or persist the draft.
    def connect_simulated_network(self):
        try:
            if self.endpoint is not None:
                self.endpoint.act("network", True)
                self.wifi_result = "Loopback enabled"
            else:
                self.wifi_result = "Preview only"
        finally:
            self.clear_simulated_wifi_secret()
        self.open(WifiSetupRoute.HOME_NETWORK_RESULT)

    # self: Studio navigation owner opening the physical-only pairing preview.
    def open_pairing_preview(self):
        self.pairing_return = self.route
        self.open(WifiSetupRoute.SCAN_TO_PAIR)

    # self: Active Studio preview receiving the BOOT shortcut.
    def options(self):
        if self.boot is None and not self.standby and not self.event_visible:
            self.show_options_page(0)

    # self: Studio navigation owner.
    # page: Options menu page reached by the lateral arrows or BOOT.
    def show_options_page(self, page):
        self.options_page = page
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()
        self.open("options")

    # self: Studio navigation owner opening the guide from Options page 3.
    def open_quick_guide(self):
        self.quick_guide_page = 0
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()
        self.open("quick_guide")

    # self: Studio navigation owner retaining the current guide page.
    # step: Direction selected by a lateral arrow.
    def move_quick_guide(self, step):
        page = self.quick_guide_page + step
        if page < 0:
            self.show_options_page(OPTIONS_PAGE_COUNT - 1)
        elif page < len(QUICK_GUIDE):
            self.quick_guide_page = page
            self.touch = None
            self.targets.clear()
            self.acknowledged = ()
            self.last_navigation = time.monotonic()

    # self: Studio navigation owner retaining the actual Options or dashboard caller.
    def open_local_timer(self):
        self.timer_return = self.route
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()
        self.open("timer")

    # self: Preview power owner; standby cancels screensaver without changing its preference.
    def toggle_standby(self):
        if self.boot is not None:
            return
        self.standby = not self.standby
        if self.standby:
            if self.route is WifiSetupRoute.HOME_PASSWORD:
                self.clear_simulated_wifi_secret()
            self.hide_mascot()
        if self.standby and self.events:
            self.events.hide()
        self.screensaver.active = False
        self.touch = None
        self.last_navigation = time.monotonic()
        self.screensaver_idle_since = self.last_navigation

    # self: Preview owner applying the same first-visible-statistics shortcut as firmware.
    def shake(self):
        if self.boot is not None:
            return
        if self.shake_to_wake:
            self.toggle_standby()
            return
        if not self.standby:
            self.last_navigation = time.monotonic()
        if not self.standby and not self.event_visible and self.route == "widgets":
            self.open_statistics()

    # self: SIMULATED wake-only shortcut; it never opens a microphone or starts recording.
    def simulate_sophia(self):
        if self.boot is None and self.standby:
            self.toggle_standby()

    # self: SIMULATED device owner accepting an ideal cardinal clockwise turn.
    def turn_device(self):
        if self.boot is not None or self.standby:
            return
        self.last_navigation = time.monotonic()
        self.device_rotation = (self.device_rotation + 90) % 360
        if self.auto_rotate:
            self.rotation = (-self.device_rotation) % 360
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()

    # self: SIMULATED source owner; no physical sensor or device is contacted.
    # direction: Unit direction in the host screen's physical coordinate frame.
    def simulate_motion(self, direction):
        if self.boot is not None or self.standby:
            return
        self.last_navigation = time.monotonic()
        if self.route == "mascot":
            self.motion_direction = direction
            self.motion_until = self.last_navigation + 0.45

    # self: Local navigation owner retaining the typed Info subpage.
    # step: Previous (-1) or next (1) action admitted by a visible arrow.
    def move(self, step):
        if self.route == "visibility":
            if step < 0 and self.visibility_page == 0:
                self.back()
                return
            self.visibility_page = min(VISIBILITY_PAGE_COUNT - 1, self.visibility_page + step)
        else:
            if PAGES[self.page] == "About":
                if self.info.move(step):
                    self.touch = None
                    self.last_navigation = time.monotonic()
                    return
                if step > 0:
                    return
            choices = self.available_pages
            if self.page in choices:
                self.page = choices[(choices.index(self.page) + step) % len(choices)]
            self.chat_title_key = None
            self.info.page = InfoPage.SYSTEM
            self.touch = None
        self.last_navigation = time.monotonic()

    # self: Active Studio preview returning to the calling route or menu page.
    def back(self):
        if self.boot is None and not self.standby and not self.event_visible:
            if self.route == "timer" and self.timer_return == "widgets":
                self.touch = None
                self.targets.clear()
                self.acknowledged = ()
                self.open("widgets")
            elif self.route == "connections":
                if self.connections_return == "options":
                    self.show_options_page(self.options_page)
                else:
                    self.open(self.connections_return)
            elif self.route in ("visibility", "display", "behavior", "restart", "factory_reset", "timer"):
                self.show_options_page(self.options_page)
            elif self.route == "quick_guide":
                self.move_quick_guide(-1)
            elif self.route == "more_behavior":
                self.open("behavior")
            elif self.route is WifiSetupRoute.HOME_CHOICES:
                self.open("mascot")
            elif self.route is WifiSetupRoute.HOME_INTRO:
                self.open(self.home_pair_return)
            elif self.route is WifiSetupRoute.HOME_PLATFORM_QR:
                self.open(WifiSetupRoute.HOME_INTRO)
            elif self.route is WifiSetupRoute.HOME_PAIR:
                self.open(self.home_pair_return)
            elif self.route is WifiSetupRoute.HOME_NETWORKS:
                self.open(WifiSetupRoute.HOME_CHOICES)
            elif self.route is WifiSetupRoute.HOME_PASSWORD:
                self.open(WifiSetupRoute.HOME_NETWORKS)
            elif self.route is WifiSetupRoute.HOME_NETWORK_RESULT:
                self.open(WifiSetupRoute.HOME_CHOICES)
            elif self.route is WifiSetupRoute.PHONE_GUIDE:
                self.open(self.guide_return)
            elif self.route is WifiSetupRoute.INSTALL_APP:
                self.open(WifiSetupRoute.PHONE_GUIDE)
            elif self.route is WifiSetupRoute.SCAN_TO_PAIR:
                self.open(self.pairing_return)
            elif self.route == "options":
                if self.options_page:
                    self.show_options_page(self.options_page - 1)
                else:
                    self.open("mascot")
            elif self.route == "widgets" and PAGES[self.page] == "About":
                self.move(-1)

    def pair(self, snapshot):
        if self.endpoint is None:
            return
        if snapshot.retained_pairing and not messagebox.askyesno(
            "Replace SIMULATED pairing", "Keep the old link until the replacement is confirmed?", parent=self.root
        ):
            return
        self.endpoint.act("pair", snapshot.retained_pairing)
        self.pairing_attempted = True

    def cancel_pairing(self):
        self.endpoint.act("cancel")
        self.pairing_attempted = False

    # self: Studio navigation owner deferring a saved link without cancelling discovery.
    def leave_pairing_wait(self):
        self.pairing_attempted = False
        self.open("widgets")

    # self: Preview host controlling only its isolated loopback endpoint.
    # action: Host-only simulated network toggle or confirmed pairing removal.
    def manage_simulated_link(self, action):
        if self.endpoint is None:
            return
        try:
            snapshot, code = self.observe()
            if code or snapshot.pending_pairing:
                return
            if action == "network":
                self.endpoint.act("network", not snapshot.network_ready)
            elif action == "forget" and snapshot.retained_pairing and messagebox.askyesno(
                "Forget SIMULATED pairing", "Forget the saved link?", parent=self.root
            ):
                self.endpoint.act("forget", True)
        except (OSError, RuntimeError, ValueError) as error:
            messagebox.showerror("SIMULATED Cody", str(error), parent=self.root)

    # self: Confirmed restart owner for this Studio window and its isolated loopback link.
    # Raises the original endpoint failure before restarting the preview.
    def restart_device(self):
        route = "widgets"
        pending_pairing = False
        if self.endpoint is not None:
            events = EventView()
            self.endpoint.act("restart", True)
            self.events = events
            snapshot, _ = self.endpoint.observe()
            pending_pairing = snapshot.pending_pairing
            if pending_pairing or (snapshot.network_ready and not snapshot.retained_pairing):
                route = "connections"
            elif not snapshot.network_ready:
                route = WifiSetupRoute.OFFLINE
        self.local_timer = LocalTimerModel()
        self.timer_return = "options"
        self.info = SystemInfoModel()
        self.reboot_preview(route, pending_pairing)

    # self: The confirmed reset owns only this Studio window and its isolated store.
    # Raises the original endpoint or storage failure before resetting the preview.
    def reset_device(self):
        if self.endpoint is not None:
            events = EventView()
            self.endpoint.act("factory_reset", True)
            self.events = events
        self.restore_preview_defaults()
        self.page = 0
        self.reboot_preview(WifiSetupRoute.NOT_CONNECTED)

    # self: Preview boot owner resetting only transient navigation and input state.
    # route: Startup route selected from the actual post-reset preview state.
    # pending_pairing: Whether a durable commissioning candidate survived restart.
    def reboot_preview(self, route, pending_pairing=False):
        self.visibility_page = self.options_page = 0
        self.pairing_attempted = pending_pairing
        self.event_visible = self.standby = False
        self.connections_return = "widgets"
        self.guide_return = WifiSetupRoute.NOT_CONNECTED
        self.pairing_return = WifiSetupRoute.PHONE_GUIDE
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()
        self.started_at = time.monotonic()
        self.open(route)
        self.start_boot()

    # self: Preview owner returning to the first enabled statistics page with data.
    def open_statistics(self):
        self.page = next((index for index in self.available_pages if PAGES[index] not in {"Device", "About"}), 0)
        self.info.page = InfoPage.SYSTEM
        self.screensaver.active = False
        self.touch = None
        self.open("widgets")

    # self: Preview owner applying visibility while retaining valid top-level and child routes.
    # page: Configurable dashboard identity from the selected row.
    def change_visibility(self, page):
        if page in self.visible:
            self.visible.remove(page)
            if PAGES[self.page] == page:
                self.page = 0
                self.info.page = InfoPage.SYSTEM
        else:
            self.visible.add(page)

    def change_interval(self):
        self.history_window = (self.history_window + 1) % len(HISTORY_WINDOWS)

    def change_brightness(self):
        self.brightness = self.brightness - 10 if self.brightness > 10 else 100

    # self: Simulated manual orientation, admitted only while automatic rotation is off.
    def change_orientation(self):
        if self.auto_rotate:
            return
        self.rotation = 0 if self.rotation == 180 else 180
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()

    # self: Simulated automatic rotation; enabling it aligns the current ideal cardinal pose.
    def change_auto_rotation(self):
        self.auto_rotate = not self.auto_rotate
        if self.auto_rotate:
            self.rotation = (-self.device_rotation) % 360
        self.touch = None
        self.targets.clear()
        self.acknowledged = ()

    # self: Current simulated panel transform composed with the physical device turn.
    def displayed_rotation(self):
        return (self.rotation + self.device_rotation) % 360

    def change_shake_to_wake(self):
        self.shake_to_wake = not self.shake_to_wake

    # self: Studio frame owner registering touch targets.
    # draw: Logical framebuffer before rotation.
    # name: Caption and identity of the touch target.
    # top: Logical y coordinate of the target.
    # action: Callback when the button is enabled.
    # width: Complete hit width in logical pixels.
    # height: Complete hit height in logical pixels.
    # secondary: Use the neutral gradient for a dismiss or postpone action.
    # destructive: Use the confirmation palette for an irreversible local action.
    # icon: Optional outline icon next to the caption.
    # caption_size: Font size chosen for the complete target.
    # center_x: Horizontal center of this button, including side-by-side actions.
    # palette: Optional explicit semantic color set for the new Wi-Fi journey.
    def button(self, draw, name, top, action, *, width=246, height=38, secondary=False,
               destructive=False, icon=None, caption_size=17, center_x=CENTER, palette=None):
        box = (center_x - width // 2, top, center_x + width // 2, top + height)
        palette = palette or (DESTRUCTIVE_CONTROL_PALETTE if destructive else (
            SECONDARY_CONTROL_PALETTE if secondary else CONTROL_PALETTE))
        color = FOREGROUND if action else MUTED
        draw_control_pill(draw, box, palette)
        caption_x = center_x
        if icon:
            text_width = round(draw.textlength(name, font=font(caption_size)))
            group_left = center_x - (text_width + 32) // 2
            trailing = icon == "chevron"
            icon_x = group_left + (text_width + 21 if trailing else 11)
            setup_icon(draw, icon, (icon_x, top + height // 2), 22, palette[3] if action else MUTED)
            caption_x += -16 if trailing else 16
        label(draw, name, top + (height - caption_size) // 2, caption_size, color, x=caption_x, width=width - 20)
        if action is not None:
            self.targets[name] = (box, action)

    # self: Studio frame owner registering touch targets.
    # draw: Logical canvas before rotation.
    # side: Negative for Back and positive for Next.
    # action: Callback while the control admits touch.
    # opacity: Arrow fade fraction.
    def sidebar(self, draw, side, action, opacity=1):
        x, y = LEFT if side < 0 else RIGHT
        arrow(draw, (x, y), side, opacity)
        if action is not None:
            self.targets[side] = ((x - 30, y - 30, x + 30, y + 30), action)

    # self: Studio preview retaining the last navigation interaction.
    # now: Monotonic frame time.
    def navigation_opacity(self, now):
        remaining = NAVIGATION_IDLE_SECONDS + NAVIGATION_FADE_SECONDS - (now - self.last_navigation)
        return max(0, min(1, remaining / NAVIGATION_FADE_SECONDS))

    # self: Studio frame owner registering one complete icon-and-label touch target.
    # draw: Logical canvas before rotation.
    # x: Logical column center for the icon, caption and hit rectangle.
    # lines: Two centered lines of the action caption.
    # icon: Outline glyph above the caption.
    # color: Accent color for the icon.
    # action: Callback when the action is available.
    def draw_connection_action(self, draw, x, lines, icon, color, action):
        setup_icon(draw, icon, (x, 219), 54, color if action else MUTED)
        caption_color = FOREGROUND if action else MUTED
        label(draw, lines[0], 264, 26, caption_color, x=x, width=140)
        label(draw, lines[1], 296, 26, caption_color, x=x, width=140)
        if action is not None:
            self.targets[" ".join(lines)] = ((x - 70, 170, x + 70, 338), action)

    # self: Studio preview owning CCP and the local pairing guide.
    # draw: Logical canvas before rotation.
    # snapshot: Current authenticated loopback projection, if available.
    # code: Active simulated Codex pairing code, if any.
    # now: Monotonic frame time for the quiet link pulse.
    def draw_connections(self, draw, snapshot, code, now):
        colors = ((98, 105, 106), (140, 150, 165), (181, 182, 222), (140, 150, 165))
        link_pulse = colors[int(now / 0.4) % len(colors)]
        if code or snapshot.pending_pairing:
            label(draw, "Pair Codex", 88, 34)
            setup_icon(draw, "link", (CENTER, 176), 56, SETUP_ACCENT if code else link_pulse)
            if code:
                label(draw, code, 236, 40, ACCENT)
                label(draw, "Enter this SIMULATED code", 292, 18)
                label(draw, "in the Cody plugin", 320, 18, MUTED)
            else:
                label(draw, "Awaiting confirmation", 256, 18)
                label(draw, "Keep Codex open", 284, 18, MUTED)
            self.button(draw, "Cancel", 390, self.cancel_pairing, width=220, height=44, secondary=True)
            return
        connect = (lambda: self.pair(snapshot)) if self.endpoint and snapshot.network_ready else None
        if self.pairing_attempted and snapshot.state in (LinkState.PAIRED, LinkState.DISCOVERING):
            label(draw, "Waiting for PC", 88, 34)
            setup_icon(draw, "link", (CENTER, 176), 56, link_pulse if snapshot.network_ready else MUTED)
            label(draw, "Open Codex on your PC" if snapshot.network_ready else "Wi-Fi unavailable", 256, 18)
            label(draw, "Pairing stays saved", 284, 18, MUTED)
            self.button(draw, "Later", 352, self.leave_pairing_wait, width=240, height=72, secondary=True)
            return
        if self.pairing_attempted and snapshot.state is LinkState.TIMED_OUT:
            label(draw, "Try again", 88, 34)
            setup_icon(draw, "retry", (CENTER, 176), 56, SETUP_AMBER)
            label(draw, "Pairing code expired", 256, 18, MUTED)
            self.button(draw, "Retry", 352, connect, width=240, height=72)
            self.sidebar(draw, -1, self.back)
            return
        label(draw, "Connections", OPTIONS_TITLE_TOP, 34)
        self.draw_connection_action(draw, 148, ("Connect", "Codex"), "link",
                                    CODEX_CONNECTION_COLOR, connect)
        self.draw_connection_action(draw, 318, ("Set up", "Wi-Fi"), "wifi",
                                    WIFI_CONNECTION_COLOR, lambda: self.open_phone_guide("connections"))
        self.sidebar(draw, -1, self.back)

    # self: Synthetic status chrome; host clock and loopback link are never firmware observations.
    # image: Logical framebuffer receiving the compact toolbar before rotation.
    # snapshot: Explicitly simulated loopback state.
    # now: Monotonic time for the visibility fade.
    def draw_status(self, image, snapshot, now):
        opacity = self.navigation_opacity(now)
        if opacity <= 0:
            return
        # Blend the chrome over the actual pixels of every scene, including the globe.
        layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        wall = time.localtime()
        draw.text((122, 31), time.strftime("%a %d %b %Y", wall).upper(), font=font(10), fill=(*MUTED, 255))
        draw.text((122, 45), time.strftime("%H:%M", wall), font=font(15), fill=(*FOREGROUND, 255))
        # The loopback has no measured RSSI, BLE telemetry or physical battery.
        setup_icon(draw, "wifi", (236, 53), 20, (*MUTED, 255))
        setup_icon(draw, "bluetooth", (266, 53), 18, (*MUTED, 255))
        draw.rounded_rectangle((287, 46, 311, 60), radius=2, outline=(*MUTED, 255), width=2)
        draw.rectangle((312, 50, 314, 56), fill=(*MUTED, 255))
        if self.show_battery_percentage:
            draw.text((319, 47), "N/A", font=font(10), fill=(*MUTED, 255))
        layer.putalpha(layer.getchannel("A").point(lambda alpha: round(alpha * opacity)))
        image.paste(layer, (0, 0), layer)

    # draw: Simulated scan and network rows use the requested solid violet fill.
    # name: Complete network identifier or action caption.
    # top: Logical top of the row.
    # action: Callback for a complete touch.
    # height: Logical row height.
    # rssi: Synthetic signal strength, absent for the scan action.
    # selected: Whether this row owns the full scrolling identity.
    # now: Monotonic frame time for the selected row.
    # target: Unique synthetic row identity, independent of the SSID text.
    def simulated_network_button(self, draw, name, top, action, height=44, rssi=None,
                                 selected=False, now=0.0, target=None):
        box = (93, top, 373, top + height)
        draw.rounded_rectangle(box, radius=14, fill=(107, 28, 173),
                               outline=FOREGROUND if selected else None, width=2)
        if rssi is None:
            label(draw, name, top + (height - 18) // 2, 18, FOREGROUND, width=256)
        elif selected:
            self.draw_selected_wifi_network(draw, now, top + 7)
        else:
            shown = name
            while shown and draw.textlength(shown, font=font(15)) > 174:
                shown = shown[:-1]
            if shown != name:
                while shown and draw.textlength(shown + "...", font=font(15)) > 174:
                    shown = shown[:-1]
                shown += "..."
            draw.text((107, top + height // 2), shown, font=font(15), fill=FOREGROUND, anchor="lm")
            draw.text((359, top + height // 2), f"{rssi} dBm", font=font(15), fill=FOREGROUND, anchor="rm")
        self.targets[target if target is not None else name] = (box, action)

    # draw: Frame receiving the selected network's clipped text.
    # now: Monotonic frame time; selection starts a new reading cycle.
    # top: Logical top of the selected row's text viewport.
    def draw_selected_wifi_network(self, draw, now, top):
        # Studio's fixture contains only an SSID and synthetic RSSI, no AP identity.
        text = self.wifi_selected_network + "  SIMULATED AP ID unavailable"
        if self.wifi_network_scroll_last_draw is not None:
            delta = max(0.0, now - self.wifi_network_scroll_last_draw)
            # Hidden routes are not drawn; do not advance their animation on return.
            if delta <= 0.25:
                self.wifi_network_scroll_elapsed += delta
        self.wifi_network_scroll_last_draw = now
        face = font(18)
        width = 256
        travel = max(0, round(draw.textlength(text, font=face)) - width)
        if travel == 0:
            label(draw, text, top, 18, FOREGROUND, width=width)
            return
        # Keep both ends readable, then move back rather than jumping to the start.
        start_hold = 1.2
        end_hold = 1.8
        speed = 20.0
        movement = travel / speed
        elapsed = self.wifi_network_scroll_elapsed % (start_hold + end_hold + 2 * movement)
        if elapsed < start_hold:
            offset = 0
        elif elapsed < start_hold + movement:
            offset = round((elapsed - start_hold) * speed)
        elif elapsed < start_hold + movement + end_hold:
            offset = travel
        else:
            offset = max(0, round(travel - (elapsed - start_hold - movement - end_hold) * speed))
        viewport = Image.new("L", (width, 25))
        ImageDraw.Draw(viewport).text((-offset, 0), text, font=face, fill=255, anchor="lt")
        draw.bitmap((105, top), viewport, fill=FOREGROUND)

    # draw: Studio representation of the native CODY_text_entry multitap layout.
    def draw_simulated_wifi_keyboard(self, draw):
        label(draw, "Wi-Fi password", 72, 19)
        label(draw, "Replaces saved Wi-Fi", 98, 15, MUTED, width=300)
        field = (95, 125, 371, 156)
        draw_control_pill(draw, field, ((24, 24, 49), (8, 8, 16), (74, 113, 222), FOREGROUND), 14)
        value = self.wifi_password if self.wifi_key_reveal else "*" * len(self.wifi_password)
        while value and draw.textlength(value, font=font(17)) > 250:
            value = value[1:]
        label(draw, value if value else "Enter password", 131, 17,
              FOREGROUND if value else MUTED, width=250)
        mode = self.wifi_key_mode
        groups = SIMULATED_WIFI_KEYS[mode]
        for key in range(15):
            if key < 12:
                left, top, width, height = 73 + (key % 3) * 110, 160 + (key // 3) * 45, 100, 39
            else:
                left, top, width, height = 104 + (key - 12) * 88, 340, 82, 43
            box = (left, top, left + width, top + height)
            draw.rounded_rectangle(box, radius=12, fill=(107, 28, 173))
            if key < 9:
                caption = groups[key].upper() if mode == "letters" and self.wifi_key_uppercase else groups[key]
            else:
                caption = ("Shift" if mode == "letters" else "#+=" if mode == "numbers" else "123") if key == 9 else (
                    "Space" if key == 10 else "Del" if key == 11 else "123" if key == 12 and mode == "letters" else
                    "ABC" if key == 12 else "Hide" if key == 13 and self.wifi_key_reveal else
                    "Show" if key == 13 else "Connect")
            label(draw, caption, top + (height - 15) // 2, 15, FOREGROUND, x=left + width // 2, width=width - 6)
            self.targets[f"wifi-key-{key}"] = (box, lambda selected=key: self.activate_simulated_wifi_key(selected))

    # self: Preview owner drawing the same Wi-Fi menu as the native device.
    # image: Logical framebuffer before rotation.
    # draw: Drawing context for the device name.
    # Exception: Preserves native preview or bundled font failures.
    def draw_home_wifi_choices(self, image, draw):
        title = setup_wordmark("SET UP WI-FI", 24, 21)
        image.paste(title, (CENTER - title.width // 2, 84), title)
        draw.text((CENTER, 118), self.simulated_ble_name, font=arcade_font(18),
                  fill=(216, 199, 247), anchor="mt")

        self.prepare_setup_mascot()
        image.paste(self.setup_mascot, (169, 150), self.setup_mascot)

        choices = (
            ("PAIR WITH BLUETOOTH", "bluetooth", (73, 282, 393, 330),
             lambda: self.open_home_pairing(WifiSetupRoute.HOME_CHOICES)),
            ("SETUP WI-FI", "wifi", (83, 337, 383, 385), self.open_home_networks),
        )
        for caption, icon, bounds, action in choices:
            left, top, right, bottom = bounds
            mark = setup_wordmark(caption, 24, 14, icon)
            image.paste(mark, ((left + right - mark.width) // 2, (top + bottom - mark.height) // 2), mark)
            self.targets[caption] = (bounds, action)

    # self: Paint the Home-launched Wi-Fi route without starting real Bluetooth.
    # image: Logical framebuffer receiving composited artwork.
    # draw: Logical framebuffer drawing context.
    # now: Monotonic clock for Back/Home visibility.
    # Exception: Preserves native preview or bundled font failures.
    def draw_home_wifi_setup(self, image, draw, now):
        if self.route is WifiSetupRoute.HOME_CHOICES:
            self.draw_home_wifi_choices(image, draw)
        elif self.route is WifiSetupRoute.HOME_INTRO:
            label(draw, "Bluetooth setup", 82, 30)
            for top, text in ((140, "1. Get Espressif app"), (180, "2. Scan Cody QR in app"),
                              (220, "3. Choose your Wi-Fi"), (270, "TAP BELOW TO CHOOSE")):
                label(draw, text, top, 19)
            apple = ("......##....", ".....##.....", "............", "...###.###..", ".##########.",
                     "#########...", "########....", "#########...", ".##########.", ".##########.",
                     "..########..", "...###.##...")
            android = ("..#......#..", "...#....#...", "...######...", "..########..", "..##.##.##..",
                       "..########..", "............", "#.########.#", "#.########.#", "#.########.#",
                       "..########..", "...##..##...")
            colors = ("#00ff00", "#ffff00", "#ffaa00", "#ff0000", "#aa00ff", "#0055ff")
            for left, glyph, platform in ((143, apple, True), (275, android, False)):
                for y, row in enumerate(glyph):
                    for x, pixel in enumerate(row):
                        if pixel == "#":
                            draw.rectangle((left + x * 4, 316 + y * 4, left + x * 4 + 3, 319 + y * 4),
                                           fill=colors[y // 2] if platform else "#00ff00")
            self.targets["Apple"] = ((112, 305, 222, 375), lambda: self.open_platform_qr(True))
            self.targets["Android"] = ((244, 305, 354, 375), lambda: self.open_platform_qr(False))
        elif self.route is WifiSetupRoute.HOME_PLATFORM_QR:
            label(draw, "Get the app", 82, 30)
            label(draw, "SIMULATED - app QR", 130, 17, MUTED)
            matrix = store_qr_matrix("SIMULATED-APP-QR-NOT-A-DEVICE")
            if matrix is not None:
                qr_tile(draw, matrix, 205, 168)
            if self.navigation_opacity(now) == 1:
                self.sidebar(draw, 1, lambda: self.open(WifiSetupRoute.HOME_PAIR), 1)
        elif self.route is WifiSetupRoute.HOME_PAIR:
            label(draw, "Pair Cody", 82, 30)
            label(draw, "SIMULATED - not a device", 130, 17, MUTED)
            matrix = store_qr_matrix("SIMULATED-CODY-NOT-A-DEVICE")
            if matrix is None:
                label(draw, "QR preview unavailable", 240, 17, MUTED)
            else:
                qr_tile(draw, matrix, 164, 192)
        elif self.route is WifiSetupRoute.HOME_NETWORKS:
            label(draw, "Wi-Fi networks", 75, 27)
            label(draw, "Replaces saved Wi-Fi", 106, 15, MUTED)
            searching = now < self.wifi_scan_until
            if not searching and self.wifi_scan_until:
                self.wifi_scan_until = 0.0
                self.wifi_networks_scanned = True
            if searching:
                frame = int(now * 5) % 8
                for y in range(21):
                    for x in range(14):
                        edge = 2 + (y if y <= 10 else 20 - y) // 2
                        cap = (y <= 1 or y >= 19) and 1 <= x <= 12
                        glass = 2 <= y <= 18 and x in (edge, 13 - edge)
                        sand = ((5 + frame // 2 <= y <= 8 or 17 - frame // 2 <= y <= 17)
                                and edge < x < 13 - edge)
                        falling = x == 6 + (y + frame) % 2 and 9 <= y <= 15 and (y + frame) % 3 == 0
                        if cap or glass or sand or falling:
                            color = (173, 179, 255) if cap else (107, 190, 255) if glass else (255, 213, 0)
                            draw.rectangle((205 + 4 * x, 174 + 4 * y, 208 + 4 * x, 177 + 4 * y), fill=color)
                label(draw, "Searching...", 270, 17)
                label(draw, "SIMULATED", 305, 13, MUTED)
            else:
                label(draw, "Tap again to select" if self.wifi_selected_index >= 0 else
                      "SIMULATED - no radio scan", 132, 15, MUTED)
            if self.wifi_networks_scanned and not searching:
                for index, (name, rssi) in enumerate((("SIMULATED Home", -42),
                                                       ("SIMULATED Office", -63),
                                                       ("SIMULATED Guest", -78))):
                    self.simulated_network_button(draw, name, 160 + index * 45,
                        lambda selected_index=index, selected=name:
                            self.select_simulated_network(selected_index, selected),
                        39, rssi, index == self.wifi_selected_index, now, f"wifi-network-{index}")
            if not searching:
                self.simulated_network_button(draw, "Scan for networks", 342,
                                              self.scan_simulated_networks, 38)
        elif self.route is WifiSetupRoute.HOME_PASSWORD:
            self.draw_simulated_wifi_keyboard(draw)
        elif self.route is WifiSetupRoute.HOME_NETWORK_RESULT:
            label(draw, "SIMULATED Wi-Fi", 82, 26)
            label(draw, self.wifi_selected_network, 154, 19, SETUP_ACCENT)
            label(draw, self.wifi_result, 200, 21)
            label(draw, "No real network was used", 243, 16, MUTED)
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview owning the current Wi-Fi setup route and its touch targets.
    # image: Logical canvas before rotation.
    # draw: Logical canvas drawing context.
    # now: Monotonic frame time for the shared Back fade.
    # Exception: Preserves native preview or bundled font failures.
    def draw_wifi_setup(self, image, draw, now):
        if self.route in (WifiSetupRoute.HOME_CHOICES, WifiSetupRoute.HOME_INTRO, WifiSetupRoute.HOME_PLATFORM_QR,
                          WifiSetupRoute.HOME_PAIR, WifiSetupRoute.HOME_NETWORKS,
                          WifiSetupRoute.HOME_PASSWORD, WifiSetupRoute.HOME_NETWORK_RESULT):
            self.draw_home_wifi_setup(image, draw, now)
            return
        if self.route in (WifiSetupRoute.OFFLINE, WifiSetupRoute.NOT_CONNECTED):
            recovering = self.route is WifiSetupRoute.OFFLINE
            draw.ellipse((191, 76, 274, 159), fill=SETUP_AMBER_FIELD)
            setup_icon(draw, "offline", (CENTER, 118), 60, SETUP_AMBER)
            label(draw, "Wi-Fi offline" if recovering else "Not connected", 180, 34)
            label(draw, "Connect Cody to Wi-Fi", 223, 17, MUTED)
            self.button(draw, "Set up Wi-Fi" if recovering else "Tap to pair", 272 if recovering else 352,
                        lambda: self.open_phone_guide(WifiSetupRoute.NOT_CONNECTED),
                        width=280 if recovering else 240, height=58 if recovering else 72,
                        icon="wifi")
            if recovering:
                self.button(draw, "Later", 350, lambda: self.open(WifiSetupRoute.NOT_CONNECTED),
                            width=160, height=50, secondary=True, icon="chevron")
            return
        if self.route is WifiSetupRoute.PHONE_GUIDE:
            label(draw, "Wi-Fi setup", 130, 32)
            label(draw, "Get the Espressif app", 186, 17, MUTED)
            label(draw, "Then open it and scan", 212, 17, MUTED)
            label(draw, "the pairing QR.", 238, 17, MUTED)
            self.button(draw, "App download QR", 274, lambda: self.open(WifiSetupRoute.INSTALL_APP),
                        width=280, height=62, icon="qr")
            self.button(draw, "Show pairing QR", 348, self.open_pairing_preview,
                        width=280, height=64, icon="bluetooth")
        elif self.route is WifiSetupRoute.INSTALL_APP:
            label(draw, "Scan to install", 98, 28)
            label(draw, "ESP BLE Provisioning", 139, 16, MUTED)
            qr_tile(draw, INSTALL_APP_QR, 165, 192)
            label(draw, "Find on the App Store", 360, 16, MUTED)
            self.button(draw, "Show pairing QR", 390, self.open_pairing_preview,
                        width=220, height=44, icon="bluetooth")
        elif self.route is WifiSetupRoute.SCAN_TO_PAIR:
            setup_icon(draw, "bluetooth", (CENTER, 121), 54, SETUP_ACCENT)
            label(draw, "Scan to pair", 180, 32)
            label(draw, "Bluetooth setup runs", 230, 17, MUTED)
            label(draw, "on the physical device.", 256, 17, MUTED)
            self.button(draw, "Cancel", 390, self.back, width=220, height=44, secondary=True)
            return
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview owning Options and its categories.
    # draw: Logical canvas before rotation.
    # rows: Visible captions and actions, with at most three entries per page.
    def draw_option_rows(self, draw, rows):
        for index, (name, action) in enumerate(rows):
            self.button(draw, name, OPTIONS_ROW_TOP + index * OPTIONS_ROW_STEP, action,
                        width=OPTIONS_ROW_WIDTH, height=OPTIONS_ROW_HEIGHT, caption_size=20)

    # self: Studio preview of the current group of three configurable screens.
    # draw: Logical canvas before rotation.
    # now: Monotonic time for the shared arrow fade.
    def draw_visibility(self, draw, now):
        label(draw, "Visible screens", OPTIONS_TITLE_TOP, 34)
        start = 1 + self.visibility_page * VISIBILITY_ROWS
        for index, page in enumerate(PAGES[start:start + VISIBILITY_ROWS]):
            visible = page in self.visible
            top = OPTIONS_ROW_TOP + index * OPTIONS_ROW_STEP
            box = (93, top, 373, top + OPTIONS_ROW_HEIGHT)
            draw_control_pill(draw, box, CONTROL_PALETTE if visible else SECONDARY_CONTROL_PALETTE,
                              corner_radius=12)
            face = font(19)
            draw.text((105, top + OPTIONS_ROW_HEIGHT // 2), page, font=face, fill=FOREGROUND, anchor="lm")
            draw.text((322, top + OPTIONS_ROW_HEIGHT // 2), "Visible" if visible else "Hidden",
                      font=face, fill=WIFI_CONNECTION_COLOR if visible else MUTED, anchor="mm")
            self.targets[page] = (box, lambda selected=page: self.change_visibility(selected))
        label(draw, f"{self.visibility_page + 1}/{VISIBILITY_PAGE_COUNT}", 375, 19, MUTED)
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, (lambda: self.move(-1)) if opacity == 1 else None, opacity)
            if self.visibility_page + 1 < VISIBILITY_PAGE_COUNT:
                self.sidebar(draw, 1, (lambda: self.move(1)) if opacity == 1 else None, opacity)

    # self: Studio preview owning the active menu page and touch targets.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time for the lateral arrow fade.
    def draw_options(self, draw, now):
        label(draw, f"Options {self.options_page + 1}/{OPTIONS_PAGE_COUNT}", OPTIONS_TITLE_TOP, 34)
        if self.options_page == 0:
            rows = (
                ("Connections", self.open_connections),
                ("Codex stats", self.open_statistics),
                ("Timer", self.open_local_timer),
            )
        elif self.options_page == 1:
            rows = (
                ("Visible screens", self.open_visibility),
                ("Display", lambda: self.open("display")),
                ("Behavior", lambda: self.open("behavior")),
            )
        else:
            rows = (
                ("Restart device", lambda: self.open("restart")),
                ("Factory reset", lambda: self.open("factory_reset")),
                ("Quick guide", self.open_quick_guide),
            )
        self.draw_option_rows(draw, rows)
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)
            if self.options_page < OPTIONS_PAGE_COUNT - 1:
                next_page = (lambda: self.show_options_page(self.options_page + 1)) if opacity == 1 else None
                self.sidebar(draw, 1, next_page, opacity)

    # self: Studio preview of the three fixed guide pages.
    # draw: Logical canvas before rotation.
    def draw_quick_guide(self, draw):
        title, accent, rows, note = QUICK_GUIDE[self.quick_guide_page]
        label(draw, title, OPTIONS_TITLE_TOP, 34, accent)
        for index, (action, detail) in enumerate(rows):
            label(draw, action, 132 + index * 76, 36, accent)
            label(draw, detail, 175 + index * 76, 19, MUTED)
        if note is not None:
            label(draw, note, 357, 15, MUTED)
        last_page = self.quick_guide_page == len(QUICK_GUIDE) - 1
        self.sidebar(draw, -1, self.back)
        if not last_page:
            self.sidebar(draw, 1, lambda: self.move_quick_guide(1))
        else:
            self.button(draw, "OK", 382, lambda: self.show_options_page(OPTIONS_PAGE_COUNT - 1),
                        width=220, height=52, caption_size=20)

    # self: Studio preview drawing its independently retained timer state.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time used for the timer digits and Back fade.
    def draw_local_timer(self, draw, now):
        timer = self.local_timer
        minutes, seconds = divmod(timer.remaining_seconds(now), 60)
        label(draw, "Timer", 93, 34, ACCENT)
        status = {
            LocalTimerPhase.IDLE: "Set duration",
            LocalTimerPhase.RUNNING: "Time remaining",
            LocalTimerPhase.FINISHED: "Time is up",
        }[timer.phase]
        finished = timer.phase is LocalTimerPhase.FINISHED
        label(draw, status, 150, 18, ACCENT if finished else MUTED)
        label(draw, f"{minutes:02d}:{seconds:02d}", 184, 40, ACCENT if finished else FOREGROUND)
        if timer.phase is LocalTimerPhase.IDLE:
            decrease = (lambda: timer.adjust(-1)) if timer.duration_minutes > TIMER_MIN_MINUTES else None
            increase = (lambda: timer.adjust(1)) if timer.duration_minutes < TIMER_MAX_MINUTES else None
            self.button(draw, "- 1 min", 247, decrease, width=154, height=60, secondary=True, center_x=150)
            self.button(draw, "+ 1 min", 247, increase, width=154, height=60, secondary=True, center_x=316)
            self.button(draw, f"Sound: {timer.sound.value}", 321, timer.cycle_sound,
                        width=154, height=60, secondary=True, center_x=150, caption_size=18)
            self.button(draw, "Start", 321, lambda: timer.start(time.monotonic()),
                        width=154, height=60, center_x=316)
        elif finished:
            self.button(draw, "OK", 321, timer.dismiss, width=240, height=60)
        else:
            self.button(draw, "Cancel", 321, timer.cancel, width=240, height=60, secondary=True)
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview routing cancellation or confirmed restart to its owner.
    # draw: Logical canvas before rotation.
    def draw_restart(self, draw):
        label(draw, "Restart device", OPTIONS_TITLE_TOP, 34)
        label(draw, "Keep Wi-Fi and settings", 155, 17, MUTED)
        label(draw, "Stops any active timer", 184, 17, MUTED)
        self.button(draw, "Cancel", 270, self.back, width=280, height=64, secondary=True, caption_size=20)
        self.button(draw, "Restart device", 382, self.restart_device, width=220, height=52, caption_size=20)
        self.sidebar(draw, -1, self.back)

    # self: Studio preview routing cancellation or an explicit reset to its owner.
    # draw: Logical canvas before rotation.
    def draw_factory_reset(self, draw):
        label(draw, "Factory reset", OPTIONS_TITLE_TOP, 34)
        label(draw, "Clear Wi-Fi, Codex", 155, 17, MUTED)
        label(draw, "link and preferences.", 184, 17, MUTED)
        label(draw, "Restart with defaults", 213, 17, MUTED)
        self.button(draw, "Cancel", 270, self.back, width=280, height=64, secondary=True, caption_size=20)
        self.button(draw, "Reset device", 382, self.reset_device, width=220, height=52,
                    destructive=True, caption_size=20)
        self.sidebar(draw, -1, self.back)

    def open_about_url(self):
        if not webbrowser.open_new_tab(ABOUT_URL):
            raise RuntimeError("Could not open the Cody project page")

    # self: Studio preview registering the project link action.
    # draw: Logical canvas before rotation.
    def draw_about_qr(self, draw):
        label(draw, ABOUT_INFO, 105, 27)
        qr_tile(draw, ABOUT_QR, 147, 205)
        self.button(draw, "Open link", 370, self.open_about_url, width=155, height=33)

    # self: Preview renderer borrowing the typed Info state and observation.
    # draw: Logical canvas before rotation, with central content clear of both arrows.
    def draw_system_info(self, draw):
        page = self.info.page
        if page is InfoPage.PROJECT_LINK:
            self.draw_about_qr(draw)
            return
        title, position = {
            InfoPage.SYSTEM: ("SYSTEM", "1/3"),
            InfoPage.MEMORY: ("MEMORY", "2/3"),
            InfoPage.FIRMWARE: ("FIRMWARE", "3/3"),
        }[page]
        label(draw, title, 82, 30, ACCENT, width=230)
        label(draw, position, 89, 16, MUTED, x=371, width=44)
        self.draw_info_icon(draw, page)
        draw.line((73, 125, 392, 125), fill=(62, 91, 113))
        if page is InfoPage.SYSTEM:
            for index, top in enumerate((137, 199), 1):
                draw.text((93, top), f"CPU {index}", font=font(17), fill=MUTED)
                draw.text((240, top), "Unavailable", font=font(17), fill=MUTED)
            hours, seconds = divmod(self.info.observation.studio_uptime_seconds, 3600)
            minutes, seconds = divmod(seconds, 60)
            label(draw, "STUDIO UPTIME", 263, 17, MUTED)
            label(draw, f"{hours:02d}:{minutes:02d}:{seconds:02d}", 288, 23, ACCENT)
            label(draw, "PREVIOUS: LAST OBSERVED", 328, 17, MUTED)
            label(draw, "Unavailable", 353, 17, MUTED)
        elif page is InfoPage.MEMORY:
            for caption, top in (("INTERNAL RAM", 134), ("PSRAM", 268)):
                label(draw, caption, top, 21, ACCENT)
                label(draw, "Unavailable", top + 27, 17, MUTED)
            label(draw, "Device telemetry is not simulated", 376, 14, MUTED)
        else:
            label(draw, "Firmware metadata unavailable", 206, 17, MUTED)
            label(draw, "This is a Studio preview", 240, 17, MUTED)

    # self: Preview renderer using small semantic outlines rather than action decorations.
    # draw: Logical canvas receiving the 24-pixel title icon.
    # page: One of the three informational pages.
    def draw_info_icon(self, draw, page):
        if page is InfoPage.SYSTEM:
            draw.rectangle((82, 91, 102, 111), outline=ACCENT, width=2)
            for offset in (86, 92, 98):
                draw.line((offset, 87, offset, 90), fill=ACCENT, width=2)
                draw.line((offset, 112, offset, 115), fill=ACCENT, width=2)
                draw.line((78, offset + 5, 81, offset + 5), fill=ACCENT, width=2)
                draw.line((103, offset + 5, 106, offset + 5), fill=ACCENT, width=2)
        elif page is InfoPage.MEMORY:
            draw.rectangle((79, 94, 105, 109), outline=ACCENT, width=2)
            for left in (83, 91, 99):
                draw.rectangle((left, 98, left + 3, 104), fill=ACCENT)
                draw.line((left, 110, left, 114), fill=ACCENT, width=2)
        else:
            draw.ellipse((80, 89, 104, 113), outline=ACCENT, width=2)
            draw.ellipse((88, 97, 96, 105), outline=ACCENT, width=2)
            for left, top, right, bottom in ((90, 85, 94, 90), (90, 112, 94, 117),
                                            (76, 99, 81, 103), (103, 99, 108, 103)):
                draw.rectangle((left, top, right, bottom), fill=ACCENT)

    # self: Studio owns the latest data-qualified navigation list.
    # snapshot: Current loopback telemetry. Account observations are not implemented in Studio.
    def update_available_pages(self, snapshot):
        technical = snapshot.technical if snapshot.technical_valid else None
        context = technical is not None and (
            (technical.context_capacity or 0) > 0 or technical.compaction.value in {
                "active", "refreshed", "orphaned", "interrupted",
            })
        supported = {"Device", "About"}
        if context:
            supported.add("Context")
        self.available_pages = tuple(index for index, page in enumerate(PAGES)
                                     if page in self.visible and page in supported)
        if self.route == "widgets" and self.page not in self.available_pages:
            self.page = 0
            self.info.page = InfoPage.SYSTEM
            self.touch = None

    # self: Display owner retaining the scroll clock for this title and page.
    # draw: Current logical canvas.
    # snapshot: Exact title and its validity and source age from CCP.
    # top: Top edge of the bounded title row.
    # now: Monotonic frame time in seconds.
    def draw_chat_title(self, draw, snapshot, top, now):
        current = (snapshot.activity_valid and snapshot.title_state == "current" and snapshot.title_error == 0
                   and snapshot.title_age_ms is not None and 0 <= snapshot.title_age_ms < 45000 and snapshot.title)
        title = snapshot.title if current else "Chat title unavailable"
        key = (self.page, snapshot.chat, title)
        if key != self.chat_title_key:
            self.chat_title_key = key
            self.chat_title_since = now
        face = font(18)
        travel = max(0, round(draw.textlength(title, font=face)) - 320)
        if not travel:
            label(draw, title, top, 18, ACCENT, width=320)
            return
        elapsed = max(0.0, now - self.chat_title_since) % (2.8 + travel * 0.016)
        offset = min(travel, int(max(0.0, elapsed - 1.2) / 0.016))
        viewport = Image.new("L", (320, 24))
        ImageDraw.Draw(viewport).text((-offset, 0), title, font=face, fill=255, anchor="lt")
        draw.bitmap((73, top), viewport, fill=ACCENT)

    # self: Studio preview of the followed chat's context.
    # draw: Logical canvas before rotation.
    # snapshot: Coherence-qualified loopback observation for the followed chat.
    # now: Monotonic frame time for the title viewport.
    def draw_context(self, draw, snapshot, now):
        self.draw_chat_title(draw, snapshot, 128, now)
        technical = snapshot.technical if snapshot.technical_valid else None
        capacity = technical.context_capacity if technical else None
        used = technical.context_used if technical else None
        compaction = None
        if technical is not None:
            state = technical.compaction.value
            if state == "active" and technical.compaction_age_ms is not None:
                age = technical.compaction_age_ms + (snapshot.technical_age_ms or 0)
                compaction = "Compacting" if age <= 120000 else "Stale"
            else:
                compaction = {
                    "refreshed": "Context refreshed", "orphaned": "Partially observed",
                    "interrupted": "Interrupted", "stale": "Stale",
                }.get(state)
        if capacity and used is not None and 0 <= used <= capacity:
            remaining = capacity - used
            label(draw, "TOKENS REMAINING", 154, 18)
            label(draw, f"{remaining:,}", 177, 45, ACCENT)
            draw.rectangle((93, 244, 372, 250), fill=(24, 34, 48))
            filled = remaining * 280 // capacity
            if filled:
                draw.rectangle((93, 244, 92 + filled, 250), fill=ACCENT)
        else:
            top = 178 if capacity or compaction else 207
            label(draw, "Context over limit" if capacity and used is not None and used > capacity
                  else "Context data unavailable", top, 20)
        if capacity:
            draw.line((93, 259, 373, 259), fill=CONTROL_PALETTE[2])
            label(draw, "CONTEXT WINDOW", 266, 18)
            label(draw, f"{capacity:,}", 291, 28)
        if compaction:
            top = 335 if capacity else 259
            draw.line((93, top, 373, top), fill=CONTROL_PALETTE[2])
            label(draw, "COMPACTION", top + 3, 18, MUTED)
            label(draw, compaction, top + 30, 20, ACCENT)

    # self: Studio owns the current loopback endpoint and Options shortcut.
    # draw: Logical canvas before display rotation.
    # snapshot: Current SIMULATED connection and authenticated activity projection.
    # now: Monotonic frame time for the title viewport.
    def draw_device_summary(self, draw, snapshot, now):
        label(draw, "Device", 86, 34)
        observed = snapshot.source == "SIMULATED" and snapshot.state is not LinkState.STOPPED
        linked = (observed and snapshot.network_ready and snapshot.state is LinkState.CONNECTED
                  and snapshot.ready_age_ms is not None and 0 <= snapshot.ready_age_ms < 8000)
        presence = snapshot.app_presence
        codex_open = (linked and presence.valid and presence.error == 0 and presence.state.value == "open"
                      and 0 <= presence.receipt_age_ms < 8000
                      and 0 <= presence.source_age_ms + presence.receipt_age_ms < 60000)
        if not observed:
            codex, codex_color = "Unavailable", MUTED
        elif codex_open:
            codex, codex_color = "Connected", WIFI_CONNECTION_COLOR
        elif linked:
            codex, codex_color = "PC linked", SETUP_AMBER
        elif snapshot.state is LinkState.DISCOVERING:
            codex, codex_color = "Connecting", SETUP_AMBER
        elif snapshot.state is LinkState.PAIRING:
            codex, codex_color = "Pairing", SETUP_AMBER
        else:
            codex, codex_color = "Not connected", SETUP_AMBER

        if not observed:
            wifi, wifi_icon, wifi_color = "Unavailable", "wifi", MUTED
        elif snapshot.network_ready:
            wifi, wifi_icon, wifi_color = "Connected", "wifi", WIFI_CONNECTION_COLOR
        else:
            wifi, wifi_icon, wifi_color = "Offline", "offline", SETUP_AMBER

        for y in (190, 242):
            draw.line((95, y, 371, y), fill=(33, 39, 53))
        setup_icon(draw, "link", (112, 164), 28, codex_color)
        setup_icon(draw, wifi_icon, (112, 216), 28, wifi_color)
        draw.rectangle((99, 261, 122, 274), outline=MUTED, width=2)
        draw.rectangle((123, 265, 126, 270), fill=MUTED)
        for y, name, value, color in (
            (164, "Codex", codex, codex_color),
            (216, "Wi-Fi", wifi, wifi_color),
            (268, "Battery", "Not simulated", MUTED),
        ):
            draw.text((140, y), name, fill=FOREGROUND, font=font(18), anchor="lm")
            draw.text((371, y), value, fill=color, font=font(18), anchor="rm")
        if codex_open and snapshot.activity_valid:
            self.draw_chat_title(draw, snapshot, 291, now)
        self.button(draw, "Options", 330, self.options, width=260, height=50, caption_size=20)

    # self: Studio preview owning the selected dashboard page.
    # draw: Logical canvas before rotation.
    # snapshot: Current loopback projection, if available.
    # now: Monotonic frame time for uptime and arrow fading.
    def draw_widget(self, draw, snapshot, now):
        page = PAGES[self.page]
        if page in ("Codex account", "Credits", "Context", "Codex limits", "Last 7 days"):
            label(draw, page.upper(), 82, 33, ACCENT)
        elif page == "Reset countdown":
            label(draw, "Codex reset", 83, 34)
        elif page == "Token history":
            label(draw, "TOKEN ACTIVITY", 78, 33, ACCENT)
        elif page not in ("About", "Device"):
            label(draw, page, 105, 33)
        if page == "Device":
            self.draw_device_summary(draw, snapshot, now)
        elif page == "Context":
            self.draw_context(draw, snapshot, now)
        elif page == "About":
            self.draw_system_info(draw)
        elif page == "Reset countdown":
            setup_icon(draw, "clock", (CENTER, 210), 58, SETUP_AMBER)
            label(draw, "Codex usage unavailable", 263, 19, SETUP_AMBER)
        elif page == "Token history":
            setup_icon(draw, "clock", (CENTER, 192), 56, ACCENT)
            label(draw, "NO ACCOUNT SAMPLES YET", 241, 19)
            label(draw, "15 MIN ACCOUNT OBSERVATIONS", 288, 16, MUTED)
            self.button(draw, HISTORY_WINDOWS[self.history_window], 359, self.change_interval, width=220, height=50)
        elif page == "Credits":
            label(draw, "Credit data unavailable", 212, 20, MUTED)
        elif page == "Codex limits":
            label(draw, "Limits unavailable", 212, 20, MUTED)
        elif page in ("Today", "Codex account"):
            label(draw, "Account data unavailable", 213, 20, MUTED)
        elif page == "Last 7 days":
            label(draw, "Daily data unavailable", 209, 20, MUTED)
        opacity = self.navigation_opacity(now)
        if opacity > 0 and (len(self.available_pages) > 1 or page == "About"):
            self.sidebar(draw, -1, (lambda: self.move(-1)) if opacity == 1 else None, opacity)
            if page != "About" or self.info.page is not InfoPage.PROJECT_LINK:
                self.sidebar(draw, 1, (lambda: self.move(1)) if opacity == 1 else None, opacity)

    # image: Logical framebuffer receiving a scalable vector icon and gradient text.
    # top: Visible top of the text; opacity follows the shared navigation timeout.
    def draw_home_action(self, image, caption, icon, top, opacity, action):
        face = arcade_font(24)
        bounds = face.getbbox(caption)
        text_height = max(1, bounds[3] - bounds[1])
        mask = Image.new("L", image.size, 0)
        draw = ImageDraw.Draw(mask)
        text_width = round(draw.textlength(caption, font=face))
        gap = 11
        left = CENTER - (text_height + gap + text_width) // 2
        setup_icon(draw, icon, (left + text_height // 2, top + text_height // 2),
                   text_height, 255)
        draw.text((left + text_height + gap, top - bounds[1]), caption, font=face, fill=255)
        # Hard pixel edges avoid a glow or blurred type on the small display.
        mask = mask.point(lambda value: round(255 * opacity) if value >= 128 else 0)
        gradient = Image.new("RGB", image.size)
        pixels = ImageDraw.Draw(gradient)
        width = max(1, text_height + gap + text_width)
        cyan, violet, pink = (82, 212, 255), (166, 125, 255), (251, 143, 239)
        for y in range(top - text_height, top + 2 * text_height + 1):
            progress = max(0.0, min(1.0, (y - top) / max(1, text_height - 1)))
            start, end = (cyan, violet) if progress < 0.5 else (violet, pink)
            progress = progress * 2 if progress < 0.5 else (progress - 0.5) * 2
            color = tuple(round(first + (last - first) * progress)
                          for first, last in zip(start, end))
            pixels.line((left, y, left + width, y), fill=color)
        image.paste(gradient, (0, 0), mask)
        if opacity >= 0.99:
            self.targets[caption] = ((left - 12, top - 9, left + width + 12, top + text_height + 10), action)

    # self: Serialized Tk owner of the native avatar preview.
    # Exception: Preserves the native renderer loader failure.
    def avatar_preview(self):
        if self.muse is None:
            self.muse = NativeMusePreview()
            self.muse.reset(self.muse_seed)
        return self.muse

    # self: Serialized preview owner caching the menu pose before Home animation.
    # Exception: Preserves native load or draw failures.
    def prepare_setup_mascot(self):
        if self.setup_mascot is not None:
            return
        muse = self.avatar_preview()
        mascot = muse.face_image(MusePose.IDLE, 1000, 1000).resize((128, 128), Image.Resampling.NEAREST)
        red, green, blue = mascot.split()
        visible = ImageChops.lighter(red, ImageChops.lighter(green, blue))
        mascot.putalpha(visible.point(lambda value: 255 if value else 0))
        self.setup_mascot = mascot
        muse.reset(self.muse_seed)

    # self: Native avatar presentation with Tk-thread input and host-only audio.
    # image: Writable logical framebuffer before display rotation.
    # now: Current monotonic time, used for both animation and touch reaction.
    # Exception: Propagates native load or drawing errors to the preview tick.
    def draw_mascot(self, image, now):
        self.prepare_setup_mascot()
        muse = self.avatar_preview()
        if self.muse_last_frame_at is not None:
            self.muse_elapsed_ms += max(0.0, (now - self.muse_last_frame_at) * 1000)
        self.muse_last_frame_at = now
        elapsed_ms = int(self.muse_elapsed_ms)
        reaction_ms = (elapsed_ms - self.muse_reaction_ms
                       if self.muse_reaction_ms is not None else NO_REACTION)
        now_us = max(1, int(now * 1_000_000))
        if self.motion_sample_at_us == 0 or now_us - self.motion_sample_at_us >= 100_000:
            dx, dy = self.motion_direction if now < self.motion_until else (0.0, 0.0)
            # Convert the visible host direction into the physical panel frame,
            # then use the firmware's board X = panel Y and board Y = panel X.
            if self.device_rotation == 90:
                panel_x, panel_y = dy, -dx
            elif self.device_rotation == 180:
                panel_x, panel_y = -dx, -dy
            elif self.device_rotation == 270:
                panel_x, panel_y = -dy, dx
            else:
                panel_x, panel_y = dx, dy
            self.motion_acceleration = (panel_y * 4.0, panel_x * 4.0)
            self.motion_sample_at_us = now_us
        muse.motion(*self.motion_acceleration, self.motion_sample_at_us, now_us, self.rotation, True)
        image.paste((0, 0, 0), (0, 0, SIZE, SIZE))
        menu_progress = 0.0
        if self.home_menu_since is not None:
            visibility = self.navigation_opacity(now)
            if visibility <= 0:
                self.home_menu_since = None
            else:
                menu_progress = min(1.0, max(0.0, (now - self.home_menu_since) /
                                                  HOME_MENU_TRANSITION_SECONDS)) * visibility
        mascot_size = round(MUSE_SIZE * (1.0 - 0.35 * menu_progress))
        mascot_top = round(MUSE_OFFSET + 39 * menu_progress)
        mascot = muse.image(elapsed_ms, reaction_ms)
        if mascot_size != MUSE_SIZE:
            mascot = mascot.resize((mascot_size, mascot_size), Image.Resampling.NEAREST)
        image.paste(mascot, (CENTER - mascot_size // 2, mascot_top))
        draw = ImageDraw.Draw(image)
        snapshot, _ = self.observe()
        if self.endpoint is None or not snapshot.network_ready:
            opacity = max(0.0, min(1.0, (self.muse_elapsed_ms - 5500.0) / 2000.0))
            if opacity:
                purple = tuple(round(value * opacity) for value in (199, 145, 255))
                label(draw, "SET UP WI-FI", 70, 21, purple)
                label(draw, self.simulated_ble_name, 98, 19, purple)
        if menu_progress > 0:
            self.draw_home_action(image, "PAIR WITH APP", "bluetooth", 344, menu_progress,
                                  self.open_home_pairing)
            self.draw_home_action(image, "SETUP WIFI", "wifi", 382, menu_progress,
                                  self.open_home_wifi)
        # Insert the full Home target after the menu actions, which get first refusal.
        self.targets["Mascot"] = ((0, 0, SIZE, SIZE), self.pet_mascot)

    # self: Preview owner excluding hidden time and retiring the visible touch reaction.
    def hide_mascot(self):
        if self.muse is not None and self.muse_last_frame_at is not None:
            self.muse.reset_motion()
        self.muse_last_frame_at = None
        self.muse_reaction_ms = None
        self.motion_until = 0.0
        self.motion_sample_at_us = 0
        self.motion_acceleration = (0.0, 0.0)

    # self: Visible avatar receiving a complete short contact admitted by release.
    def pet_mascot(self):
        now = time.monotonic()
        if not 0.03 <= now - self.touch_pressed_at <= 0.45:
            return
        if self.home_menu_since is None:
            self.home_menu_since = now
        self.muse_reaction_ms = int(self.muse_elapsed_ms)
        if self.audio is not None:
            self.audio.touch()

    # self: Studio preview owning the rendered route and touch targets.
    # snapshot: Current loopback projection, if available.
    # code: Current SIMULATED CCP code, if a pairing attempt is active.
    # now: Monotonic frame time.
    # Exception: Propagates drawing failures to the preview tick.
    def frame(self, snapshot, code, now):
        self.update_available_pages(snapshot)
        wifi_setup = isinstance(self.route, WifiSetupRoute)
        image = Image.new("RGB", (SIZE, SIZE), BACKGROUND)
        draw = ImageDraw.Draw(image)
        self.targets = {}
        event_was_visible = self.event_visible
        self.event_visible = False
        waking = self.route == "mascot" and self.muse_elapsed_ms < 1500
        if self.mascot_was_waking and not waking and self.route == "mascot":
            # Start Home's idle period once, after the initial wake completes.
            self.screensaver_idle_since = now
        self.mascot_was_waking = waking
        event_active = (not self.standby and not waking and self.events and
                        self.events.visible(snapshot, self.endpoint))
        if self.events and not event_active:
            self.events.hide()
        if event_was_visible and not event_active:
            self.screensaver_idle_since = now
        project_link = PAGES[self.page] == "About" and self.info.page is InfoPage.PROJECT_LINK
        saver_route = ((self.route == "mascot" and not waking) or
                       (self.route == "widgets" and not project_link))
        self.screensaver.observe(not self.standby and not event_active and saver_route,
                                 now - self.screensaver_idle_since)
        if self.standby:
            self.hide_mascot()
            image.paste((0, 0, 0), (0, 0, SIZE, SIZE))
        elif event_active:
            self.hide_mascot()
            self.touch = None
            self.event_visible = True
            if not event_was_visible:
                if self.route is WifiSetupRoute.HOME_PASSWORD:
                    self.clear_simulated_wifi_secret()
                self.last_navigation = now
                self.screensaver_idle_since = now
            image = self.events.render_device(snapshot, now)
            # The native event renderer stamps SIMULATED here. The window and
            # status line already identify Studio; reserve this area for chrome.
            event_background = image.getpixel((CENTER, 20))
            ImageDraw.Draw(image).rectangle((183, 22, 282, 46), fill=event_background)
        elif self.screensaver.active:
            self.hide_mascot()
            image = render_screensaver(int((now - self.screensaver_idle_since - 15.0) * 25)).copy()
        elif self.route == "mascot":
            self.draw_mascot(image, now)
            if self.safe_guide:
                draw.rectangle((73, 73, 392, 392), outline=(62, 91, 113))
        else:
            if wifi_setup:
                self.draw_wifi_setup(image, draw, now)
            elif self.route == "connections":
                self.draw_connections(draw, snapshot, code, now)
            elif self.route == "options":
                self.draw_options(draw, now)
            elif self.route == "timer":
                self.draw_local_timer(draw, now)
            elif self.route == "restart":
                self.draw_restart(draw)
            elif self.route == "factory_reset":
                self.draw_factory_reset(draw)
            elif self.route == "quick_guide":
                self.draw_quick_guide(draw)
            elif self.route == "visibility":
                self.draw_visibility(draw, now)
            elif self.route == "display":
                label(draw, "Display", OPTIONS_TITLE_TOP, 34)
                self.draw_option_rows(draw, (
                    (f"Brightness  {self.brightness}%", self.change_brightness),
                    ("Auto-rotate  " + ("On" if self.auto_rotate else "Off"), self.change_auto_rotation),
                    ("Orientation  Auto" if self.auto_rotate else f"Orientation  {self.rotation} deg",
                     None if self.auto_rotate else self.change_orientation),
                ))
                self.sidebar(draw, -1, self.back)
            elif self.route == "behavior":
                label(draw, "Behavior", OPTIONS_TITLE_TOP, 34)
                self.button(draw, "Dictation  N/A", OPTIONS_ROW_TOP, None,
                            width=OPTIONS_ROW_WIDTH, height=OPTIONS_ROW_HEIGHT, caption_size=20)
                self.button(draw, "Shake standby/wake " + ("On" if self.shake_to_wake else "Off"),
                            OPTIONS_ROW_TOP + OPTIONS_ROW_STEP, self.change_shake_to_wake,
                            width=OPTIONS_ROW_WIDTH, height=OPTIONS_ROW_HEIGHT, caption_size=17)
                self.button(draw, "Sounds  N/A", OPTIONS_ROW_TOP + 2 * OPTIONS_ROW_STEP, None,
                            width=OPTIONS_ROW_WIDTH, height=OPTIONS_ROW_HEIGHT, caption_size=20)
                self.button(draw, "More options", 382, lambda: self.open("more_behavior"),
                            width=220, height=52, secondary=True, caption_size=20)
                self.sidebar(draw, -1, self.back)
            elif self.route == "more_behavior":
                label(draw, "More behavior", OPTIONS_TITLE_TOP, 34)
                setup_icon(draw, "motion", (CENTER, 169), 52, SETUP_ACCENT)
                label(draw, "Animate while idle", 208, 18, MUTED)
                box = (93, 273, 373, 345)
                palette = CONTROL_PALETTE if self.screensaver.enabled else SECONDARY_CONTROL_PALETTE
                draw_control_pill(draw, box, palette, corner_radius=12)
                face = font(20)
                draw.text((105, 309), "Screensaver", font=face, fill=FOREGROUND, anchor="lm")
                draw.text((365, 309), "On" if self.screensaver.enabled else "Off",
                          font=face, fill=FOREGROUND, anchor="rm")
                self.targets["Screensaver"] = (box, self.screensaver.toggle)
                self.sidebar(draw, -1, self.back)
            else:
                self.draw_widget(draw, snapshot, now)
            if self.safe_guide:
                draw.rectangle((73, 73, 392, 392), outline=(62, 91, 113))
        if not self.standby:
            self.draw_status(image, snapshot, now)
        if not self.standby and self.brightness != 100:
            image = ImageEnhance.Brightness(image).enhance(self.brightness / 100)
        return self.display_image(image)

    def display_image(self, image):
        mask = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(mask).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
        image.putalpha(mask)
        return rotate(image, self.displayed_rotation())

    def boot_image(self, now):
        elapsed_ms = (now - self.boot_started_at) * 1000
        if elapsed_ms >= self.boot_cycle_ms:
            self.boot = None
            self.boot_frame = None
            self.last_navigation = now
            self.screensaver_idle_since = now
            return None
        index = self.boot.frame_index(elapsed_ms)
        if self.boot_frame is None or self.boot_frame[0] != index:
            image = self.boot.image(elapsed_ms)
            if self.brightness != 100:
                image = ImageEnhance.Brightness(image).enhance(self.brightness / 100)
            self.boot_frame = (index, self.display_image(image))
        return self.boot_frame[1]

    # self: Preview scheduler publishing only observations actually supported by Studio.
    # Original source and drawing failures remain visible in the existing status surface.
    def tick(self):
        now = time.monotonic()
        if self.local_timer.observe(now) and self.route == "widgets" and not self.event_visible:
            self.open_local_timer()
        self.info.observation = SystemInfoObservation(studio_uptime_seconds=int(now - self.started_at))
        try:
            snapshot, code = self.observe()
            if self.pairing_attempted and snapshot.state == LinkState.CONNECTED:
                self.pairing_attempted = False
                if self.route != "mascot":
                    self.page = 0
                    self.open("widgets")
            sound_error = self.audio.last_error if self.audio is not None else self.audio_error
            status = self.source_error or self.boot_error or snapshot.last_error or sound_error
            if not status:
                if self.endpoint:
                    wifi_state = "on" if snapshot.network_ready else "off"
                    status = f"Loopback Wi-Fi {wifi_state} / {snapshot.state.value}"
                else:
                    status = "Offline preview / use -Ccp to pair Codex"
            self.status.configure(text=f"SIMULATED / device {self.device_rotation}° / {status}")
            image = self.boot_image(now) if self.boot else None
            if image is None:
                image = self.frame(snapshot, code, now)
        except (OSError, RuntimeError, ValueError, AttributeError) as error:
            self.hide_mascot()
            if self.boot is not None:
                self.boot = None
                self.boot_frame = None
                self.boot_error = f"VibeWare boot unavailable: {error}"
            self.targets.clear()
            self.acknowledged = ()
            self.event_visible = False
            self.status.configure(text="SIMULATED / " + str(error))
        else:
            self.layout = PreviewLayout.fit(self.canvas.winfo_width(), self.canvas.winfo_height(), False)
            self.photo = ImageTk.PhotoImage(enlarge_framebuffer(image, self.layout.scale))
            self.canvas.coords(self.image, self.layout.left, self.layout.top)
            self.canvas.itemconfigure(self.image, image=self.photo)
            self.acknowledged = tuple(self.targets)
        animated = self.boot or self.screensaver.active or (self.route == "mascot" and not self.standby)
        self.timer = self.root.after(40 if animated else 100, self.tick)

    def point(self, event):
        point = self.layout.to_device_pixel(event.x, event.y, self.displayed_rotation())
        return point if point is not None and sum((axis - CENTER) ** 2 for axis in point) <= CENTER ** 2 else None

    def action_at(self, point):
        if point is not None:
            x, y = point
            for name, ((x0, y0, x1, y1), action) in self.targets.items():
                if not (x0 <= x < x1 and y0 <= y < y1):
                    continue
                if isinstance(name, int) and (x - (x0 + 30)) ** 2 + (y - CENTER) ** 2 > 900:
                    continue
                return name, action
        return None, None

    # self: Preview input owner; a screensaver-dismiss touch cannot trigger hidden controls.
    # event: Tk pointer event transformed into the logical device coordinate system.
    def press(self, event):
        point = self.point(event)
        if self.boot is not None or self.standby:
            return
        if point is not None:
            self.screensaver_idle_since = time.monotonic()
        if point is not None and self.screensaver.active:
            self.screensaver.active = False
            self.last_navigation = time.monotonic()
            self.touch = None
            self.targets.clear()
            self.acknowledged = ()
            return
        if self.event_visible:
            if point is not None:
                self.last_navigation = time.monotonic()
            self.events.press(point, self.observe()[0], self.endpoint)
        elif point is not None:
            name, _ = self.action_at(point)
            self.touch = (self.route, self.page, self.visibility_page, point, name)
            self.touch_pressed_at = self.last_navigation = time.monotonic()

    def drag(self, event):
        point = self.point(event)
        if self.event_visible:
            if point is not None:
                self.last_navigation = self.screensaver_idle_since = time.monotonic()
            self.events.drag(point)
        elif self.touch is not None:
            if point is not None:
                self.last_navigation = self.screensaver_idle_since = time.monotonic()
            origin = self.touch[3]
            if point is None or sum((end - start) ** 2 for start, end in zip(origin, point)) >= 144:
                self.touch = None

    def release(self, event):
        touch, self.touch = self.touch, None
        point = self.point(event)
        if self.boot is not None or self.standby or point is None:
            return
        self.last_navigation = self.screensaver_idle_since = time.monotonic()
        try:
            if self.event_visible:
                self.events.release(point, self.observe()[0], self.endpoint)
                return
            if not touch or touch[:3] != (self.route, self.page, self.visibility_page):
                return
            _, _, _, origin, pressed = touch
            if sum((end - start) ** 2 for start, end in zip(origin, point)) >= 144:
                return
            name, action = self.action_at(point)
            if pressed is not None and name == pressed and name in self.acknowledged:
                action()
        except (OSError, RuntimeError, ValueError) as error:
            messagebox.showerror("SIMULATED Cody", str(error), parent=self.root)

    def close(self):
        self.clear_simulated_wifi_secret()
        if self.timer is not None:
            self.root.after_cancel(self.timer)
        try:
            if self.endpoint:
                self.endpoint.close()
        finally:
            if self.audio is not None:
                self.audio.close()
            self.root.destroy()
