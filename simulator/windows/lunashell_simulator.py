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

"""ESP32 Luna Shell round-device UI simulator for Windows.

The simulator mirrors the firmware flow without pretending to emulate physical
drivers. Mouse clicks stand in for touch and deterministic scenarios provide
synthetic PMU, IMU, TOF, GPS, Wi-Fi, BLE, SD, and logger data."""

from __future__ import annotations

import sys
import time
import tkinter as tk
import webbrowser
import winsound
from collections import deque
from pathlib import Path

from PIL import Image, ImageTk

from device_model import (
    ScenarioEngine,
    ScenarioValidationError,
    SemanticActionError,
    load_scenario,
    scenario_names,
)
from lunashell_audio_pages import LunaShellAudioPages
from lunashell_chrome import LunaShellChrome
from lunashell_cli import _pump_capture_events, run_cli
from lunashell_device_pages import LunaShellDevicePages
from lunashell_navigation import LunaShellNavigation
from lunashell_pairing_pages import LunaShellPairingPages
from lunashell_sensor_pages import LunaShellSensorPages, blend_hex_color
from lunashell_settings_pages import LunaShellSettingsPages
from lunashell_state import (
    CanonicalLogger,
    PAIRING_PREVIEWS,
    PairingPreview,
    ScenarioView,
    component_current,
    component_status,
    seconds_label,
)
# Preserve the original entry module's public palette and geometry imports.
from lunashell_style import (
    BACKGROUND,
    BLUE,
    CIRCULAR_CONTENT_INSET,
    COMMAND_ARC_RISE_LINEAR,
    COMMAND_ARC_RISE_QUADRATIC,
    COMMAND_BUTTON_GAP,
    COMMAND_CENTER_X,
    COMMAND_DIAMETER_STEP,
    COMMAND_MAIN_CENTER_Y,
    COMMAND_MAIN_DIAMETER,
    COMMAND_MIN_DIAMETER,
    COMMAND_SCREENS,
    CONTROL_BORDER_PX,
    DANGER_BORDER,
    DANGER_BOTTOM,
    DANGER_GLOW,
    DANGER_TOP,
    DISPLAY_SIZE,
    EDGE_COMMAND_DIAMETER,
    FIXED_CANVAS_SCREENS,
    FONT_BODY,
    FONT_DISPLAY,
    FONT_TECH,
    LEG_FADE_IN_SECONDS,
    LEG_FADE_OUT_SECONDS,
    LEG_PULSE_SECONDS,
    MUTED,
    MUTED_BRIGHT,
    NAVY,
    NEUTRAL_BORDER,
    NEUTRAL_BOTTOM,
    NEUTRAL_GLOW,
    NEUTRAL_TOP,
    PAIRING_SCREENS,
    PRIMARY_BORDER,
    PRIMARY_BOTTOM,
    PRIMARY_GLOW,
    PRIMARY_TOP,
    SAFE_ORIGIN,
    SAFE_SIZE,
    STATUS_EDGE,
    STATUS_FAULT,
    STATUS_GLOW,
    STATUS_MISSING,
    STATUS_READY,
    STATUS_WARNING,
    SUCCESS_BORDER,
    SUCCESS_BOTTOM,
    SUCCESS_GLOW,
    SUCCESS_TOP,
    SURFACE,
    TEXT,
    TRACK,
    WARNING_BORDER,
    WARNING_BOTTOM,
    WARNING_GLOW,
    WARNING_TOP,
    WIDE_PAGE_SCREENS,
    command_diameter,
    command_geometry,
)
from runtime_paths import resolve_runtime_output
from vibeware_boot import VibeWareBoot


# Resolve assets in both source runs and PyInstaller bundles.
def resource_path(relative: str) -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
    return base / relative


class LUNASHELLSimulator(
    LunaShellChrome,
    LunaShellDevicePages,
    LunaShellSettingsPages,
    LunaShellPairingPages,
    LunaShellAudioPages,
    LunaShellSensorPages,
    LunaShellNavigation,
):
    # self: Host controller keeping desktop branding outside the device canvas.
    # root: Tk window hosting the simulator.
    # engine: Scenario engine providing explicitly synthetic state.
    # play_sound: Whether to enable the existing host audio preview.
    # idle_state: Initial simulated power/display state.
    # Raises tk.TclError if host widgets cannot be constructed.
    # Raises OSError if the bundled artwork cannot be read.
    def __init__(
        self,
        root: tk.Tk,
        engine: ScenarioEngine,
        play_sound: bool = True,
        idle_state: str = "active",
    ) -> None:
        self.root = root
        # The simulator uses pixel-based firmware geometry, so keep Tk font
        # sizing deterministic instead of inheriting Windows display scaling.
        self.root.tk.call("tk", "scaling", 1.0)
        self.engine = engine
        self.scenario = ScenarioView(engine)
        self.idle_state = idle_state
        self.logger = CanonicalLogger(lambda: self.engine.elapsed_ms)
        if idle_state == "standby" and self.engine.snapshot()["power"]["state"] == "active":
            self.engine.action("press:pwr")
        elif idle_state == "fullscreen" and not self.engine.snapshot()["power"]["fullscreen_active"]:
            self.engine.action("press:boot")
        self._build_project_header()
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
        self.leg_display_acceleration = [0.0, 0.0, 0.0]
        self.leg_display_angular_velocity = [0.0, 0.0, 0.0]
        self.leg_previous_imu_sample_at_us = 0
        self.leg_previous_imu_values: tuple[tuple[float, ...], tuple[float, ...]] | None = None
        self.leg_movement_history: deque[float] = deque(maxlen=32)
        self.leg_current_intensity = 0.0
        self.leg_acc_pulse_at: float | None = None
        self.leg_rot_pulse_at: float | None = None
        self.leg_indicator_cycle_started_at: dict[str, float | None] = {
            "acc": None, "rot": None,
        }
        self.leg_indicator_levels = {"acc": 0.0, "rot": 0.0}
        self.leg_alert_level = 0.0
        self.leg_alert_color = "#ffd291"
        self.leg_animation_last_at = time.monotonic()
        self.leg_navigation_visible = True
        self.leg_navigation_last_activity_at = time.monotonic()
        self.performance_mode = False
        self.battery_return_screen = "home"
        self.tof_settings_return_screen = "tof"
        self.pending_action: str | None = None
        self.confirmation_return_screen = "home"
        self.result_message = "Confirmed"
        self.ready = False
        self.safe_guide = False
        self.timeline_index = 0
        self.boot_image: tk.PhotoImage | None = None
        self.boot_branding = None
        self.boot_frame_index = 0
        self.boot_animation_timer = None
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

    # self: Host controller retaining the project icon and initiative link image.
    # Raises tk.TclError if Tk cannot construct the desktop header.
    # Raises OSError if either bundled source image cannot be read.
    def _build_project_header(self) -> None:
        header = tk.Frame(self.root, background="#101820")
        header.pack(fill="x", padx=18, pady=(12, 0))
        identity = tk.Frame(header, background="#101820")
        identity.pack(side="left")
        tk.Label(identity, text="ESP32 Luna Shell", background="#101820", foreground=TEXT,
                 font=("Consolas", 14, "bold")).pack(anchor="w")
        tk.Label(identity, text="Participates in the VibeWare initiative",
                 background="#101820", foreground=MUTED_BRIGHT,
                 font=("Consolas", 9)).pack(anchor="w")
        with Image.open(resource_path("simulator/windows/assets/branding/vibeware-logo.png")) as logo:
            self.initiative_logo = ImageTk.PhotoImage(logo.resize((56, 56), Image.Resampling.NEAREST))
        with Image.open(resource_path("assets/branding/LUNASHELL/generated/LUNASHELL-boot-466x466.png")) as logo:
            self.project_logo = ImageTk.PhotoImage(logo.resize((64, 64), Image.Resampling.LANCZOS))
        self.root.iconphoto(True, self.project_logo)
        tk.Button(header, image=self.initiative_logo, text="Manifesto", compound="top",
                  command=self._open_vibeware_manifesto, cursor="hand2", takefocus=True,
                  background="#101820", foreground=TEXT, activebackground=SURFACE,
                  activeforeground=TEXT, relief="flat", borderwidth=0,
                  highlightthickness=1).pack(side="right")

    # self: Host controller retaining and reporting desktop browser failures.
    def _open_vibeware_manifesto(self) -> None:
        try:
            if not webbrowser.open_new_tab("https://umbertogiacobbi.biz/vibeware/manifesto"):
                raise RuntimeError("The default browser rejected the VibeWare manifesto link")
        except (OSError, webbrowser.Error, RuntimeError) as error:
            self.last_link_error = error
            print(f"[luna-ui] Open MANIFESTO failed: {error!r}", file=sys.stderr, flush=True)

    def _snapshot(self) -> dict:
        return self.engine.snapshot()

    def _wireless_running(self) -> bool:
        snapshot = self._snapshot()
        wifi = snapshot["radios"]["wifi"]
        ble = snapshot["radios"]["ble"]
        return bool(wifi["started"] or component_current(ble))

    def _storage_format_allowed(self) -> bool:
        storage = self._snapshot()["storage"]
        return bool(
            storage["format_required"]
            and storage["mount_outcome"] == "format-required"
            and storage["card_response_verified"]
            and storage["cleanup_complete"]
            and storage["cleanup_error"] == "ESP_OK"
            and not storage["poisoned"]
            and not storage["mounted"]
            and not storage["authorization_consumed"]
        )

    def _apply_action(self, name: str, **params) -> bool:
        try:
            self.engine.action(name, **params)
        except SemanticActionError as error:
            self.logger.info(
                "LUNASHELL_simulator",
                f"Synthetic action {name} rejected: {error}",
            )
            return False
        self.logger.info(
            "LUNASHELL_simulator",
            f"Synthetic action {name} applied; generation={self.engine.generation.sequence}",
        )
        return True

    # self: Synthetic Windows UI owner; resources never enter firmware snapshots.
    # play_sound: Explicit simulator sound preference.
    # Raises OSError or ValueError for unavailable or inconsistent approved artwork.
    def _load_assets(self, play_sound: bool) -> None:
        self.boot_branding = VibeWareBoot(resource_path("assets").parent)
        sound_path = resource_path("assets/sfx/generated/lunashell-boot.wav")
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

    # self: Synthetic UI owner; retain the active frame when a guide redraw occurs.
    # Raises tk.TclError for unavailable Canvas or timer resources.
    def show_boot(self) -> None:
        if "SYNTHETIC" not in self.root.title():
            self.root.title(self.root.title() + " / SYNTHETIC")
        self.screen = "boot"
        self._clear()
        self.boot_image = ImageTk.PhotoImage(self.boot_branding.frames[self.boot_frame_index])
        self.canvas.create_image(233, 233, image=self.boot_image, tags=("ui", "boot_branding"))
        self._draw_safe_guide()
        if self.boot_animation_timer is None:
            self.boot_animation_timer = self.root.after(
                self.boot_branding.durations[self.boot_frame_index], self._advance_boot_branding)

    # self: Synthetic boot owner; complete an original GIF cycle before automatic handoff.
    # Raises tk.TclError if the destination UI has been destroyed.
    def _advance_boot_branding(self) -> None:
        self.boot_animation_timer = None
        if self.screen != "boot":
            return
        if self.boot_frame_index + 1 == len(self.boot_branding.frames) and self.ready:
            self.logger.info("VIBEWARE_boot", "Synthetic full cycle completed; opening Luna Shell")
            self.show_home()
            return
        self.boot_frame_index = (self.boot_frame_index + 1) % len(self.boot_branding.frames)
        self.show_boot()

    def _refresh_log(self) -> None:
        if self.screen == "boot":
            self.canvas.itemconfigure("boot_log", text=self.logger.snapshot(count=5, max_chars=58))
        elif self.screen in {"leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings"}:
            self._refresh_leg_visuals()
            self._refresh_leg_navigation()
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

    # Present deterministic, snapshot-backed startup events without drivers.
    # self: Synthetic scenario owner; startup readiness is independent of animation time.
    # Raises tk.TclError when the window or scheduled callback becomes unavailable.
    def _advance_timeline(self) -> None:
        snapshot = self._snapshot()
        touch = snapshot["ui"]["touch"]
        imu = snapshot["imu"]
        wifi = snapshot["radios"]["wifi"]
        ble = snapshot["radios"]["ble"]

        if component_current(touch):
            touch_message = "Touch CST9217: synthetic input ready"
        else:
            touch_state, _ = component_status(touch)
            touch_message = f"Touch CST9217: synthetic input {touch_state}"

        if component_current(imu):
            acceleration = imu["acceleration_mps2"]
            imu_message = (
                "IMU QMI8658: synthetic acceleration "
                f"x={acceleration[0]:+.2f} y={acceleration[1]:+.2f} "
                f"z={acceleration[2]:+.2f}m/s2"
            )
        else:
            imu_state, _ = component_status(imu)
            imu_message = f"IMU QMI8658: synthetic sample {imu_state}"

        if component_current(wifi):
            wifi_state = "connected" if wifi["connected"] else "started"
            wifi_message = f"Wi-Fi: synthetic STA {wifi_state}"
        elif not wifi["started"] and wifi["last_error"] == "ESP_ERR_INVALID_STATE":
            credentials = "configured" if wifi["credentials_configured"] else "missing"
            wifi_message = f"Wi-Fi: synthetic STA stopped, credentials {credentials}"
        else:
            wifi_state, _ = component_status(wifi)
            wifi_message = f"Wi-Fi: synthetic STA {wifi_state}"

        if component_current(ble):
            ble_state = (
                "connected"
                if ble["connected"]
                else "advertising"
                if ble["advertising"]
                else "ready"
            )
            ble_message = f"BLE: synthetic radio {ble_state} as {ble['device_name']}"
        elif ble["last_error"] == "ESP_ERR_INVALID_STATE":
            ble_message = "BLE: synthetic radio stopped"
        else:
            ble_state, _ = component_status(ble)
            ble_message = f"BLE: synthetic radio {ble_state}"

        events = [
            ("LUNASHELL_logger", "Canonical logger online: console + RAM ring"),
            ("LUNASHELL_main", self.scenario.sd_message),
            ("LUNASHELL_boot", "Synthetic VibeWare floppy boot online; logs remain outside the artwork"),
            ("LUNASHELL_main", touch_message),
            ("LUNASHELL_hw", self._battery_timeline_message()),
            ("LUNASHELL_hw", imu_message),
            ("LUNASHELL_hw", self.scenario.gps_message),
            ("LUNASHELL_radios", wifi_message),
            ("LUNASHELL_radios", ble_message),
            ("LUNASHELL_main", self.scenario.final_message),
        ]
        if self.timeline_index < len(events):
            tag, message = events[self.timeline_index]
            self.logger.info(tag, message)
            self.timeline_index += 1
            self.root.after(430, self._advance_timeline)
            return

        self.ready = True
        if snapshot["last_update_complete"]:
            completion = "Synthetic snapshot ready; awaiting the complete VibeWare cycle"
        else:
            completion = (
                "Synthetic snapshot incomplete; real scenario error retained: "
                f"{snapshot['last_update_error']}"
            )
        self.logger.info("LUNASHELL_boot", completion)

    # self: Synthetic device owner; restart the logo without leaving an old timer active.
    # Raises scenario or Tk errors without substituting a successful reboot.
    def reboot(self) -> None:
        if self.boot_animation_timer is not None:
            self.root.after_cancel(self.boot_animation_timer)
            self.boot_animation_timer = None
        self.boot_frame_index = 0
        source = self.engine.scenario.source
        self.engine = ScenarioEngine.from_scenario(source)
        self.scenario = ScenarioView(self.engine)
        self.idle_state = "active"
        self.ready = False
        self.timeline_index = 0
        self.battery_return_screen = "home"
        self.tof_settings_return_screen = "tof"
        self.pending_action = None
        self.confirmation_return_screen = "home"
        self.result_message = "Confirmed"
        self.leg_display_acceleration[:] = [0.0, 0.0, 0.0]
        self.leg_display_angular_velocity[:] = [0.0, 0.0, 0.0]
        self.leg_previous_imu_sample_at_us = 0
        self.leg_previous_imu_values = None
        self.leg_movement_history.clear()
        self.leg_current_intensity = 0.0
        self.leg_acc_pulse_at = None
        self.leg_rot_pulse_at = None
        self.leg_indicator_cycle_started_at = {"acc": None, "rot": None}
        self.leg_indicator_levels = {"acc": 0.0, "rot": 0.0}
        self.leg_alert_level = 0.0
        self.leg_alert_color = "#ffd291"
        self.leg_animation_last_at = time.monotonic()
        self.leg_navigation_visible = True
        self.leg_navigation_last_activity_at = time.monotonic()
        self.logger = CanonicalLogger(lambda: self.engine.elapsed_ms)
        self.show_boot()
        self.root.after(150, self._advance_timeline)

    def toggle_fullscreen(self) -> None:
        if self.screen == "boot":
            return
        if self.idle_state == "standby":
            self.logger.info(
                "LUNASHELL_power_manager",
                "Synthetic BOOT ignored in standby; use PWR or IMU wake",
            )
            return
        if self.idle_state == "fullscreen":
            if not self._apply_action("press:boot"):
                return
            self.idle_state = "fullscreen" if self._snapshot()["power"]["fullscreen_active"] else "active"
            self.logger.info(
                "LUNASHELL_power_manager",
                "Synthetic BOOT exited Fullscreen; Adaptive Chrome visible",
            )
        elif self.idle_state == "active":
            if not self._apply_action("press:boot"):
                return
            self.idle_state = "fullscreen" if self._snapshot()["power"]["fullscreen_active"] else "active"
            self.logger.info(
                "LUNASHELL_power_manager",
                "Synthetic BOOT entered Fullscreen; Adaptive Chrome locked hidden",
            )
        else:
            previous_state = self.idle_state
            self.idle_state = "active"
            self.logger.info(
                "LUNASHELL_power_manager",
                f"Synthetic BOOT woke {previous_state}; press again for Fullscreen",
            )
        self._redraw_current_screen()

    def toggle_standby(self) -> None:
        if self.screen == "boot":
            return
        power = self._snapshot()["power"]
        was_standby = power["state"] == "standby"
        was_fullscreen = bool(power["fullscreen_active"])
        if not self._apply_action("press:pwr"):
            return
        now_standby = self._snapshot()["power"]["state"] == "standby"
        self.idle_state = "standby" if now_standby else "active"
        if was_standby:
            self.logger.info(
                "LUNASHELL_power_manager",
                "Synthetic PWR wake completed",
            )
        else:
            fullscreen_cancelled = was_fullscreen
            self.logger.info(
                "LUNASHELL_power_manager",
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


# No parameters: run the existing source or bundled simulator through its CLI.
def main() -> None:
    run_cli(LUNASHELLSimulator)


if __name__ == "__main__":
    main()
