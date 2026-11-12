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

from cody_animation_transport import AnimationTransport
from cody_ccp_event_view import EventView
from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_device_state import (
    INFO_PAGES, InfoPage, LocalTimerModel, LocalTimerPhase, ScreensaverModel, SystemInfoModel, SystemInfoObservation,
    TIMER_MAX_MINUTES, TIMER_MIN_MINUTES,
)
from cody_fake_audio import FakeAudioDriver
from cody_muse_preview import DISPLAY_SIZE as MUSE_SIZE, NO_REACTION, SCREEN_OFFSETS_MS, MusePose, NativeMusePreview
from cody_preview_layout import PreviewLayout, SIZE, enlarge_framebuffer
from cody_semantic_palette import device_rgb, studio_hex, studio_rgb
from cody_screensaver import render_screensaver
from cody_ui_theme import install_theme
from vibeware_boot import VibeWareBoot


CENTER = SIZE // 2
MUSE_OFFSET = (SIZE - MUSE_SIZE) // 2
BACKGROUND = studio_rgb("background")
FOREGROUND = studio_rgb("foreground")
MUTED = studio_rgb("muted")
ACCENT = studio_rgb("accent")
SETUP_ACCENT = studio_rgb("setup_primary")
SETUP_AMBER = studio_rgb("setup_warning")
SETUP_AMBER_FIELD = studio_rgb("setup_warning_field")
CONTROL_PALETTE = tuple(studio_rgb("primary_control", part) for part in ("top", "bottom", "border", "icon"))
SECONDARY_CONTROL_PALETTE = tuple(studio_rgb("secondary_control", part) for part in ("top", "bottom", "border", "icon"))
DESTRUCTIVE_CONTROL_PALETTE = tuple(studio_rgb("destructive_control", part) for part in ("top", "bottom", "border", "icon"))
CODEX_CONNECTION_COLOR = studio_rgb("codex_connection")
WIFI_CONNECTION_COLOR = studio_rgb("wifi_connection")
OPTIONS_TITLE_TOP = 82
OPTIONS_ROW_TOP = 130
OPTIONS_ROW_STEP = 80
OPTIONS_ROW_WIDTH = 280
OPTIONS_ROW_HEIGHT = 72
SETTINGS_ROW_STEP = 58
SETTINGS_ROW_HEIGHT = 56
OPTIONS_PAGE_COUNT = 3
QUICK_GUIDE = (
    ("Buttons", ACCENT, (
        ("HOLD BOOT 2S", "Options / tap: Home"), ("POWER", "Standby or wake"), ("HOLD PWR 3S", "Power off"),
    ), None),
    ("Touch", studio_rgb("device_ui", "guide_touch"), (
        ("SWIPE", "Change pages"), ("SWIPE DOWN", "Home from top edge"), ("DOUBLE TAP", "Record on stats"),
    ), "Dictation On + Codex link"),
    ("Offline", studio_rgb("device_ui", "guide_offline"), (
        ("SAVED STATS", "Last update in UTC"), ("TIMER", "Works without PC"), ("RESTART", "Keeps settings"),
    ), None),
)
NAVIGATION_IDLE_SECONDS = 10
NAVIGATION_FADE_SECONDS = 0.35
# Stable identities are shared with the local-network animation command.
# Each entry carries the existing native pose, artwork symbol, and work flag.
ANIMATION_SCENES = (
    ("Idle", "idle", MusePose.IDLE, 0, False),
    ("Listening / question", "listening_question", MusePose.LISTENING, 1, False),
    ("Thinking", "thinking", MusePose.THINKING, 4, False),
    ("Working", "working", MusePose.THINKING, 0, True),
    ("Reply / idea", "reply_idea", MusePose.REPLY, 3, False),
    ("Error / exclamation", "error_exclamation", MusePose.ERROR, 2, False),
    ("Sleep", "sleep", MusePose.SLEEP, 8, False),
    ("Waking", "waking", MusePose.WAKING, 0, False),
    ("Idle / heart", "idle_heart", MusePose.IDLE, 5, False),
    ("Idle / sweat", "idle_sweat", MusePose.IDLE, 6, False),
    ("Error / tension", "error_tension", MusePose.ERROR, 7, False),
    ("Thinking / question", "thinking_question", MusePose.THINKING, 1, False),
    ("Listening / exclamation", "listening_exclamation", MusePose.LISTENING, 2, False),
)
SIMULATED_WIFI_KEYS = {
    "letters": (".-_@", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"),
    "numbers": ("01", "2", "3", "4", "5", "6", "7", "8", "9"),
    "symbols": ("!\"#$", "%&'(", ")*+,", "-./:", ";<=>", "?@[\\", "]^_`", "{|}~", "@_`"),
}
PAGES = (
    "Device", "Reset countdown", "Context", "Codex limits", "Today", "Last 7 days",
    "Token history", "Credits", "Codex account", "About",
)
# Fixed demonstration fixtures, never reported as scanned radio observations.
SIMULATED_WIFI_NETWORKS = (("SIMULATED Home", -42), ("SIMULATED Office", -63), ("SIMULATED Guest", -78))
WIFI_NETWORKS_PER_PAGE = 3
VISIBILITY_ROWS = 3
VISIBILITY_PAGE_COUNT = (len(PAGES) - 1 + VISIBILITY_ROWS - 1) // VISIBILITY_ROWS
ABOUT_INFO = "Scan for more info"
ABOUT_URL = "https://github.com/umbertotechnopreneur/esp32-luna-shell"
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


@lru_cache(maxsize=8)
# size: Requested point size of the proportional application system face.
# Exception: Retains Pillow's default-font fallback if installed faces are unavailable.
def system_font(size):
    for filename in ("segoeui.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf"):
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
    layer.paste((*studio_rgb("device_ui", "title_shadow"), 255), (2, 3, width + 2, rows + 3), outline)
    layer.paste((*studio_rgb("device_ui", "title_outline"), 255), (0, 0, width, rows), outline)

    face_fill = Image.new("RGBA", mask.size)
    fill = ImageDraw.Draw(face_fill)
    frost, lavender = ((studio_rgb("device_ui", "icon_title_top"), studio_rgb("device_ui", "icon_title_bottom"))
                       if icon else (studio_rgb("device_ui", "title_top"), studio_rgb("device_ui", "title_bottom")))
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


# text: Complete screen heading.
# Returns a reusable monochrome bitmap from the same arcade face as SET UP WI-FI.
@lru_cache(maxsize=64)
def screen_title_mask(text):
    face = arcade_font(32)
    bounds = face.getbbox(text)
    mask = Image.new("L", (max(1, bounds[2] - bounds[0]), max(1, bounds[3] - bounds[1])))
    ImageDraw.Draw(mask).text((-bounds[0], -bounds[1]), text, font=face, fill=255)
    return mask.point(lambda value: 255 if value >= 128 else 0)


# draw: Logical preview surface.
# text: Complete screen heading; its original spelling is retained.
# top: Heading top edge, above the body.
# width: Maximum heading width inside the circular display.
# offset: Horizontal marquee position inside the full heading mask.
def screen_title(draw, text, top=82, width=320, offset=0):
    mask = screen_title_mask(str(text))
    viewport_width = min(width, mask.width)
    offset = min(max(0, offset), mask.width - viewport_width)
    left = CENTER - viewport_width // 2
    shadow = mask.filter(ImageFilter.MaxFilter(3))
    if mask.width <= width:
        draw.bitmap((left + 2, top + 3), shadow, fill=studio_rgb("device_ui", "title_shadow"))
        draw.bitmap((left, top), shadow, fill=studio_rgb("device_ui", "title_outline"))
    else:
        shadow_start = max(0, offset - 2)
        shadow_end = min(mask.width, offset + viewport_width - 2)
        if shadow_end > shadow_start:
            draw.bitmap((left + shadow_start - offset + 2, top + 3),
                        shadow.crop((shadow_start, 0, shadow_end, shadow.height)),
                        fill=studio_rgb("device_ui", "title_shadow"))
        draw.bitmap((left, top), shadow.crop((offset, 0, offset + viewport_width, shadow.height)),
                    fill=studio_rgb("device_ui", "title_outline"))
    first = studio_rgb("device_ui", "title_top")
    last = studio_rgb("device_ui", "title_bottom")
    for row in range(mask.height):
        fraction = row / max(1, mask.height - 1)
        color = tuple(round(start + (end - start) * fraction) for start, end in zip(first, last))
        draw.bitmap((left, top + row), mask.crop((offset, row, offset + viewport_width, row + 1)), fill=color)


# elapsed: Visible time since this title became current, in seconds.
# travel: Distance between the first and last readable positions in pixels.
def marquee_offset(elapsed, travel):
    if travel <= 0:
        return 0
    phase = max(0.0, elapsed) % (2.8 + travel * 0.016)
    return min(travel, int(max(0.0, phase - 1.2) / 0.016))


# elapsed: Visible time since the current heading first appeared, in seconds.
# travel: Overflow distance in logical pixels.
def screen_title_offset(elapsed, travel):
    if travel <= 0:
        return 0
    travel_time = travel / 40
    motion = elapsed % (3.0 + 2 * travel_time) - 1.2
    if motion <= 0:
        return 0
    if motion < travel_time:
        return int(motion * 40)
    return max(0, travel - int(max(0.0, motion - travel_time - 1.8) * 40))


# draw: Logical preview surface.
# text: Supporting line beneath the heading.
# top: Top edge of the supporting line.
# width: Available line width within the display.
def screen_subtitle(draw, text, top, width=310):
    face = arcade_font(18)
    text = str(text)
    if draw.textlength(text, font=face) > width:
        while text and draw.textlength(text + "...", font=face) > width:
            text = text[:-1]
        text += "..."
    draw.text((CENTER, top), text, font=face, fill=device_rgb("text", "subtitle"), anchor="mt")


def blend(color, opacity):
    return tuple(round(base + (value - base) * opacity) for base, value in zip(BACKGROUND, color))


def arrow(draw, center, step, opacity=1):
    x, y = center
    draw.ellipse((x - 30, y - 30, x + 29, y + 29), fill=blend(studio_rgb("secondary_control", "top"), opacity))
    draw.line(((x - 5 * step, y - 10), (x + 5 * step, y), (x - 5 * step, y + 10)),
              fill=blend(studio_rgb("secondary_control", "icon"), opacity), width=3, joint="curve")


# draw: Logical preview surface.
# selected: Zero-based page shown by the presenter.
# count: Known complete number of pages.
# top: Vertical position, kept above any footer action.
# compact: Use narrow marks for the outer dashboard carousel.
def page_marks(draw, selected, count, top=370, compact=False):
    if count < 1 or not 0 <= selected < count:
        raise ValueError("Invalid page indicator")
    mark_width = 8 if compact else (24 if count <= 8 else 16)
    width = count * mark_width + (count - 1) * 3
    left = (SIZE - width) // 2
    for index in range(count):
        color = studio_rgb("device_ui", "page_active" if index == selected else "page_inactive")
        x = left + index * (mark_width + 3)
        draw.rounded_rectangle((x, top, x + mark_width - 1, top + 3), radius=2, fill=color)


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
    draw.rectangle((left, top, left + size - 1, top + size - 1), fill=studio_rgb("device_ui", "qr_light"))
    for row, modules in enumerate(matrix):
        y = top + quiet + row * module
        for column, value in enumerate(modules):
            if value == "1":
                x = left + quiet + column * module
                draw.rectangle((x, y, x + module - 1, y + module - 1), fill=studio_rgb("device_ui", "qr_dark"))


@lru_cache(maxsize=1)
# No parameters: load the existing project artwork once for the Studio preview.
# Raises OSError for a missing or unreadable bundled logo.
def vibeware_about_logo():
    path = Path(__file__).resolve().parent / "assets/branding/vibeware-logo.png"
    with Image.open(path) as source:
        logo = source.convert("RGBA")
    logo.thumbnail((240, 240), Image.Resampling.NEAREST)
    return logo


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
    elif icon == "check":
        draw.line(((x - half // 2, y), (x - half // 6, y + half // 3), (x + half // 2, y - half // 3)),
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
        self.animation_scene = None
        self.animation_playing = False
        self.animation_elapsed_ms = 0.0
        self.animation_last_frame_at = None
        self.animation_frame = None
        self.animation_transport = None
        self.animation_device_pending = False
        # Studio-only intent; never sent to a physical device or stored on disk.
        self.birth_preview_queued = False
        self.birth_preview_started_at = None
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
        self.wifi_choices_return = "connections"
        self.wifi_networks_scanned = False
        self.wifi_network_page = 0
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
        self.about_from_options = False
        self.quick_guide_page = 0
        self.visibility_page = 0
        self.standby = False
        self.touch = None
        self.gesture = None
        self.event_visible = False
        self.available_pages = (0, PAGES.index("About"))
        self.chat_title_key = None
        self.chat_title_since = 0.0
        self.heading_key = None
        self.heading_elapsed = 0.0
        self.heading_last_frame = None
        self.frame_time = time.monotonic()
        self.targets = {}
        self.qr_chrome_until = 0.0
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
        ttk.Button(controls, text="BOOT / Home", command=self.home).pack(side="left", padx=4)
        ttk.Button(controls, text="BOOT 2s / Options", command=self.options).pack(side="left", padx=4)
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
        animation = ttk.Frame(root, style="Studio.TFrame")
        animation.pack(padx=12, pady=(0, 6))
        ttk.Label(animation, text="Animation", style="Status.TLabel").pack(side="left", padx=4)
        self.animation_choice = ttk.Combobox(animation, state="readonly", width=22,
                                             values=[entry[0] for entry in ANIMATION_SCENES])
        self.animation_choice.current(0)
        self.animation_choice.pack(side="left", padx=4)
        self.animation_choice.bind("<<ComboboxSelected>>", self.animation_select)
        preview_animation = ttk.Frame(root, style="Studio.TFrame")
        preview_animation.pack(padx=12, pady=(0, 6))
        ttk.Label(preview_animation, text="Preview", style="Status.TLabel").pack(side="left", padx=4)
        for caption, callback in (("Select", self.animation_select), ("Play", self.animation_play),
                                  ("Pause", self.animation_pause), ("Restart", self.animation_restart),
                                  ("Showcase", self.animation_normal)):
            ttk.Button(preview_animation, text=caption, width=8, command=callback).pack(side="left", padx=2)
        device_animation = ttk.Frame(root, style="Studio.TFrame")
        device_animation.pack(padx=12, pady=(0, 6))
        ttk.Label(device_animation, text="Physical Cody", style="Status.TLabel").pack(side="left", padx=4)
        self.animation_transport = AnimationTransport(root)
        self.animation_device_buttons = []
        for caption, action in (("Select", "select"), ("Play", "play"), ("Pause", "pause"),
                                ("Restart", "restart"), ("Restore", "normal")):
            button = ttk.Button(device_animation, text=caption, width=8,
                                command=lambda selected=action: self.animation_device_request(selected))
            button.pack(side="left", padx=2)
            self.animation_device_buttons.append(button)
        self.animation_device_status = ttk.Label(root, text="Physical Cody: not contacted", style="Status.TLabel")
        self.animation_device_status.pack(pady=(0, 6))
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
        self.show_assistant_quota_ring = True
        self.show_battery_ring = True
        self.auto_time = True
        self.simulated_time_notice = ""
        # Studio-only clock preferences; the preview never edits a physical Cody.
        self.quiet_hours_enabled = False
        self.quiet_hours_start = "22:00"
        self.quiet_hours_end = "07:00"
        self.quiet_edit_start = True
        self.quiet_edit_hour = 22
        self.quiet_edit_minute = 0
        self.info = SystemInfoModel()
        self.screensaver = ScreensaverModel()
        self.local_timer = LocalTimerModel()
        self.timer_return = "options"

    # self: Preview window playing the standard startup animation.
    def start_boot(self):
        self.boot = self.boot_frame = None
        self.birth_preview_started_at = None
        self.boot_error = ""
        self.muse_elapsed_ms = 0.0
        self.mascot_was_waking = False
        self.muse_last_frame_at = None
        self.muse_reaction_ms = None
        self.muse_seed = randbits(32)
        self.animation_scene = None
        self.animation_playing = False
        self.animation_elapsed_ms = 0.0
        self.animation_last_frame_at = None
        self.animation_frame = None
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
            if self.birth_preview_queued:
                self.birth_preview_queued = False
                self.birth_preview_started_at = time.monotonic()

    def observe(self):
        return self.endpoint.observe() if self.endpoint else (SimulatorSnapshot(), "")

    # self: Local route owner cancelling screensaver presentation on user navigation.
    # route: Requested setup, options or dashboard route.
    def open(self, route):
        if route != self.route:
            self.qr_chrome_until = 0.0
        if self.route is WifiSetupRoute.HOME_PASSWORD and route is not self.route:
            self.clear_simulated_wifi_secret()
        if route != "mascot":
            self.hide_mascot()
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

    # self: Open the Wi-Fi choices from the Connections view.
    def open_wifi_choices(self):
        self.wifi_choices_return = self.route
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

    # self: QR route owner showing transient status and decorative edge after a tap.
    def reveal_qr_chrome(self):
        self.qr_chrome_until = time.monotonic() + 3.5

    # self: Open the synthetic network picker and begin a new simulated scan.
    def open_home_networks(self):
        self.wifi_selected_index = -1
        self.open(WifiSetupRoute.HOME_NETWORKS)
        self.scan_simulated_networks()

    # self: Begin another explicitly synthetic scan and retire the old selection.
    def scan_simulated_networks(self):
        self.wifi_networks_scanned = False
        self.wifi_network_page = 0
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

    # self: Active Studio preview receiving the short BOOT Home shortcut.
    def home(self):
        if self.boot is None and self.birth_preview_started_at is None and not self.standby and not self.event_visible:
            if self.route is WifiSetupRoute.HOME_PASSWORD:
                self.clear_simulated_wifi_secret()
            self.about_from_options = False
            self.open("mascot")

    # self: Active Studio preview receiving the long BOOT Options shortcut.
    def options(self):
        if self.boot is None and self.birth_preview_started_at is None and not self.standby and not self.event_visible:
            self.show_options_page(0)

    # self: Studio navigation owner.
    # page: Options menu page reached by the lateral arrows or BOOT.
    def show_options_page(self, page):
        self.about_from_options = False
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
        if self.boot is not None or self.birth_preview_started_at is not None:
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
        if self.boot is not None or self.birth_preview_started_at is not None:
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
        if self.boot is not None or self.birth_preview_started_at is not None or self.standby:
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
        if self.boot is not None or self.birth_preview_started_at is not None or self.standby:
            return
        self.last_navigation = time.monotonic()
        if self.route == "mascot":
            self.motion_direction = direction
            self.motion_until = self.last_navigation + 0.45

    # self: Local navigation owner retaining the typed Info subpage.
    # step: Previous (-1) or next (1) action admitted by a visible arrow.
    def move(self, step):
        self.qr_chrome_until = 0.0
        if self.route == "visibility":
            if step < 0 and self.visibility_page == 0:
                self.back()
                return
            self.visibility_page = min(VISIBILITY_PAGE_COUNT - 1, self.visibility_page + step)
        else:
            if PAGES[self.page] == "About":
                if self.info.page is InfoPage.SYSTEM and step < 0 and self.about_from_options:
                    self.show_options_page(OPTIONS_PAGE_COUNT - 1)
                    return
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
        if self.boot is None and self.birth_preview_started_at is None and not self.standby and not self.event_visible:
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
            elif self.route == "display_rings":
                self.open("display")
            elif self.route == "display_time":
                self.open("display_rings")
            elif self.route == "quick_guide":
                self.move_quick_guide(-1)
            elif self.route == "more_behavior":
                self.open("behavior")
            elif self.route == "behavior_more":
                self.open("more_behavior")
            elif self.route == "quiet_hours":
                self.open("behavior_more")
            elif self.route == "quiet_editor":
                self.open("quiet_hours")
            elif self.route == "synth_voice":
                self.open("behavior_more")
            elif self.route is WifiSetupRoute.HOME_CHOICES:
                self.open(self.wifi_choices_return)
            elif self.route is WifiSetupRoute.HOME_INTRO:
                self.open(self.home_pair_return)
            elif self.route is WifiSetupRoute.HOME_PLATFORM_QR:
                self.open(WifiSetupRoute.HOME_INTRO)
            elif self.route is WifiSetupRoute.HOME_PAIR:
                self.open(WifiSetupRoute.HOME_INTRO)
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
        self.birth_preview_queued = True
        self.page = 0
        self.reboot_preview(WifiSetupRoute.NOT_CONNECTED)

    # self: Preview boot owner resetting only transient navigation and input state.
    # route: Startup route selected from the actual post-reset preview state.
    # pending_pairing: Whether a durable commissioning candidate survived restart.
    def reboot_preview(self, route, pending_pairing=False):
        self.visibility_page = self.options_page = 0
        self.about_from_options = False
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

    # self: Schedule only this Studio window's next simulated restart.
    def schedule_birth_preview(self):
        self.birth_preview_queued = True

    # self: Preview owner returning to the first enabled statistics page with data.
    def open_statistics(self):
        self.about_from_options = False
        self.page = next((index for index in self.available_pages if PAGES[index] not in {"Device", "About"}), 0)
        self.info.page = InfoPage.SYSTEM
        self.screensaver.active = False
        self.touch = None
        self.open("widgets")

    # self: Open About directly from Device options without depending on visibility settings.
    def open_about(self):
        self.page = PAGES.index("About")
        self.info.page = InfoPage.SYSTEM
        self.about_from_options = True
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

    # self: Simulated preference for displaying an available assistant quota arc.
    def change_assistant_quota_ring(self):
        self.show_assistant_quota_ring = not self.show_assistant_quota_ring

    # self: Simulated preference for displaying an available battery arc.
    def change_battery_ring(self):
        self.show_battery_ring = not self.show_battery_ring

    # self: Simulated preference only; Studio does not perform a device NTP sync.
    def change_auto_time(self):
        self.auto_time = not self.auto_time

    # self: Isolated Studio preview; enabling never changes a physical device.
    def toggle_quiet_hours(self):
        if not self.quiet_hours_enabled and self.quiet_hours_start == self.quiet_hours_end:
            messagebox.showerror("SIMULATED quiet hours", "Start and end must differ.", parent=self.root)
            return
        self.quiet_hours_enabled = not self.quiet_hours_enabled

    # self: Isolated Studio preview holding the simulated local-time schedule.
    # start: True edits the start; false edits the end of the interval.
    def edit_quiet_hours(self, start):
        value = self.quiet_hours_start if start else self.quiet_hours_end
        self.quiet_edit_start = start
        self.quiet_edit_hour, self.quiet_edit_minute = map(int, value.split(":"))
        self.open("quiet_editor")

    # self: Simulated edit state; no changes are applied until Save.
    # hours: True adjusts hours; false adjusts minutes.
    # delta: One increment or decrement chosen in the row.
    def adjust_quiet_time(self, hours, delta):
        if hours:
            self.quiet_edit_hour = (self.quiet_edit_hour + delta) % 24
        else:
            self.quiet_edit_minute = (self.quiet_edit_minute + delta) % 60

    # self: Apply the staged clock value to this Studio session only.
    def save_quiet_time(self):
        value = f"{self.quiet_edit_hour:02d}:{self.quiet_edit_minute:02d}"
        other = self.quiet_hours_end if self.quiet_edit_start else self.quiet_hours_start
        if self.quiet_hours_enabled and value == other:
            messagebox.showerror("SIMULATED quiet hours", "Start and end must differ.", parent=self.root)
            return
        if self.quiet_edit_start:
            self.quiet_hours_start = value
        else:
            self.quiet_hours_end = value
        self.open("quiet_hours")

    # self: Simulated Sync time action; no device or PC time is fabricated.
    def request_simulated_time_sync(self):
        self.simulated_time_notice = "PC time unavailable in Studio"

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
    # secondary: Preserve the semantic identity of a dismiss or postpone action.
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
        colors = tuple(studio_rgb("device_ui", name) for name in ("link_pulse_dark", "link_pulse_mid", "link_pulse_light", "link_pulse_mid"))
        link_pulse = colors[int(now / 0.4) % len(colors)]
        if code or snapshot.pending_pairing:
            self.screen_title(draw, "Pair Codex", 88)
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
            self.screen_title(draw, "Waiting for PC", 88)
            setup_icon(draw, "link", (CENTER, 176), 56, link_pulse if snapshot.network_ready else MUTED)
            label(draw, "Open Codex on your PC" if snapshot.network_ready else "Wi-Fi unavailable", 256, 18)
            label(draw, "Pairing stays saved", 284, 18, MUTED)
            self.button(draw, "Later", 352, self.leave_pairing_wait, width=240, height=72, secondary=True)
            return
        if self.pairing_attempted and snapshot.state is LinkState.TIMED_OUT:
            self.screen_title(draw, "Try again", 88)
            setup_icon(draw, "retry", (CENTER, 176), 56, SETUP_AMBER)
            label(draw, "Pairing code expired", 256, 18, MUTED)
            self.button(draw, "Retry", 352, connect, width=240, height=72)
            self.sidebar(draw, -1, self.back)
            return
        self.screen_title(draw, "Connections", OPTIONS_TITLE_TOP)
        self.draw_connection_action(draw, 148, ("Connect", "Codex"), "link",
                                    CODEX_CONNECTION_COLOR, connect)
        self.draw_connection_action(draw, 318, ("Set up", "Wi-Fi"), "wifi",
                                    WIFI_CONNECTION_COLOR, self.open_wifi_choices)
        self.sidebar(draw, -1, self.back)

    # self: Synthetic status chrome; host clock and loopback link are never firmware observations.
    # image: Logical framebuffer receiving the compact toolbar before rotation.
    # snapshot: Explicitly simulated loopback state.
    # now: Monotonic time for the visibility fade.
    # opacity_override: Optional QR-specific fade independent of general navigation.
    def draw_status(self, image, snapshot, now, opacity_override=None):
        opacity = self.navigation_opacity(now) if opacity_override is None else opacity_override
        if opacity <= 0:
            return
        # Blend the chrome over the actual pixels of every scene, including the globe.
        layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        wall = time.localtime()
        draw.text((233, 20), time.strftime("%a %d %b %Y", wall).upper(), font=font(10),
                  fill=(*MUTED, 255), anchor="mt")
        draw.text((233, 34), time.strftime("%H:%M", wall), font=font(15),
                  fill=(*FOREGROUND, 255), anchor="mt")
        # The loopback has no measured RSSI, BLE telemetry or physical battery.
        setup_icon(draw, "wifi", (110, 65), 20, (*MUTED, 255))
        setup_icon(draw, "bluetooth", (161, 40), 18, (*MUTED, 255))
        draw.rounded_rectangle((335, 57, 358, 72), radius=2, outline=(*MUTED, 255), width=2)
        draw.rectangle((359, 61, 361, 68), fill=(*MUTED, 255))
        if self.show_battery_percentage:
            draw.text((364, 63), "N/A", font=font(10), fill=(*MUTED, 255))
        layer.putalpha(layer.getchannel("A").point(lambda alpha: round(alpha * opacity)))
        image.paste(layer, (0, 0), layer)

    # draw: Simulated network rows share flat icons and separators with Settings.
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
        if rssi is None:
            label(draw, name, top + (height - 18) // 2, 18, FOREGROUND, width=256)
        else:
            setup_icon(draw, "wifi", (120, top + height // 2), 24,
                       WIFI_CONNECTION_COLOR if selected else SETUP_ACCENT)
            if selected:
                self.draw_selected_wifi_network(draw, now, top + 7)
            else:
                shown = name
                while shown and draw.textlength(shown, font=font(15)) > 132:
                    shown = shown[:-1]
                if shown != name:
                    while shown and draw.textlength(shown + "...", font=font(15)) > 132:
                        shown = shown[:-1]
                    shown += "..."
                draw.text((141, top + height // 2), shown, font=font(15), fill=FOREGROUND, anchor="lm")
                draw.text((359, top + height // 2), f"{rssi} dBm", font=font(15), fill=FOREGROUND, anchor="rm")
            draw.line((141, top + height - 2, 373, top + height - 2),
                      fill=studio_rgb("device_ui", "row_separator"), width=2)
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
        width = 220
        travel = max(0, round(draw.textlength(text, font=face)) - width)
        if travel == 0:
            draw.text((141, top), text, font=face, fill=FOREGROUND, anchor="lt")
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
        draw.bitmap((141, top), viewport, fill=FOREGROUND)

    # draw: Studio representation of the native CODY_text_entry multitap layout.
    def draw_simulated_wifi_keyboard(self, draw):
        self.screen_title(draw, "Wi-Fi password", 70)
        screen_subtitle(draw, "Replaces saved Wi-Fi", 106, width=300)
        field = (95, 125, 371, 156)
        draw_control_pill(draw, field, (studio_rgb("device_ui", "wifi_field_top"), studio_rgb("device_ui", "wifi_field_bottom"), studio_rgb("device_ui", "wifi_field_border"), FOREGROUND), 14)
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
            if key < 9:
                caption = groups[key].upper() if mode == "letters" and self.wifi_key_uppercase else groups[key]
            else:
                caption = ("Shift" if mode == "letters" else "#+=" if mode == "numbers" else "123") if key == 9 else (
                    "Space" if key == 10 else "Del" if key == 11 else "123" if key == 12 and mode == "letters" else
                    "ABC" if key == 12 else "Hide" if key == 13 and self.wifi_key_reveal else
                    "Show" if key == 13 else "Connect")
            color = WIFI_CONNECTION_COLOR if key == 9 and mode == "letters" and self.wifi_key_uppercase else FOREGROUND
            label(draw, caption, top + (height - 15) // 2, 15, color, x=left + width // 2, width=width - 6)
            self.targets[f"wifi-key-{key}"] = (box, lambda selected=key: self.activate_simulated_wifi_key(selected))

    # self: Preview owner drawing the same Wi-Fi menu as the native device.
    # image: Logical framebuffer before rotation.
    # draw: Drawing context for the screen title and device name.
    # Exception: Preserves native preview or bundled font failures.
    def draw_home_wifi_choices(self, image, draw):
        self.screen_title(draw, "SET UP WI-FI", 84)
        draw.text((CENTER, 118), self.simulated_ble_name, font=arcade_font(18),
                  fill=studio_rgb("device_ui", "wifi_key_hint"), anchor="mt")

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
            self.screen_title(draw, "Bluetooth setup")
            for top, text in ((140, "1. Get Espressif app"), (180, "2. Scan Cody QR in app"),
                              (220, "3. Choose your Wi-Fi"), (270, "TAP BELOW TO CHOOSE")):
                label(draw, text, top, 19)
            apple = ("......##....", ".....##.....", "............", "...###.###..", ".##########.",
                     "#########...", "########....", "#########...", ".##########.", ".##########.",
                     "..########..", "...###.##...")
            android = ("..#......#..", "...#....#...", "...######...", "..########..", "..##.##.##..",
                       "..########..", "............", "#.########.#", "#.########.#", "#.########.#",
                       "..########..", "...##..##...")
            colors = tuple(studio_hex("device_ui", name) for name in
                           ("platform_green", "platform_yellow", "platform_orange", "platform_red", "platform_violet", "platform_blue"))
            for left, glyph, platform in ((143, apple, True), (275, android, False)):
                for y, row in enumerate(glyph):
                    for x, pixel in enumerate(row):
                        if pixel == "#":
                            draw.rectangle((left + x * 4, 316 + y * 4, left + x * 4 + 3, 319 + y * 4),
                                           fill=colors[y // 2] if platform else colors[0])
            self.targets["Apple"] = ((112, 305, 222, 375), lambda: self.open_platform_qr(True))
            self.targets["Android"] = ((244, 305, 354, 375), lambda: self.open_platform_qr(False))
        elif self.route is WifiSetupRoute.HOME_PLATFORM_QR:
            self.screen_title(draw, "SIMULATED app QR")
            matrix = store_qr_matrix("SIMULATED-APP-QR-NOT-A-DEVICE")
            if matrix is not None:
                qr_tile(draw, matrix, 128, 264)
            self.targets["QR chrome"] = ((0, 0, SIZE, SIZE), self.reveal_qr_chrome)
            return
        elif self.route is WifiSetupRoute.HOME_PAIR:
            self.screen_title(draw, "SIMULATED pairing")
            matrix = store_qr_matrix("SIMULATED-CODY-NOT-A-DEVICE")
            if matrix is None:
                label(draw, "QR preview unavailable", 240, 17, MUTED)
            else:
                qr_tile(draw, matrix, 128, 264)
            self.targets["QR chrome"] = ((0, 0, SIZE, SIZE), self.reveal_qr_chrome)
            return
        elif self.route is WifiSetupRoute.HOME_NETWORKS:
            self.screen_title(draw, "Wi-Fi networks", 75)
            screen_subtitle(draw, "Replaces saved Wi-Fi", 110)
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
                            color = studio_rgb("device_ui", "hourglass_cap" if cap else "hourglass_glass" if glass else "hourglass_sand")
                            draw.rectangle((205 + 4 * x, 174 + 4 * y, 208 + 4 * x, 177 + 4 * y), fill=color)
                label(draw, "Searching...", 270, 17)
                label(draw, "SIMULATED", 305, 13, MUTED)
            else:
                label(draw, "Tap again to select" if self.wifi_selected_index >= 0 else
                      "SIMULATED - no radio scan", 132, 15, MUTED)
            if self.wifi_networks_scanned and not searching:
                start = self.wifi_network_page * WIFI_NETWORKS_PER_PAGE
                visible = SIMULATED_WIFI_NETWORKS[start:start + WIFI_NETWORKS_PER_PAGE]
                for row, (name, rssi) in enumerate(visible):
                    index = start + row
                    self.simulated_network_button(draw, name, 160 + row * 45,
                        lambda selected_index=index, selected=name:
                            self.select_simulated_network(selected_index, selected),
                        39, rssi, index == self.wifi_selected_index, now, f"wifi-network-{index}")
                pages = (len(SIMULATED_WIFI_NETWORKS) + WIFI_NETWORKS_PER_PAGE - 1) // WIFI_NETWORKS_PER_PAGE
                if pages:
                    page_marks(draw, self.wifi_network_page, pages, 310)
            if not searching:
                self.simulated_network_button(draw, "Scan for networks", 342,
                                              self.scan_simulated_networks, 38)
        elif self.route is WifiSetupRoute.HOME_PASSWORD:
            self.draw_simulated_wifi_keyboard(draw)
        elif self.route is WifiSetupRoute.HOME_NETWORK_RESULT:
            self.screen_title(draw, "SIMULATED Wi-Fi")
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
            self.screen_title(draw, "Wi-Fi offline" if recovering else "Not connected", 180)
            screen_subtitle(draw, "Connect Cody to Wi-Fi", 223)
            self.button(draw, "Set up Wi-Fi" if recovering else "Tap to pair", 272 if recovering else 352,
                        lambda: self.open_phone_guide(WifiSetupRoute.NOT_CONNECTED),
                        width=280 if recovering else 240, height=58 if recovering else 72,
                        icon="wifi")
            if recovering:
                self.button(draw, "Later", 350, lambda: self.open(WifiSetupRoute.NOT_CONNECTED),
                            width=160, height=50, secondary=True, icon="chevron")
            return
        if self.route is WifiSetupRoute.PHONE_GUIDE:
            self.screen_title(draw, "Wi-Fi setup", 130)
            label(draw, "Get the Espressif app", 186, 17, MUTED)
            label(draw, "Then open it and scan", 212, 17, MUTED)
            label(draw, "the pairing QR.", 238, 17, MUTED)
            self.button(draw, "App download QR", 274, lambda: self.open(WifiSetupRoute.INSTALL_APP),
                        width=280, height=62, icon="qr")
            self.button(draw, "Show pairing QR", 348, self.open_pairing_preview,
                        width=280, height=64, icon="bluetooth")
        elif self.route is WifiSetupRoute.INSTALL_APP:
            self.screen_title(draw, "Scan to install")
            qr_tile(draw, INSTALL_APP_QR, 128, 264)
            self.targets["QR chrome"] = ((0, 0, SIZE, SIZE), self.reveal_qr_chrome)
            return
        elif self.route is WifiSetupRoute.SCAN_TO_PAIR:
            setup_icon(draw, "bluetooth", (CENTER, 121), 54, SETUP_ACCENT)
            self.screen_title(draw, "Scan to pair", 180)
            screen_subtitle(draw, "Bluetooth setup runs", 230)
            screen_subtitle(draw, "on the physical device.", 256)
            self.button(draw, "Cancel", 390, self.back, width=220, height=44, secondary=True)
            return
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview owning Options and its categories.
    # draw: Logical canvas before rotation.
    # rows: Visible captions and actions, with at most four entries per page.
    def draw_option_rows(self, draw, rows):
        for index, (name, action) in enumerate(rows):
            top = OPTIONS_ROW_TOP + index * SETTINGS_ROW_STEP
            box = (93, top, 373, top + SETTINGS_ROW_HEIGHT)
            parts = name.split("  ", 1)
            icon = "phone" if "Orientation" in name or "rotate" in name else "motion"
            setup_icon(draw, icon, (111, top + 28), 28, SETUP_ACCENT if action else MUTED)
            draw.text((141, top + (15 if len(parts) > 1 else 28)), parts[0],
                      font=system_font(17 if len(parts[0]) > 18 else 20), fill=FOREGROUND, anchor="lm")
            if len(parts) > 1:
                draw.text((141, top + 42), parts[1], font=font(18),
                          fill=WIFI_CONNECTION_COLOR if action else MUTED, anchor="lm")
            draw.line((141, top + SETTINGS_ROW_HEIGHT - 2, 373, top + SETTINGS_ROW_HEIGHT - 2),
                      fill=studio_rgb("device_ui", "row_separator"), width=2)
            if action is not None:
                self.targets[name] = (box, action)

    # self: Studio preview of the current group of three configurable screens.
    # draw: Logical canvas before rotation.
    # now: Monotonic time for the shared arrow fade.
    def draw_visibility(self, draw, now):
        self.screen_title(draw, "Visible screens", OPTIONS_TITLE_TOP)
        start = 1 + self.visibility_page * VISIBILITY_ROWS
        for index, page in enumerate(PAGES[start:start + VISIBILITY_ROWS]):
            visible = page in self.visible
            top = OPTIONS_ROW_TOP + index * OPTIONS_ROW_STEP
            box = (93, top, 373, top + OPTIONS_ROW_HEIGHT)
            setup_icon(draw, "phone", (126, top + 36), 28, SETUP_ACCENT)
            draw.text((156, top + 23), page, font=system_font(19), fill=FOREGROUND, anchor="lm")
            draw.text((156, top + 50), "Visible" if visible else "Hidden",
                      font=font(18), fill=WIFI_CONNECTION_COLOR if visible else MUTED, anchor="lm")
            draw.line((141, top + OPTIONS_ROW_HEIGHT - 2, 373, top + OPTIONS_ROW_HEIGHT - 2),
                      fill=studio_rgb("device_ui", "row_separator"), width=2)
            self.targets[page] = (box, lambda selected=page: self.change_visibility(selected))
        page_marks(draw, self.visibility_page, VISIBILITY_PAGE_COUNT)
        opacity = self.navigation_opacity(now)
        if opacity > 0 and self.visibility_page == 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview owning the active menu page and touch targets.
    # image: Logical framebuffer receiving the gradient footer.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time for the lateral arrow fade.
    def draw_options(self, image, draw, now):
        self.screen_title(draw, ("TOOLS", "PREFERENCES", "DEVICE")[self.options_page], OPTIONS_TITLE_TOP)
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
                ("About", self.open_about),
            )
        icons = (("link", "motion", "clock"), ("qr", "phone", "motion"),
                 ("retry", "offline", "phone", "link"))[self.options_page]
        colors = (studio_rgb("codex_connection"), studio_rgb("device_ui", "guide_offline"),
                  studio_rgb("setup_warning"), studio_rgb("codex_connection"))
        for index, ((name, action), icon) in enumerate(zip(rows, icons)):
            top = OPTIONS_ROW_TOP + index * SETTINGS_ROW_STEP
            box = (93, top, 373, top + SETTINGS_ROW_HEIGHT)
            setup_icon(draw, icon, (126, top + 28), 28, colors[index])
            words = name.split(" ", 1) if name in ("Visible screens", "Factory reset", "Quick guide", "Restart device") else (name,)
            for line, title in enumerate(words):
                baseline = top + 28 if len(words) == 1 else top + 15 + line * 24
                draw.text((156, baseline), title, font=system_font(21), fill=FOREGROUND, anchor="lm")
            draw.line((141, top + SETTINGS_ROW_HEIGHT - 2, 373, top + SETTINGS_ROW_HEIGHT - 2), fill=studio_rgb("device_ui", "row_separator"), width=2)
            self.targets[name] = (box, action)
        page_marks(draw, self.options_page, OPTIONS_PAGE_COUNT)
        opacity = self.navigation_opacity(now)
        if opacity > 0 and self.options_page == 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

    # self: Studio preview of the three fixed guide pages.
    # image: Logical framebuffer receiving the gradient footer.
    # draw: Logical canvas before rotation.
    def draw_quick_guide(self, image, draw):
        title, accent, rows, note = QUICK_GUIDE[self.quick_guide_page]
        self.screen_title(draw, title, OPTIONS_TITLE_TOP)
        for index, (action, detail) in enumerate(rows):
            label(draw, action, 130 + index * 68, 36, accent)
            label(draw, detail, 173 + index * 68, 19, MUTED)
        if note is not None:
            label(draw, note, 340, 15, MUTED)
        last_page = self.quick_guide_page == len(QUICK_GUIDE) - 1
        page_marks(draw, self.quick_guide_page, len(QUICK_GUIDE))
        if self.quick_guide_page == 0:
            self.sidebar(draw, -1, self.back)
        if last_page:
            setup_icon(draw, "check", (111, 351), 24, WIFI_CONNECTION_COLOR)
            draw.text((141, 351), "OK", font=font(20), fill=FOREGROUND, anchor="lm")
            draw.line((141, 364, 373, 364), fill=studio_rgb("device_ui", "row_separator"), width=2)
            self.targets["OK"] = ((93, 336, 373, 366), lambda: self.show_options_page(OPTIONS_PAGE_COUNT - 1))

    # self: Studio preview drawing its independently retained timer state.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time used for the timer digits and Back fade.
    def draw_local_timer(self, draw, now):
        timer = self.local_timer
        minutes, seconds = divmod(timer.remaining_seconds(now), 60)
        self.screen_title(draw, "Timer", 93)
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
    # image: Logical framebuffer receiving the gradient footer.
    # draw: Logical canvas before rotation.
    def draw_restart(self, image, draw):
        self.screen_title(draw, "Restart device", OPTIONS_TITLE_TOP)
        screen_subtitle(draw, "Keeps Wi-Fi/settings", 155)
        screen_subtitle(draw, "Stops active timer", 184)
        self.draw_confirmation_rows(draw, "Restart device", self.restart_device)
        self.sidebar(draw, -1, self.back)

    # self: Studio preview routing cancellation or an explicit reset to its owner.
    # image: Logical framebuffer receiving the gradient footer.
    # draw: Logical canvas before rotation.
    def draw_factory_reset(self, image, draw):
        self.screen_title(draw, "Factory reset", OPTIONS_TITLE_TOP)
        screen_subtitle(draw, "Clear Wi-Fi, Codex", 155)
        screen_subtitle(draw, "link and preferences.", 184)
        screen_subtitle(draw, "Restart with defaults", 213)
        self.draw_confirmation_rows(draw, "Factory reset", self.reset_device)
        self.sidebar(draw, -1, self.back)

    # draw: Logical framebuffer for the two explicit, release-activated choices.
    # caption: Confirmed action label.
    # confirm: Callback invoked only when the confirmation row is tapped.
    def draw_confirmation_rows(self, draw, caption, confirm):
        for index, (name, action, icon) in enumerate((("Cancel", self.back, "offline"), (caption, confirm, "check"))):
            top = 250 + index * 60
            setup_icon(draw, icon, (111, top + 28), 28, SETUP_ACCENT if index == 0 else WIFI_CONNECTION_COLOR)
            draw.text((141, top + 28), name, font=font(20), fill=FOREGROUND, anchor="lm")
            draw.line((141, top + 54, 373, top + 54), fill=studio_rgb("device_ui", "row_separator"), width=2)
            self.targets[name] = ((93, top, 373, top + 56), action)

    def open_about_url(self):
        if not webbrowser.open_new_tab(ABOUT_URL):
            raise RuntimeError("Could not open the Cody project page")

    # self: Studio preview registering the project link action.
    # draw: Logical canvas before rotation.
    def draw_about_qr(self, draw):
        self.screen_title(draw, ABOUT_INFO, 82)
        matrix = store_qr_matrix(ABOUT_URL)
        if matrix is None:
            label(draw, "QR preview unavailable", 240, 17, MUTED)
        else:
            qr_tile(draw, matrix, 128, 264)
        self.targets["QR chrome"] = ((0, 0, SIZE, SIZE), self.reveal_qr_chrome)

    # self: Preview renderer borrowing the typed Info state and observation.
    # image: Logical framebuffer receiving the existing VibeWare artwork.
    # draw: Logical canvas before rotation, with central content clear of both arrows.
    def draw_system_info(self, image, draw):
        page = self.info.page
        if page is InfoPage.PROJECT_LINK:
            self.draw_about_qr(draw)
            return
        if page is InfoPage.VIBEWARE:
            logo = vibeware_about_logo()
            image.paste(logo, (CENTER - logo.width // 2, 105), logo)
            label(draw, "umbertogiacobbi.biz", 355, 19, FOREGROUND)
            page_marks(draw, INFO_PAGES.index(page), len(INFO_PAGES), 397)
            return
        title = {
            InfoPage.SYSTEM: "SYSTEM",
            InfoPage.MEMORY: "MEMORY",
            InfoPage.FIRMWARE: "FIRMWARE",
        }[page]
        self.screen_title(draw, title, 82, width=230)
        page_marks(draw, INFO_PAGES.index(page), len(INFO_PAGES), 397)
        self.draw_info_icon(draw, page)
        draw.line((73, 125, 392, 125), fill=studio_rgb("device_ui", "guide_outline"))
        if page is InfoPage.SYSTEM:
            for index, top in enumerate((137, 187), 1):
                draw.text((93, top), f"CPU {index}", font=font(17), fill=MUTED)
                draw.text((240, top), "Unavailable", font=font(17), fill=MUTED)
            for caption, top in (("INTERNAL RAM", 230), ("PSRAM", 256)):
                draw.text((93, top), caption, font=font(17), fill=MUTED)
                draw.text((240, top), "Unavailable", font=font(17), fill=MUTED)
            hours, seconds = divmod(self.info.observation.studio_uptime_seconds, 3600)
            minutes, seconds = divmod(seconds, 60)
            label(draw, "STUDIO UPTIME", 284, 17, MUTED)
            label(draw, f"{hours:02d}:{minutes:02d}:{seconds:02d}", 309, 23, ACCENT)
            label(draw, "PREVIOUS UPTIME", 337, 17, MUTED)
            label(draw, "Unavailable", 362, 17, MUTED)
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
        if self.route == "widgets" and self.page not in self.available_pages and not (
                self.about_from_options and PAGES[self.page] == "About"):
            self.page = 0
            self.info.page = InfoPage.SYSTEM
            self.touch = None

    # self: Display owner retaining the current heading and visible animation time.
    # draw: Logical preview canvas.
    # text: Complete heading, without truncating or changing font size.
    # top: Heading top edge.
    # width: Fixed viewport width inside the admitted display chord.
    def screen_title(self, draw, text, top=82, width=320):
        text = str(text)
        key = (self.route, self.page, self.options_page, self.visibility_page,
               self.quick_guide_page, text, top, width)
        now = self.frame_time
        if key != self.heading_key:
            self.heading_key = key
            self.heading_elapsed = 0.0
        elif self.heading_last_frame is not None:
            delta = now - self.heading_last_frame
            # An unseen screen does not advance while a different route owns the display.
            if 0.0 <= delta <= 0.25:
                self.heading_elapsed += delta
        self.heading_last_frame = now
        mask = screen_title_mask(text)
        offset = screen_title_offset(self.heading_elapsed, mask.width - width)
        screen_title(draw, text, top, width, offset)

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
        offset = marquee_offset(now - self.chat_title_since, travel)
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
            draw.rectangle((93, 244, 372, 250), fill=studio_rgb("device_ui", "context_track"))
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
        self.screen_title(draw, "Device", 82)
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
            draw.line((95, y, 371, y), fill=studio_rgb("device_ui", "device_divider"))
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
    # image: Logical framebuffer receiving visual artwork.
    # draw: Logical canvas before rotation.
    # snapshot: Current loopback projection, if available.
    # now: Monotonic frame time for uptime and arrow fading.
    def draw_widget(self, image, draw, snapshot, now):
        page = PAGES[self.page]
        if page in ("Codex account", "Credits", "Context", "Codex limits", "Last 7 days"):
            self.screen_title(draw, page.upper(), 82)
        elif page == "Reset countdown":
            self.screen_title(draw, "Codex reset", 82)
        elif page == "Token history":
            self.screen_title(draw, "TOKEN ACTIVITY", 82)
        elif page not in ("About", "Device"):
            self.screen_title(draw, page, 82)
        if page == "Device":
            self.draw_device_summary(draw, snapshot, now)
        elif page == "Context":
            self.draw_context(draw, snapshot, now)
        elif page == "About":
            self.draw_system_info(image, draw)
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
        if page != "About" and len(self.available_pages) > 1 and self.page in self.available_pages:
            page_marks(draw, self.available_pages.index(self.page), len(self.available_pages), 435, compact=True)
        if opacity > 0 and self.about_from_options and page == "About" and self.info.page is InfoPage.SYSTEM:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)

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

    # self: Studio preview owner with an optional manually selected animation.
    # now: Current monotonic time, used only while the mascot is visible.
    def advance_animation_clock(self, now):
        if self.animation_playing and self.animation_last_frame_at is not None:
            self.animation_elapsed_ms += max(0.0, (now - self.animation_last_frame_at) * 1000)
        self.animation_last_frame_at = now

    # self: Studio preview owner selecting one authored native pose.
    # event: Optional Tk combobox event.
    def animation_select(self, event=None):
        self.animation_scene = ANIMATION_SCENES[self.animation_choice.current()]
        self.animation_playing = False
        self.animation_elapsed_ms = 0.0
        self.animation_last_frame_at = None
        self.animation_frame = None
        self.boot = self.boot_frame = None
        self.standby = False
        self.open("mascot")
        self.prepare_setup_mascot()
        self.avatar_preview().reset(0)

    # self: Studio preview owner resuming the selected pose from its paused time.
    def animation_play(self):
        if self.animation_scene is None:
            self.animation_select()
        else:
            self.boot = self.boot_frame = None
            self.standby = False
            self.open("mascot")
        self.animation_last_frame_at = None
        self.animation_playing = True

    # self: Studio preview owner freezing the selected pose at its current time.
    def animation_pause(self):
        if self.animation_scene is None:
            return
        if self.animation_playing:
            self.advance_animation_clock(time.monotonic())
            self.animation_frame = None
        self.animation_playing = False
        self.animation_last_frame_at = None

    # self: Studio preview owner replaying the selected pose from its first frame.
    def animation_restart(self):
        self.animation_select()
        self.animation_playing = True

    # self: Studio preview owner restoring the automatic showcase from its beginning.
    def animation_normal(self):
        self.animation_scene = None
        self.animation_playing = False
        self.animation_elapsed_ms = 0.0
        self.animation_last_frame_at = None
        self.animation_frame = None
        self.muse_elapsed_ms = 0.0
        self.muse_last_frame_at = None
        self.muse_reaction_ms = None
        self.avatar_preview().reset(self.muse_seed)

    # self: Studio owner issuing a single authenticated local-network request.
    # action: Valid device animation operation selected by the user.
    def animation_device_request(self, action):
        if self.animation_device_pending:
            return
        scene = ANIMATION_SCENES[self.animation_choice.current()][1]
        self.animation_device_pending = True
        for button in self.animation_device_buttons:
            button.configure(state="disabled")
        self.animation_device_status.configure(text=f"Physical Cody: sending {action}...")
        self.animation_transport.request(scene, action, self.animation_device_complete)

    # self: Tk owner receiving the completed device command.
    # success: Whether the device confirmed application of the request.
    # message: Authenticated result or transport failure details.
    def animation_device_complete(self, success, message):
        self.animation_device_pending = False
        for button in self.animation_device_buttons:
            button.configure(state="normal")
        self.animation_device_status.configure(text=f"Physical Cody: {message}")

    # self: Native avatar presentation with Tk-thread input and host-only audio.
    # image: Writable logical framebuffer before display rotation.
    # now: Current monotonic time, used for both animation and touch reaction.
    # Exception: Propagates native load or drawing errors to the preview tick.
    def draw_mascot(self, image, now):
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
        image.paste(studio_rgb("device_ui", "canvas_black"), (0, 0, SIZE, SIZE))
        if self.animation_scene is None:
            mascot = muse.image(elapsed_ms, reaction_ms)
        else:
            self.advance_animation_clock(now)
            _, _, pose, symbol, working = self.animation_scene
            pose_ms = int(self.animation_elapsed_ms)
            if pose == MusePose.SLEEP:
                pose_ms = min(pose_ms, 1020)
            if self.animation_playing or self.animation_frame is None:
                self.animation_frame = muse.face_image(pose, int(self.animation_elapsed_ms), pose_ms,
                                                       symbol=symbol, working=working)
            mascot = self.animation_frame
        image.paste(mascot, (MUSE_OFFSET, MUSE_OFFSET))
        # The full Home target keeps the mascot's existing tap interaction.
        self.targets["Mascot"] = ((0, 0, SIZE, SIZE), self.pet_mascot)

    # self: Preview owner excluding hidden time and retiring the visible touch reaction.
    def hide_mascot(self):
        if self.muse is not None and self.muse_last_frame_at is not None:
            self.muse.reset_motion()
        self.muse_last_frame_at = None
        self.animation_last_frame_at = None
        self.muse_reaction_ms = None
        self.motion_until = 0.0
        self.motion_sample_at_us = 0
        self.motion_acceleration = (0.0, 0.0)

    # self: Visible avatar receiving a complete short contact admitted by release.
    def pet_mascot(self):
        now = time.monotonic()
        if not 0.03 <= now - self.touch_pressed_at <= 0.45:
            return
        self.muse_reaction_ms = int(self.muse_elapsed_ms)
        if self.audio is not None:
            self.audio.touch()

    # self: Studio preview owning the rendered route and touch targets.
    # snapshot: Current loopback projection, if available.
    # code: Current SIMULATED CCP code, if a pairing attempt is active.
    # now: Monotonic frame time.
    # Exception: Propagates drawing failures to the preview tick.
    def frame(self, snapshot, code, now):
        self.frame_time = now
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
            image.paste(studio_rgb("device_ui", "canvas_black"), (0, 0, SIZE, SIZE))
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
                draw.rectangle((73, 73, 392, 392), outline=studio_rgb("device_ui", "guide_outline"))
        else:
            if wifi_setup:
                self.draw_wifi_setup(image, draw, now)
            elif self.route == "connections":
                self.draw_connections(draw, snapshot, code, now)
            elif self.route == "options":
                self.draw_options(image, draw, now)
            elif self.route == "timer":
                self.draw_local_timer(draw, now)
            elif self.route == "restart":
                self.draw_restart(image, draw)
            elif self.route == "factory_reset":
                self.draw_factory_reset(image, draw)
            elif self.route == "quick_guide":
                self.draw_quick_guide(image, draw)
            elif self.route == "visibility":
                self.draw_visibility(draw, now)
            elif self.route == "display":
                self.screen_title(draw, "Display", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (
                    (f"Brightness  {self.brightness}%", self.change_brightness),
                    ("Auto-rotate  " + ("On" if self.auto_rotate else "Off"), self.change_auto_rotation),
                    ("Orientation  Auto" if self.auto_rotate else f"Orientation  {self.rotation} deg",
                     None if self.auto_rotate else self.change_orientation),
                    ("More...", lambda: self.open("display_rings")),
                ))
                page_marks(draw, 0, 3)
                self.sidebar(draw, -1, self.back)
            elif self.route == "display_rings":
                self.screen_title(draw, "Display & time", OPTIONS_TITLE_TOP)
                page_marks(draw, 1, 3)
                self.draw_option_rows(draw, (
                    ("Assistant quota  " + ("On" if self.show_assistant_quota_ring else "Off"), self.change_assistant_quota_ring),
                    ("Battery ring  " + ("On" if self.show_battery_ring else "Off"), self.change_battery_ring),
                    ("Time", lambda: self.open("display_time")),
                    ("Next restart" if self.birth_preview_queued else "Replay intro",
                     None if self.birth_preview_queued else self.schedule_birth_preview),
                ))
                self.sidebar(draw, -1, self.back)
            elif self.route == "display_time":
                self.screen_title(draw, "Time", OPTIONS_TITLE_TOP)
                page_marks(draw, 2, 3)
                rows = (("Auto time", "On" if self.auto_time else "Off", self.change_auto_time),
                        ("Sync time", "SIMULATED / unavailable", self.request_simulated_time_sync))
                for index, (name, status, action) in enumerate(rows):
                    top = OPTIONS_ROW_TOP + index * SETTINGS_ROW_STEP
                    setup_icon(draw, "clock", (111, top + 28), 28, SETUP_AMBER)
                    draw.text((141, top + 15), name, font=system_font(20), fill=FOREGROUND, anchor="lm")
                    draw.text((141, top + 42), status, font=font(14 if index == 1 else 18),
                              fill=MUTED if index == 1 else WIFI_CONNECTION_COLOR, anchor="lm")
                    draw.line((141, top + SETTINGS_ROW_HEIGHT - 2, 373, top + SETTINGS_ROW_HEIGHT - 2),
                              fill=studio_rgb("device_ui", "row_separator"), width=2)
                    self.targets[name] = ((93, top, 373, top + SETTINGS_ROW_HEIGHT), action)
                if self.simulated_time_notice:
                    label(draw, self.simulated_time_notice, 318, 15, MUTED)
                self.sidebar(draw, -1, self.back)
            elif self.route == "behavior":
                self.screen_title(draw, "Behavior", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (("Dictation  N/A", None),
                    ("Shake standby/wake  " + ("On" if self.shake_to_wake else "Off"), self.change_shake_to_wake),
                    ("Sounds  N/A", None), ("More...", lambda: self.open("more_behavior"))))
                self.sidebar(draw, -1, self.back)
            elif self.route == "more_behavior":
                self.screen_title(draw, "More behavior", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (("Screensaver  " + ("On" if self.screensaver.enabled else "Off"), self.screensaver.toggle),
                    ("Audio  N/A", None), ("More...", lambda: self.open("behavior_more"))))
                self.sidebar(draw, -1, self.back)
            elif self.route == "behavior_more":
                self.screen_title(draw, "More behavior", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (("Quiet hours", lambda: self.open("quiet_hours")),
                    ("Synth Voice", lambda: self.open("synth_voice"))))
                self.sidebar(draw, -1, self.back)
            elif self.route == "quiet_hours":
                self.screen_title(draw, "Quiet hours", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (("Quiet hours  " + ("On" if self.quiet_hours_enabled else "Off"), self.toggle_quiet_hours),
                    ("Start  " + self.quiet_hours_start, lambda: self.edit_quiet_hours(True)),
                    ("End  " + self.quiet_hours_end, lambda: self.edit_quiet_hours(False))))
                label(draw, "SIMULATED", 345, 14, MUTED)
                self.sidebar(draw, -1, self.back)
            elif self.route == "quiet_editor":
                self.screen_title(draw, "Start" if self.quiet_edit_start else "End", OPTIONS_TITLE_TOP)
                hour = f"Hour  -   {self.quiet_edit_hour:02d}   +"
                minute = f"Minute  -   {self.quiet_edit_minute:02d}   +"
                self.draw_option_rows(draw, ((hour, lambda: None), (minute, lambda: None), ("Save", self.save_quiet_time)))
                for index, (caption, hours) in enumerate(((hour, True), (minute, False))):
                    self.targets.pop(caption, None)
                    top = OPTIONS_ROW_TOP + index * SETTINGS_ROW_STEP
                    self.targets[caption + " minus"] = ((93, top, 232, top + SETTINGS_ROW_HEIGHT),
                        lambda selected=hours: self.adjust_quiet_time(selected, -1))
                    self.targets[caption + " plus"] = ((233, top, 373, top + SETTINGS_ROW_HEIGHT),
                        lambda selected=hours: self.adjust_quiet_time(selected, 1))
                label(draw, "SIMULATED", 345, 14, MUTED)
                self.sidebar(draw, -1, self.back)
            elif self.route == "synth_voice":
                self.screen_title(draw, "Synth Voice", OPTIONS_TITLE_TOP)
                self.draw_option_rows(draw, (("Enable Synth Voice  N/A", None),
                                             ("Voice volume  N/A", None),
                                             ("Voice type  N/A", None)))
                self.sidebar(draw, -1, self.back)
            else:
                self.draw_widget(image, draw, snapshot, now)
            if self.safe_guide:
                draw.rectangle((73, 73, 392, 392), outline=studio_rgb("device_ui", "guide_outline"))
        qr_visible = (not event_active and not self.screensaver.active and
                      ((wifi_setup and self.route in (WifiSetupRoute.INSTALL_APP,
                       WifiSetupRoute.HOME_PLATFORM_QR, WifiSetupRoute.HOME_PAIR)) or
                       (self.route == "widgets" and project_link)))
        qr_opacity = max(0.0, min(1.0, (self.qr_chrome_until - now) / 0.5)) if qr_visible else 0.0
        if qr_opacity > 0:
            border = Image.new("RGBA", image.size)
            ImageDraw.Draw(border).ellipse((3, 3, SIZE - 4, SIZE - 4),
                outline=(*studio_rgb("device_ui", "qr_chrome_border"), round(qr_opacity * 255)), width=2)
            image.paste(border, (0, 0), border)
        if not self.standby and (not qr_visible or qr_opacity > 0):
            self.draw_status(image, snapshot, now, qr_opacity if qr_visible else None)
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
            if self.birth_preview_queued:
                self.birth_preview_queued = False
                self.birth_preview_started_at = now
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

    # self: Render an in-memory approximation of the device's first-run intro.
    # now: Monotonic Studio frame time; no device setting is read or written.
    # Returns a rotated image, or None after the simulated introduction completes.
    def birth_preview_image(self, now):
        elapsed_ms = max(0.0, (now - self.birth_preview_started_at) * 1000)
        if elapsed_ms >= 7000:
            self.birth_preview_started_at = None
            self.last_navigation = self.screensaver_idle_since = now
            return None
        white = 255 if elapsed_ms < 850 else max(0, round((2750 - elapsed_ms) * 255 / 1900))
        image = Image.new("RGB", (SIZE, SIZE), (white, white, white))
        if elapsed_ms >= 2800:
            phase = int(elapsed_ms - 2800)
            greeting = elapsed_ms >= 5050
            greeting_ms = int(elapsed_ms - 5050) if greeting else 0
            avatar = self.avatar_preview().face_image(
                MusePose.REPLY if greeting else MusePose.IDLE, phase,
                greeting_ms if greeting else phase,
                reaction_ms=greeting_ms if greeting else NO_REACTION)
            avatar = avatar.resize((256, 256), Image.Resampling.NEAREST)
            avatar = ImageEnhance.Brightness(avatar).enhance(min(1.0, phase / 2150))
            image.paste(avatar, (105, 72))
        if elapsed_ms >= 5050:
            welcome = Image.new("RGB", (SIZE, SIZE))
            screen_title(ImageDraw.Draw(welcome), "Welcome!", 356)
            image = ImageChops.lighter(image, welcome)
        if self.brightness != 100:
            image = ImageEnhance.Brightness(image).enhance(self.brightness / 100)
        return self.display_image(image)

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
            if image is None and self.birth_preview_started_at is not None:
                image = self.birth_preview_image(now)
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
        animated = self.boot or self.birth_preview_started_at is not None or self.screensaver.active or (self.route == "mascot" and not self.standby)
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
        self.gesture = None
        if self.boot is not None or self.birth_preview_started_at is not None or self.standby:
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
        if point is not None:
            self.gesture = (self.route, self.page, self.visibility_page, self.options_page,
                            self.quick_guide_page, point, self.displayed_rotation(), self.event_visible)
            if point[1] < 73:
                # The top edge belongs to the global Home gesture, not a control below it.
                self.touch = None
                return
        if self.event_visible:
            if point is not None:
                self.last_navigation = time.monotonic()
            self.events.press(point, self.observe()[0], self.endpoint)
        elif point is not None:
            name, _ = self.action_at(point)
            self.touch = (self.route, self.page, self.visibility_page, point, name)
            self.touch_pressed_at = self.last_navigation = time.monotonic()

    # self: Preview input owner retaining the original gesture identity.
    # event: Tk pointer sample in the same transformed device coordinate system.
    def drag(self, event):
        point = self.point(event)
        if self.gesture is not None and self.gesture[5][1] < 73:
            return
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

    # self: Preview input owner admitting one completed tap or swipe.
    # event: Tk release sample mapped through the current display orientation.
    # Raises OSError, RuntimeError or ValueError through the existing preview error dialog.
    def release(self, event):
        touch, self.touch = self.touch, None
        gesture, self.gesture = self.gesture, None
        point = self.point(event)
        if self.boot is not None or self.birth_preview_started_at is not None or self.standby or point is None:
            return
        self.last_navigation = self.screensaver_idle_since = time.monotonic()
        try:
            if gesture is not None:
                route, page, visibility, options, guide, origin, rotation, event_visible = gesture
                unchanged = (route, page, visibility, options, guide, rotation, event_visible) == (
                    self.route, self.page, self.visibility_page, self.options_page,
                    self.quick_guide_page, self.displayed_rotation(), self.event_visible)
                dx, dy = point[0] - origin[0], point[1] - origin[1]
                if origin[1] < 73:
                    if unchanged and dy >= 60 and dy * 2 > abs(dx) * 3:
                        if self.event_visible and self.events:
                            self.events.hide()
                            self.event_visible = False
                        self.home()
                    return
                if unchanged and not self.event_visible and abs(dx) >= 48 and abs(dx) * 2 > abs(dy) * 3:
                    step = 1 if dx < 0 else -1
                    if self.route == "options":
                        page = self.options_page + step
                        if 0 <= page < OPTIONS_PAGE_COUNT:
                            self.show_options_page(page)
                        elif page < 0:
                            self.back()
                        return
                    if self.route == "quick_guide":
                        self.move_quick_guide(step)
                        return
                    if self.route == "display" and step > 0:
                        self.open("display_rings")
                        return
                    if self.route == "display_rings" and step > 0:
                        self.open("display_time")
                        return
                    if self.route is WifiSetupRoute.HOME_NETWORKS:
                        pages = (len(SIMULATED_WIFI_NETWORKS) + WIFI_NETWORKS_PER_PAGE - 1) // WIFI_NETWORKS_PER_PAGE
                        page = self.wifi_network_page + step
                        if self.wifi_networks_scanned and 0 <= page < pages:
                            self.wifi_network_page = page
                            self.wifi_selected_index = -1
                            self.wifi_network_scroll_elapsed = 0.0
                            self.wifi_network_scroll_last_draw = None
                        return
                    if self.route in ("visibility", "widgets"):
                        self.move(step)
                        return
                    if self.route is WifiSetupRoute.HOME_INTRO and step > 0:
                        self.open(WifiSetupRoute.HOME_PAIR)
                        return
                    if step < 0 and self.route in ("display", "display_rings", "display_time", "behavior", "more_behavior", "behavior_more", "quiet_hours", "quiet_editor", "synth_voice",
                                                  "connections", "timer", "restart", "factory_reset",
                                                  WifiSetupRoute.INSTALL_APP, WifiSetupRoute.HOME_PLATFORM_QR,
                                                  WifiSetupRoute.HOME_PAIR):
                        self.back()
                        return
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
