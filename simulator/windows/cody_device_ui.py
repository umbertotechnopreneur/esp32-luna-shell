"""Device-first Cody Studio preview, isolated from the legacy artwork editor."""

from __future__ import annotations

from functools import lru_cache
import json
from pathlib import Path
import time
import tkinter as tk
from tkinter import messagebox, ttk
import webbrowser

from PIL import Image, ImageDraw, ImageEnhance, ImageFont, ImageTk

from cody_ccp_event_view import EventView
from cody_ccp_simulator import LinkState, SimulatorEndpoint, SimulatorSnapshot
from cody_preview_layout import PreviewLayout, SIZE, enlarge_framebuffer
from cody_ui_theme import install_theme
from vibeware_boot import VibeWareBoot


CENTER = SIZE // 2
BACKGROUND = (11, 15, 24)
FOREGROUND = (239, 236, 224)
MUTED = (153, 168, 186)
ACCENT = (122, 218, 222)
NAVIGATION_IDLE_SECONDS = 10
NAVIGATION_FADE_SECONDS = 0.35
PAGES = (
    "Device", "Reset countdown", "Codex limits", "Today", "Last 7 days",
    "Token history", "Credits", "Codex account", "Context", "About",
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
HISTORY_WINDOWS = ("1 hour", "5 hours", "24 hours")
UNAVAILABLE_ROWS = {
    "Codex limits": ("Short window", "Weekly window"),
    "Today": ("Total tokens", "Current and best streak"),
    "Last 7 days": ("Daily tokens",),
    "Credits": ("Credit balance", "Reset vouchers"),
    "Codex account": ("Plan", "ChatGPT account", "Workspace messages"),
}
LEFT = (40, CENTER)
RIGHT = (SIZE - 40, CENTER)


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
        self.rotation = args.rotation
        self.safe_guide = args.safe_guide
        self.endpoint = None
        self.events = None
        self.source_error = ""
        self.boot_error = ""
        self.boot = None
        self.boot_frame = None
        self.route = "connections"
        self.pairing_attempted = False
        self.page = 0
        self.options_page = 0
        self.settings_page = 0
        self.history_window = 1
        self.visible = set(PAGES)
        self.standby = False
        self.brightness = 100
        self.shake_to_wake = False
        self.touch = None
        self.event_visible = False
        self.targets = {}
        self.acknowledged = ()
        self.photo = None
        self.timer = None
        self.started_at = self.last_navigation = time.monotonic()
        if args.ccp:
            endpoint = SimulatorEndpoint()
            try:
                endpoint.start()
                self.events = EventView()
                self.endpoint = endpoint
                snapshot, _ = endpoint.observe()
                self.pairing_attempted = snapshot.pending_pairing
                if snapshot.retained_pairing and not self.pairing_attempted:
                    self.route = "widgets"
            except (OSError, RuntimeError, ValueError) as error:
                endpoint.close()
                self.source_error = str(error)

        project = Path(__file__).resolve().parents[2]
        try:
            manifest = project / "assets/branding/VibeWare/generated/manifest.json"
            self.boot_cycle_ms = json.loads(manifest.read_text(encoding="utf-8"))["cycle_duration_ms"]
            self.boot = VibeWareBoot(project, cody_companion=True)
            self.boot_started_at = time.monotonic()
        except (OSError, RuntimeError, ValueError, AttributeError, KeyError) as error:
            self.boot_error = f"VibeWare boot unavailable: {error}"

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
        self.status = ttk.Label(root, style="Status.TLabel")
        self.status.pack(pady=(0, 12))
        root.minsize(SIZE + 48, SIZE + 104)
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.drag)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        root.bind("<Escape>", lambda _: self.back())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.tick()

    def observe(self):
        return self.endpoint.observe() if self.endpoint else (SimulatorSnapshot(), "")

    def open(self, route):
        self.route = route
        self.last_navigation = time.monotonic()

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

    def toggle_standby(self):
        if self.boot is not None:
            return
        self.standby = not self.standby
        self.touch = None
        self.last_navigation = time.monotonic()

    def shake(self):
        if self.boot is not None:
            return
        if self.standby and self.shake_to_wake:
            self.toggle_standby()
        if not self.standby and not self.event_visible and self.route == "widgets":
            self.page = 0
            self.last_navigation = time.monotonic()

    def move(self, step):
        if self.route == "visibility":
            self.settings_page = (self.settings_page + step) % len(PAGES)
        else:
            choices = [index for index, page in enumerate(PAGES) if page in self.visible]
            if "About" in self.visible:
                choices.append(len(PAGES))
            self.page = choices[(choices.index(self.page) + step) % len(choices)]
        self.last_navigation = time.monotonic()

    # self: Active Studio preview returning to the calling route or menu page.
    def back(self):
        if self.boot is None and not self.standby and not self.event_visible:
            if self.route in ("visibility", "connections", "display", "behavior"):
                self.show_options_page(self.options_page)
            elif self.route == "options":
                if self.options_page:
                    self.show_options_page(0)
                else:
                    self.open("widgets")

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

    def forget(self):
        if messagebox.askyesno("Forget SIMULATED pairing", "Forget the saved link?", parent=self.root):
            self.endpoint.act("forget", True)

    def open_statistics(self):
        self.page = next((index for index, page in enumerate(PAGES) if index and page in self.visible), 0)
        self.open("widgets")

    def change_visibility(self):
        page = PAGES[self.settings_page]
        if self.settings_page:
            if page in self.visible:
                self.visible.remove(page)
                if self.page == self.settings_page or (page == "About" and self.page == len(PAGES)):
                    self.page = 0
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

    def button(self, draw, name, top, action, *, width=246, height=38):
        box = (CENTER - width // 2, top, CENTER + width // 2, top + height)
        draw.rounded_rectangle(box, radius=height // 2, fill=(32, 43, 58), outline=(64, 80, 101))
        label(draw, name, top + (height - 17) // 2, 17, FOREGROUND if action else MUTED, width=width - 20)
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

    def draw_connections(self, draw, snapshot, code):
        if code or snapshot.pending_pairing:
            label(draw, "Pair Codex", 109, 32)
            label(draw, code or "Awaiting confirmation", 163, 40 if code else 22, ACCENT)
            label(draw, "Share this SIMULATED code in Codex", 224, 15)
            self.button(draw, "Cancel", 301, self.cancel_pairing)
            return
        label(draw, "Connections", 104, 34)
        status = snapshot.state.value if self.endpoint else "Loopback is off"
        label(draw, status, 155, 20, ACCENT)
        if self.endpoint and not snapshot.network_ready:
            label(draw, "SIMULATED Wi-Fi offline", 184, 16, MUTED)
        self.button(draw, "Connect Codex", 213,
                    (lambda: self.pair(snapshot)) if self.endpoint and snapshot.network_ready else None)
        self.button(draw, "Wi-Fi", 263, None if self.endpoint is None else
                    lambda: self.endpoint.act("network", not snapshot.network_ready))
        if snapshot.retained_pairing:
            self.button(draw, "Forget pairing", 313, self.forget)
        else:
            self.button(draw, "Later", 313, lambda: self.open("widgets"))
        self.button(draw, "Options", 370, self.options, width=150, height=33)

    # self: Studio preview owning the active menu page and touch targets.
    # draw: Logical canvas before rotation.
    # now: Monotonic frame time for the lateral arrow fade.
    def draw_options(self, draw, now):
        label(draw, f"Options {self.options_page + 1}/2", 103, 34)
        if self.options_page == 0:
            rows = (
                ("Connections", lambda: self.open("connections")),
                ("Codex stats", self.open_statistics),
                ("Visible screens", lambda: self.open("visibility")),
            )
        else:
            rows = (
                ("Display", lambda: self.open("display")),
                ("Behavior", lambda: self.open("behavior")),
            )
        for index, (name, action) in enumerate(rows):
            self.button(draw, name, 194 + index * 60, action, width=240, height=50)
        self.button(draw, "Go home", 390, self.open_statistics, width=220, height=44)
        opacity = self.navigation_opacity(now)
        if opacity > 0:
            self.sidebar(draw, -1, self.back if opacity == 1 else None, opacity)
            if self.options_page == 0:
                next_page = (lambda: self.show_options_page(1)) if opacity == 1 else None
                self.sidebar(draw, 1, next_page, opacity)

    def open_about_url(self):
        if not webbrowser.open_new_tab(ABOUT_URL):
            raise RuntimeError("Could not open the Cody project page")

    def draw_about_qr(self, draw):
        module = 5
        quiet = 4 * module
        width = len(ABOUT_QR) * module + quiet * 2
        left = CENTER - width // 2
        top = 147
        draw.rectangle((left, top, left + width - 1, top + width - 1), fill=(255, 255, 255))
        for row, modules in enumerate(ABOUT_QR):
            y = top + quiet + row * module
            for column, value in enumerate(modules):
                if value == "1":
                    x = left + quiet + column * module
                    draw.rectangle((x, y, x + module - 1, y + module - 1), fill=(0, 0, 0))
        self.button(draw, "Open link", 370, self.open_about_url, width=155, height=33)

    # self: Studio preview owning the selected dashboard page.
    # draw: Logical canvas before rotation.
    # snapshot: Current loopback projection, if available.
    # now: Monotonic frame time for uptime and arrow fading.
    def draw_widget(self, draw, snapshot, now):
        page = PAGES[self.page] if self.page < len(PAGES) else ABOUT_INFO
        label(draw, page, 105, 27 if page == ABOUT_INFO else 33)
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
            hours, seconds = divmod(int(now - self.started_at), 3600)
            minutes, seconds = divmod(seconds, 60)
            label(draw, "Studio uptime", 159, 17, MUTED)
            label(draw, f"{hours:02d}:{minutes:02d}:{seconds:02d}", 185, 23, ACCENT)
            label(draw, "Firmware version unavailable", 232, 17, MUTED)
            label(draw, "Firmware build date unavailable", 269, 17, MUTED)
            label(draw, f"ID {snapshot.device_id}" if snapshot.device_id else "Device ID unavailable", 307, 16)
        elif page == ABOUT_INFO:
            self.draw_about_qr(draw)
        elif page == "Reset countdown":
            for index, unit in enumerate(("DAYS", "HRS", "MIN", "SEC")):
                x = 113 + index * 80
                draw.rounded_rectangle((x - 35, 166, x + 35, 253), radius=7, fill=(28, 34, 49))
                draw.line((x - 35, 211, x + 35, 211), fill=(53, 64, 85))
                label(draw, "--", 188, 37, x=x, width=70)
                label(draw, unit, 261, 14, MUTED, x=x, width=70)
            label(draw, "Reset time unavailable", 306, 17, MUTED)
        elif page == "Token history":
            label(draw, "No measured intervals", 201, 20, MUTED)
            self.button(draw, HISTORY_WINDOWS[self.history_window], 299, self.change_interval)
        else:
            for index, name in enumerate(UNAVAILABLE_ROWS[page]):
                y = 159 + index * 57
                label(draw, name, y, 18)
                label(draw, "Unavailable", y + 24, 16, MUTED)
        opacity = self.navigation_opacity(now)
        if opacity > 0 and len(self.visible) > 1:
            self.sidebar(draw, -1, lambda: self.move(-1), opacity)
            self.sidebar(draw, 1, lambda: self.move(1), opacity)

    # self: Studio preview owning the rendered route and touch targets.
    # snapshot: Current loopback projection, if available.
    # code: Current SIMULATED CCP code, if a pairing attempt is active.
    # now: Monotonic frame time.
    # Exception: Propagates drawing failures to the preview tick.
    def frame(self, snapshot, code, now):
        image = Image.new("RGB", (SIZE, SIZE), BACKGROUND)
        draw = ImageDraw.Draw(image)
        self.targets = {}
        self.event_visible = False
        if self.standby:
            image.paste((0, 0, 0), (0, 0, SIZE, SIZE))
        elif self.events and self.events.visible(snapshot, self.endpoint):
            self.event_visible = True
            self.events.render_device(image, snapshot, label, arrow)
        else:
            label(draw, "SIMULATED", 46, 14, MUTED)
            if self.route == "connections":
                self.draw_connections(draw, snapshot, code)
            elif self.route == "options":
                self.draw_options(draw, now)
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
                label(draw, "Display", 110, 34)
                self.button(draw, f"Brightness  {self.brightness}%", 170, self.change_brightness)
                self.button(draw, f"Orientation  {self.rotation} deg", 222, self.change_orientation)
                self.sidebar(draw, -1, self.back)
            elif self.route == "behavior":
                label(draw, "Behavior", 110, 34)
                self.button(draw, "Dictation  N/A", 170, None)
                self.button(draw, "Shake to wake  " + ("On" if self.shake_to_wake else "Off"),
                            222, self.change_shake_to_wake)
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

    def tick(self):
        now = time.monotonic()
        try:
            snapshot, code = self.observe()
            if self.pairing_attempted and snapshot.state == LinkState.CONNECTED:
                self.pairing_attempted = False
                self.page = 0
                self.open("widgets")
            status = self.source_error or self.boot_error or snapshot.last_error or (
                "Authenticated loopback" if self.endpoint else "Offline preview / use -Ccp to pair"
            )
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
        self.timer = self.root.after(40 if self.boot else 100, self.tick)

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

    def press(self, event):
        point = self.point(event)
        if self.boot is not None or self.standby:
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
