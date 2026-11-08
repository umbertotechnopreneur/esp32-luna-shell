"""Device-first Cody Studio preview, isolated from the legacy artwork editor."""

from __future__ import annotations

from enum import Enum, auto
from functools import lru_cache
import json
from math import isqrt
from pathlib import Path
import time
import tkinter as tk
from tkinter import messagebox, ttk
import webbrowser

from PIL import Image, ImageDraw, ImageEnhance, ImageFont, ImageTk

from cody_ccp_event_view import EventView
from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_device_state import (
    InfoPage, LocalTimerModel, LocalTimerPhase, ScreensaverModel, SystemInfoModel, SystemInfoObservation,
    TIMER_MAX_MINUTES, TIMER_MIN_MINUTES,
)
from cody_preview_layout import PreviewLayout, SIZE, enlarge_framebuffer
from cody_screensaver import render_screensaver
from cody_ui_theme import install_theme
from vibeware_boot import VibeWareBoot


CENTER = SIZE // 2
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
PAGES = (
    "Device", "Reset countdown", "Context", "Codex limits", "Today", "Last 7 days",
    "Token history", "Credits", "Codex account", "About",
)
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
UNAVAILABLE_ROWS = {
    "Codex limits": ("Short window", "Weekly window"),
    "Last 7 days": ("Daily tokens",),
}
LEFT = (40, CENTER)
RIGHT = (SIZE - 40, CENTER)


class WifiSetupRoute(Enum):
    OFFLINE = auto()
    NOT_CONNECTED = auto()
    PHONE_GUIDE = auto()
    INSTALL_APP = auto()
    SCAN_TO_PAIR = auto()


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
def draw_control_pill(draw, bounds, palette):
    left, top, right, bottom = bounds
    top_color, bottom_color, outline, _ = palette
    radius = (bottom - top) // 2
    draw.rounded_rectangle(bounds, radius=radius, fill=outline)
    inner_radius = radius - 1
    for y in range(top + 1, bottom):
        dy = max(top + radius - y, y - (bottom - radius), 0)
        inset = inner_radius - isqrt(inner_radius * inner_radius - dy * dy)
        fraction = (y - top) / (bottom - top)
        color = tuple(round(start + (end - start) * fraction)
                      for start, end in zip(top_color, bottom_color))
        draw.line((left + 1 + inset, y, right - 1 - inset, y), fill=color)


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
    if icon in ("wifi", "offline"):
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

    def __init__(self, root, args):
        self.root = root
        self.restore_preview_defaults()
        self.rotation = args.rotation
        self.safe_guide = args.safe_guide
        self.endpoint = None
        self.events = None
        self.source_error = ""
        self.route = WifiSetupRoute.OFFLINE
        self.connections_return = "widgets"
        self.guide_return = WifiSetupRoute.NOT_CONNECTED
        self.pairing_return = WifiSetupRoute.PHONE_GUIDE
        self.pairing_attempted = False
        self.page = 0
        self.options_page = 0
        self.quick_guide_page = 0
        self.settings_page = 0
        self.standby = False
        self.touch = None
        self.event_visible = False
        self.targets = {}
        self.acknowledged = ()
        self.photo = None
        self.timer = None
        self.started_at = self.last_navigation = time.monotonic()
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

        self.start_boot()

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
        root.minsize(SIZE + 48, SIZE + 104)
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.drag)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        root.bind("<Escape>", lambda _: self.back())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.tick()

    # self: Preview window loading the same visible defaults at startup and after reset.
    def restore_preview_defaults(self):
        self.rotation = 0
        self.history_window = 1
        self.visible = set(PAGES)
        self.brightness = 100
        self.shake_to_wake = False
        self.info = SystemInfoModel()
        self.screensaver = ScreensaverModel()
        self.local_timer = LocalTimerModel()
        self.timer_return = "options"

    # self: Preview window playing the standard startup animation.
    def start_boot(self):
        self.boot = self.boot_frame = None
        self.boot_error = ""
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
        self.route = route
        self.screensaver.active = False
        self.last_navigation = time.monotonic()

    # self: Studio navigation owner preserving the menu that opened Connections.
    def open_connections(self):
        self.connections_return = self.route
        self.open("connections")

    # self: Studio navigation owner.
    # previous: Actual caller when leaving the phone guide with Back.
    def open_phone_guide(self, previous):
        self.guide_return = previous
        self.open(WifiSetupRoute.PHONE_GUIDE)

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
        self.screensaver.active = False
        self.touch = None
        self.last_navigation = time.monotonic()

    # self: Preview owner applying the same first-visible-statistics shortcut as firmware.
    def shake(self):
        if self.boot is not None:
            return
        if self.standby and self.shake_to_wake:
            self.toggle_standby()
        if not self.standby and not self.event_visible and self.route == "widgets":
            self.open_statistics()

    # self: Local navigation owner retaining the typed Info subpage.
    # step: Previous (-1) or next (1) action admitted by a visible arrow.
    def move(self, step):
        if self.route == "visibility":
            self.settings_page = (self.settings_page + step) % len(PAGES)
        else:
            if PAGES[self.page] == "About":
                if self.info.move(step):
                    self.touch = None
                    self.last_navigation = time.monotonic()
                    return
                if step > 0:
                    return
            choices = [index for index, page in enumerate(PAGES) if page in self.visible]
            self.page = choices[(choices.index(self.page) + step) % len(choices)]
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
                    self.open("widgets")
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
        self.settings_page = self.options_page = 0
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

    # self: Preview owner returning to the first enabled statistics page.
    def open_statistics(self):
        self.page = next((index for index, page in enumerate(PAGES) if index and page in self.visible), 0)
        self.info.page = InfoPage.SYSTEM
        self.screensaver.active = False
        self.touch = None
        self.open("widgets")

    # self: Preview owner applying visibility while retaining valid top-level and child routes.
    def change_visibility(self):
        page = PAGES[self.settings_page]
        if self.settings_page:
            if page in self.visible:
                self.visible.remove(page)
                if self.page == self.settings_page:
                    self.page = 0
                    self.info.page = InfoPage.SYSTEM
            else:
                self.visible.add(page)

    def change_interval(self):
        self.history_window = (self.history_window + 1) % len(HISTORY_WINDOWS)

    def change_brightness(self):
        self.brightness = self.brightness - 10 if self.brightness > 10 else 100

    def change_orientation(self):
        self.rotation = 0 if self.rotation == 180 else 180

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
    def button(self, draw, name, top, action, *, width=246, height=38, secondary=False,
               destructive=False, icon=None, caption_size=17, center_x=CENTER):
        box = (center_x - width // 2, top, center_x + width // 2, top + height)
        palette = DESTRUCTIVE_CONTROL_PALETTE if destructive else (
            SECONDARY_CONTROL_PALETTE if secondary else CONTROL_PALETTE)
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
    def draw_connections(self, draw, snapshot, code):
        if code or snapshot.pending_pairing:
            label(draw, "Pair Codex", 109, 32)
            label(draw, code or "Awaiting confirmation", 163, 40 if code else 22, ACCENT)
            label(draw, "Share this SIMULATED code in Codex", 224, 15)
            self.button(draw, "Cancel", 301, self.cancel_pairing)
            return
        label(draw, "Connections", OPTIONS_TITLE_TOP, 34)
        connect = (lambda: self.pair(snapshot)) if self.endpoint and snapshot.network_ready else None
        self.draw_connection_action(draw, 148, ("Connect", "Codex"), "link",
                                    CODEX_CONNECTION_COLOR, connect)
        self.draw_connection_action(draw, 318, ("Set up", "Wi-Fi"), "wifi",
                                    WIFI_CONNECTION_COLOR, lambda: self.open_phone_guide("connections"))
        self.sidebar(draw, -1, self.back)

    # self: Studio preview owning the current Wi-Fi setup route and its touch targets.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time for the shared Back fade.
    def draw_wifi_setup(self, draw, now):
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
            setup_icon(draw, "phone", (CENTER, 89), 54, SETUP_ACCENT)
            label(draw, "Wi-Fi setup", 130, 32)
            label(draw, "Get the Espressif app", 186, 17, MUTED)
            label(draw, "Then open it and scan", 212, 17, MUTED)
            label(draw, "the pairing QR.", 238, 17, MUTED)
            self.button(draw, "App download QR", 274, lambda: self.open(WifiSetupRoute.INSTALL_APP),
                        width=280, height=62, icon="qr")
            self.button(draw, "Show pairing QR", 348, self.open_pairing_preview,
                        width=280, height=64, icon="bluetooth")
        elif self.route is WifiSetupRoute.INSTALL_APP:
            setup_icon(draw, "phone", (CENTER, 72), 46, SETUP_ACCENT)
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
                ("Visible screens", lambda: self.open("visibility")),
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
        self.button(draw, "Go home", 382, self.open_statistics, width=220, height=52,
                    icon="home", caption_size=20)
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
            self.button(draw, "Done", 382, lambda: self.show_options_page(OPTIONS_PAGE_COUNT - 1),
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
            self.button(draw, "Done", 321, timer.dismiss, width=240, height=60)
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

    # self: Studio preview owning the selected dashboard page.
    # draw: Logical canvas before rotation.
    # snapshot: Current loopback projection, if available.
    # now: Monotonic frame time for uptime and arrow fading.
    def draw_widget(self, draw, snapshot, now):
        page = PAGES[self.page]
        if page in ("Codex account", "Credits"):
            label(draw, page.upper(), 82, 33, ACCENT)
        elif page != "About":
            label(draw, page, 105, 33)
        if page == "Device":
            label(draw, snapshot.state.value if self.endpoint else "Local mode", 170, 24, ACCENT)
            label(draw, "SIMULATED Wi-Fi " + ("on" if snapshot.network_ready else "off")
                  if self.endpoint else "Loopback is off", 216, 17, MUTED)
            self.button(draw, "Options", 300, self.options)
        elif page == "Context":
            activity = snapshot.activity_valid and snapshot.phase != "none"
            label(draw, snapshot.project if activity and snapshot.project else "No current project", 162, 23, ACCENT)
            label(draw, snapshot.branch if activity and snapshot.branch else "Branch unavailable", 200, 17, MUTED)
            title = snapshot.title if activity and snapshot.title_state == "current" else ""
            label(draw, title or "Chat title unavailable", 235, 17)
            if activity and snapshot.description:
                label(draw, snapshot.description, 273, 16, MUTED)
        elif page == "About":
            self.draw_system_info(draw)
        elif page == "Reset countdown":
            for index, unit in enumerate(("DAYS", "HRS", "MIN", "SEC")):
                x = 113 + index * 80
                draw.rounded_rectangle((x - 35, 166, x + 35, 253), radius=7, fill=(28, 34, 49))
                draw.line((x - 35, 211, x + 35, 211), fill=(53, 64, 85))
                label(draw, "--", 188, 37, x=x, width=70)
                label(draw, unit, 261, 14, MUTED, x=x, width=70)
            label(draw, "Reset time unavailable", 306, 17, MUTED)
        elif page == "Token history":
            label(draw, "No account samples yet", 201, 20, MUTED)
            label(draw, "15 MIN ACCOUNT OBSERVATIONS", 239, 16, MUTED)
            self.button(draw, HISTORY_WINDOWS[self.history_window], 299, self.change_interval)
        elif page == "Credits":
            label(draw, "BALANCE", 139, 18)
            label(draw, "Unavailable", 190, 28, MUTED)
            draw.line((93, 264, 373, 264), fill=(41, 105, 123))
            caption = "RESET VOUCHERS"
            caption_width = draw.textlength(caption, font=font(33))
            left = CENTER - (caption_width + 32) / 2
            draw.rectangle((left, 289, left + 23, 304), outline=MUTED, width=2)
            for top in (292, 299):
                draw.line((left + 16, top, left + 16, top + 3), fill=MUTED)
            for x in (left, left + 23):
                draw.ellipse((x - 2, 295, x + 2, 299), fill=BACKGROUND)
            label(draw, caption, 274, 33, x=left + 32 + caption_width / 2, width=288)
            label(draw, "Unavailable", 328, 22, MUTED)
        elif page in ("Today", "Codex account"):
            label(draw, "Account data unavailable", 213, 20, MUTED)
        else:
            for index, name in enumerate(UNAVAILABLE_ROWS[page]):
                y = 159 + index * 57
                label(draw, name, y, 18)
                label(draw, "Unavailable", y + 24, 16, MUTED)
        opacity = self.navigation_opacity(now)
        if opacity > 0 and (len(self.visible) > 1 or page == "About"):
            self.sidebar(draw, -1, (lambda: self.move(-1)) if opacity == 1 else None, opacity)
            if page != "About" or self.info.page is not InfoPage.PROJECT_LINK:
                self.sidebar(draw, 1, (lambda: self.move(1)) if opacity == 1 else None, opacity)

    # self: Studio preview owning the rendered route and touch targets.
    # snapshot: Current loopback projection, if available.
    # code: Current SIMULATED CCP code, if a pairing attempt is active.
    # now: Monotonic frame time.
    # Exception: Propagates drawing failures to the preview tick.
    def frame(self, snapshot, code, now):
        wifi_setup = isinstance(self.route, WifiSetupRoute)
        image = Image.new("RGB", (SIZE, SIZE), BACKGROUND)
        draw = ImageDraw.Draw(image)
        self.targets = {}
        self.event_visible = False
        event_active = not self.standby and self.events and self.events.visible(snapshot, self.endpoint)
        project_link = PAGES[self.page] == "About" and self.info.page is InfoPage.PROJECT_LINK
        self.screensaver.observe(not self.standby and not event_active and self.route == "widgets" and not project_link,
                                 now - self.last_navigation)
        if self.standby:
            image.paste((0, 0, 0), (0, 0, SIZE, SIZE))
        elif event_active:
            self.event_visible = True
            self.last_navigation = now
            self.events.render_device(image, snapshot, label, arrow)
        elif self.screensaver.active:
            image = render_screensaver(int((now - self.last_navigation - 15.0) * 8)).copy()
        else:
            label(draw, "SIMULATED", 28 if wifi_setup else 46, 14, MUTED)
            if wifi_setup:
                self.draw_wifi_setup(draw, now)
            elif self.route == "connections":
                self.draw_connections(draw, snapshot, code)
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
                label(draw, "Visible screens", 103, 32)
                name = PAGES[self.settings_page]
                label(draw, name, 180, 24)
                if self.settings_page:
                    self.button(draw, "Visible" if name in self.visible else "Hidden", 275, self.change_visibility)
                else:
                    label(draw, "Always visible", 290, 17, ACCENT)
                self.sidebar(draw, -1, lambda: self.move(-1))
                self.sidebar(draw, 1, lambda: self.move(1))
                self.button(draw, "Done", 340, self.back)
            elif self.route == "display":
                label(draw, "Display", OPTIONS_TITLE_TOP, 34)
                self.draw_option_rows(draw, (
                    (f"Brightness  {self.brightness}%", self.change_brightness),
                    (f"Orientation  {self.rotation} deg", self.change_orientation),
                ))
                self.sidebar(draw, -1, self.back)
            elif self.route == "behavior":
                label(draw, "Behavior", OPTIONS_TITLE_TOP, 34)
                self.draw_option_rows(draw, (
                    ("Dictation  N/A", None),
                    ("Shake to wake  " + ("On" if self.shake_to_wake else "Off"), self.change_shake_to_wake),
                    ("More behavior", lambda: self.open("more_behavior")),
                ))
                self.sidebar(draw, -1, self.back)
            elif self.route == "more_behavior":
                label(draw, "More behavior", OPTIONS_TITLE_TOP, 34)
                self.draw_option_rows(draw, (
                    ("Screensaver  " + ("On" if self.screensaver.enabled else "Off"), self.screensaver.toggle),
                ))
                self.sidebar(draw, -1, self.back)
            else:
                self.draw_widget(draw, snapshot, now)
            if self.safe_guide:
                draw.rectangle((73, 73, 392, 392), outline=(62, 91, 113))
        if not self.standby and self.brightness != 100:
            image = ImageEnhance.Brightness(image).enhance(self.brightness / 100)
        return self.display_image(image)

    def display_image(self, image):
        mask = Image.new("L", (SIZE, SIZE))
        ImageDraw.Draw(mask).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
        image.putalpha(mask)
        return rotate(image, self.rotation)

    def boot_image(self, now):
        elapsed_ms = (now - self.boot_started_at) * 1000
        if elapsed_ms >= self.boot_cycle_ms:
            self.boot = None
            self.boot_frame = None
            self.last_navigation = now
            return None
        index = self.boot.frame_index(elapsed_ms)
        if self.boot_frame is None or self.boot_frame[0] != index:
            self.boot_frame = (index, self.display_image(self.boot.image(elapsed_ms)))
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
                self.page = 0
                self.open("widgets")
            status = self.source_error or self.boot_error or snapshot.last_error
            if not status:
                if self.endpoint:
                    wifi_state = "on" if snapshot.network_ready else "off"
                    status = f"Loopback Wi-Fi {wifi_state} / {snapshot.state.value}"
                else:
                    status = "Offline preview / use -Ccp to pair Codex"
            self.status.configure(text="SIMULATED / " + status)
            image = self.boot_image(now) if self.boot else None
            if image is None:
                image = self.frame(snapshot, code, now)
        except (OSError, RuntimeError, ValueError) as error:
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
        self.timer = self.root.after(40 if self.boot else 125 if self.screensaver.active else 100, self.tick)

    def point(self, event):
        point = self.layout.to_device_pixel(event.x, event.y, self.rotation)
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
        if point is not None and self.screensaver.active:
            self.screensaver.active = False
            self.last_navigation = time.monotonic()
            self.touch = None
            self.targets.clear()
            self.acknowledged = ()
            return
        if self.event_visible:
            self.events.press(point, self.observe()[0], self.endpoint)
        elif point is not None:
            name, _ = self.action_at(point)
            self.touch = (self.route, self.page, self.settings_page, point, name)
            self.last_navigation = time.monotonic()

    def drag(self, event):
        point = self.point(event)
        if self.event_visible:
            self.events.drag(point)
        elif self.touch is not None:
            origin = self.touch[3]
            if point is None or sum((end - start) ** 2 for start, end in zip(origin, point)) >= 144:
                self.touch = None

    def release(self, event):
        touch, self.touch = self.touch, None
        point = self.point(event)
        if self.boot is not None or self.standby or point is None:
            return
        try:
            if self.event_visible:
                self.events.release(point, self.observe()[0], self.endpoint)
                return
            if not touch or touch[:3] != (self.route, self.page, self.settings_page):
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
        if self.timer is not None:
            self.root.after_cancel(self.timer)
        try:
            if self.endpoint:
                self.endpoint.close()
        finally:
            self.root.destroy()
