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

"""Settings, display policy, and distance configuration views."""

from __future__ import annotations

import tkinter as tk

from lunashell_state import component_current, component_status, seconds_label
from lunashell_style import (
    FONT_BODY,
    FONT_DISPLAY,
    FONT_TECH,
    MUTED_BRIGHT,
    SAFE_ORIGIN,
    SAFE_SIZE,
    STATUS_READY,
    STATUS_WARNING,
    TEXT,
    TRACK,
    command_geometry,
)


class LunaShellSettingsPages:
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

    def show_settings_demo(self) -> None:
        self.screen = "settings"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        layout = self._settings_layout()
        snapshot = self._snapshot()
        diagnostics = snapshot["diagnostics"]
        cadence_component = diagnostics["full_snapshot_period"]
        cadence_current = component_current(cadence_component)
        cadence_state, cadence_color = component_status(cadence_component)
        cadence_ms = int(diagnostics["full_snapshot_period_ms"])
        cadence_value = f"{cadence_ms} ms" if cadence_current else cadence_state
        cadence_detail = (
            f"Synthetic updates every {cadence_ms} ms"
            if cadence_current
            else f"Full Snapshot {cadence_state}"
        )
        brightness = snapshot["ui"]["brightness"]
        brightness_current = component_current(brightness)
        brightness_value = (
            f"Configured · {snapshot['ui']['configured_brightness_percent']}%"
            if brightness_current
            else component_status(brightness)[0]
        )
        orientation = snapshot["ui"]["orientation"]
        orientation_current = component_current(orientation)
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
        self._draw_settings_control(
            int(layout["cadence_control"]),
            cadence_value,
        )
        self.canvas.create_text(
            94,
            int(layout["cadence_state"]),
            anchor="w",
            text=cadence_detail,
            fill="#51c7f2" if cadence_current else cadence_color,
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
                brightness_value,
            )
        rotation_y = layout["rotation"]
        if rotation_y is not None:
            self._draw_settings_switch(
                rotation_y,
                (
                    "Auto rotation"
                    if orientation_current
                    else f"Auto rotation · {component_status(orientation)[0]}"
                ),
                "Canonical snapshot" if orientation_current else None,
                bool(
                    orientation_current
                    and orientation["auto_rotation_enabled"]
                ),
            )
        gps_y = int(layout["gps"])
        wireless_running = self._wireless_running()
        wifi = snapshot["radios"]["wifi"]
        ble = snapshot["radios"]["ble"]
        wireless_current = component_current(wifi) or component_current(ble)
        if wireless_running and wireless_current:
            wireless_text = "WIRELESS ACTIVE"
            wireless_color = STATUS_READY
        elif wireless_running:
            wireless_component = wifi if wifi["started"] else ble
            wireless_state, wireless_color = component_status(wireless_component)
            wireless_text = f"WIRELESS {wireless_state}"
        else:
            wireless_text = "WIRELESS OFF"
            wireless_color = STATUS_WARNING
        gps = snapshot["gps"]
        storage = snapshot["storage"]
        self.canvas.create_text(
            94,
            gps_y - 14,
            anchor="w",
            text=wireless_text,
            fill=wireless_color,
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        if self._command_arc_visible():
            gps_role = (
                "success"
                if component_current(gps) and gps["has_fix"]
                else "warning"
                if component_current(gps)
                else "danger"
            )
            sd_role = (
                "warning"
                if self._storage_format_allowed()
                else "success"
                if component_current(storage) and storage["mounted"]
                else "danger"
            )
            commands = (
                ("≡", "primary"),
                ("➤", "success"),
                (
                    "power" if wireless_running else "wifi",
                    "success"
                    if wireless_running and wireless_current
                    else "danger"
                    if wireless_running
                    else "warning",
                ),
                ("gps", gps_role),
                ("sd", sd_role),
            )
            for index, (icon, role) in enumerate(commands):
                x, y, diameter = command_geometry(len(commands), index)
                self._draw_command_button(x, y, icon, role, diameter)
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
        power = self._snapshot()["power"]
        if component_current(power):
            command_timeout = seconds_label(power["command_arc_timeout_seconds"])
            status_timeout = seconds_label(power["status_arc_timeout_seconds"])
        else:
            unavailable = component_status(power)[0]
            command_timeout = unavailable
            status_timeout = unavailable
        self._show_timeout_settings(
            "chrome-settings",
            "ADAPTIVE CHROME",
            "Touch or motion wakes",
            (
                ("Hide Command Arc after", command_timeout),
                ("Hide Status Arc after", status_timeout),
            ),
        )

    def show_power_settings_demo(self) -> None:
        power = self._snapshot()["power"]
        if component_current(power):
            dim_timeout = seconds_label(power["display_dim_timeout_seconds"])
            sleep_delay = seconds_label(power["screen_sleep_delay_seconds"])
        else:
            unavailable = component_status(power)[0]
            dim_timeout = unavailable
            sleep_delay = unavailable
        self._show_timeout_settings(
            "power-settings",
            "POWER",
            "Dim, then screen sleep",
            (
                ("Dim display after", dim_timeout),
                ("Full sleep after display dim", sleep_delay),
            ),
        )

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
        distance = self._snapshot()["distance"]
        calibration = distance["calibration"]
        settings = distance["settings"]
        calibration_state = (
            f"SYNTHETIC CAL {calibration['state'].upper()}"
            if component_current(calibration)
            else f"SYNTHETIC CAL {component_status(calibration)[0]}"
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
            (94, "SAMPLES", str(settings["sample_count"])),
            (240, "INTERVAL", f"{settings['sample_interval_ms']} ms"),
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
            text=f"{settings['reference_distance_mm']:,} mm".replace(",", " "),
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
        slider_x = 104 + round(
            258 * (int(settings["reference_distance_mm"]) - 500) / 1000
        )
        self.canvas.create_line(
            104,
            296 + y_offset,
            slider_x,
            296 + y_offset,
            fill=STATUS_READY,
            width=7,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self._draw_glow_disc(
            slider_x,
            296 + y_offset,
            12,
            STATUS_READY,
            80,
        )
        self.canvas.create_oval(
            slider_x - 9,
            287 + y_offset,
            slider_x + 9,
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
