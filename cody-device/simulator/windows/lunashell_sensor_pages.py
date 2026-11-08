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

"""IMU, leg motion, GPS, and distance views."""

from __future__ import annotations

import math
import time
import tkinter as tk

from PIL import ImageColor

from lunashell_state import component_current, component_status
from lunashell_style import (
    FONT_DISPLAY,
    FONT_TECH,
    LEG_FADE_IN_SECONDS,
    LEG_FADE_OUT_SECONDS,
    LEG_PULSE_SECONDS,
    MUTED,
    MUTED_BRIGHT,
    STATUS_READY,
    STATUS_WARNING,
    TEXT,
    command_geometry,
)


def blend_hex_color(base: str, accent: str, opacity: float) -> str:
    """Blend a Canvas color toward its active state for short UI fades."""
    amount = max(0.0, min(1.0, opacity))
    base_rgb = ImageColor.getrgb(base)
    accent_rgb = ImageColor.getrgb(accent)
    channels = (round(start + (end - start) * amount)
                for start, end in zip(base_rgb, accent_rgb))
    return "#" + "".join(f"{channel:02x}" for channel in channels)


class LunaShellSensorPages:
    def show_imu_demo(self) -> None:
        self.screen = "imu"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        snapshot = self._snapshot()
        imu = snapshot["imu"]
        fusion = snapshot["sensor_fusion"]
        imu_current = component_current(imu)
        imu_state, imu_color = component_status(imu)
        if imu_current:
            acceleration = [float(value) for value in imu["acceleration_mps2"]]
            angular_velocity = [float(value) for value in imu["angular_velocity_dps"]]
            roll = math.degrees(math.atan2(acceleration[1], acceleration[2]))
            pitch = math.degrees(
                math.atan2(
                    -acceleration[0],
                    math.hypot(acceleration[1], acceleration[2]),
                )
            )
            roll_text = f"{roll:+.1f}°"
            pitch_text = f"{pitch:+.1f}°"
        else:
            acceleration = [0.0, 0.0, 0.0]
            angular_velocity = [0.0, 0.0, 0.0]
            roll = 0.0
            roll_text = "UNAVAILABLE"
            pitch_text = "UNAVAILABLE"
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "IMU LEVEL",
            "QMI8658 diagnostics"
            if self.performance_mode
            else (
                "QMI8658 · canonical synthetic sample"
                if imu_current
                else f"QMI8658 · {imu_state}"
            ),
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
            text=roll_text,
            fill="#61d8ff" if imu_current else imu_color,
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
            text=pitch_text,
            fill="#ff9d5c" if imu_current else imu_color,
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
        if imu_current:
            line_delta = round(max(-30.0, min(30.0, roll)) / 30.0 * 15)
            self.canvas.create_line(
                center_x - 46,
                center_y + line_delta,
                center_x + 46,
                center_y - line_delta,
                fill=STATUS_READY,
                width=2,
                capstyle=tk.ROUND,
                tags="ui",
            )
            dot_x = center_x + round(max(-1.0, min(1.0, acceleration[0] / 9.81)) * 38)
            dot_y = center_y + round(max(-1.0, min(1.0, acceleration[1] / 9.81)) * 38)
            self._draw_glow_disc(dot_x, dot_y, 15, "#5be4ff", 100)
            self.canvas.create_oval(
                dot_x - 14,
                dot_y - 14,
                dot_x + 14,
                dot_y + 14,
                fill="#5be4ff",
                outline="#d8faff",
                width=1,
                tags="ui",
            )
        else:
            self.canvas.create_text(
                center_x,
                center_y,
                text=imu_state,
                fill=imu_color,
                font=(FONT_TECH, 9, "bold"),
                tags="ui",
            )
        self.canvas.create_text(
            72,
            319,
            text=(
                "SYNTHETIC ACC   "
                f"X {acceleration[0]:+.2f}   Y {acceleration[1]:+.2f}   "
                f"Z {acceleration[2]:+.2f} m/s²"
                if imu_current
                else f"SYNTHETIC ACC   {imu_state}"
            ),
            anchor="w",
            fill="#61d8ff" if imu_current else imu_color,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            72,
            337,
            text=(
                "SYNTHETIC GYRO  "
                f"X {angular_velocity[0]:+.2f}   Y {angular_velocity[1]:+.2f}   "
                f"Z {angular_velocity[2]:+.2f} dps"
                if imu_current
                else f"SYNTHETIC GYRO  {imu_state}"
            ),
            anchor="w",
            fill="#ff9d5c" if imu_current else imu_color,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            92,
            355,
            anchor="w",
            text=(
                "SYNTHETIC CAL READY"
                if imu_current
                and component_current(imu["calibration"])
                and imu["calibrated"]
                else f"SYNTHETIC CAL {component_status(imu['calibration'])[0]}"
            ),
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            374,
            355,
            anchor="e",
            text=(
                ("STATIONARY" if fusion["stationary"] else "MOTION")
                if component_current(fusion)
                else f"FUSION {component_status(fusion)[0]}"
            ),
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

    def show_leg_demo(self, screen: str | None = None) -> None:
        if screen is not None:
            self.screen = screen
        self.leg_navigation_visible = True
        self.leg_navigation_last_activity_at = time.monotonic()
        pages = ("leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings")
        page = pages.index(self.screen)
        self._clear()
        self._draw_atmosphere()
        if self._status_arc_visible():
            for kind, x, color in (("usb", 153, "#35d8f7"),
                                   ("battery", 233, "#f3f7fa"),
                                   ("sd", 313, "#35d8f7")):
                self._draw_status_icon(kind, x, 43, color)
                self.canvas.create_oval(x - 3, 69, x + 3, 75,
                                        fill=color, outline="", tags="ui")
        imu = self._snapshot()["imu"]
        current = component_current(imu)
        acceleration = [float(value) for value in imu["acceleration_mps2"]] if current else None
        angular_velocity = [float(value) for value in imu["angular_velocity_dps"]] if current else None
        titles = ("ACCELEROMETRO", "GIROSCOPIO", "ACCEL + GIRO", "MOVIMENTO", "IMPOSTAZIONI")
        offset = self._fixed_canvas_offset_y()
        first_item = self._next_canvas_item_id()
        self.canvas.create_text(233, 107, text=titles[page], fill=TEXT,
                                font=(FONT_DISPLAY, 25, "bold"), tags="ui")
        if page != 4:
            self.canvas.create_text(233, 143,
                                    text="FERMO" if current else "IMU NON DISPONIBILE",
                                    fill="#57caff" if current else MUTED,
                                    font=(FONT_DISPLAY, 9), tags=("ui", "leg-health"))
        if page in (0, 1):
            self.canvas.create_oval(94, 182, 216, 304, outline="#1d4776",
                                    width=1, tags="ui")
            if page == 0:
                for index, (dx, dy, color, x, y, label) in enumerate((
                    (-0.72, 0.68, "#23d7f7", 108, 284, "X"),
                    (0.86, 0.50, "#8af055", 200, 276, "Y"),
                    (0.0, -1.0, "#fcbf48", 155, 187, "Z"),
                )):
                    self.canvas.create_line(155, 243, 155 + dx * 48, 243 + dy * 48,
                                            fill="#1b3e69", width=2, tags="ui")
                    self.canvas.create_line(155, 243, 155, 243, fill=color, width=5,
                                            capstyle=tk.ROUND, tags=("ui", f"leg-accel-spoke-{index}"))
                    self.canvas.create_text(x, y, text=label, fill=color,
                                            font=(FONT_TECH, 10, "bold"), tags="ui")
                self.canvas.create_oval(150, 238, 160, 248, fill="#d4ebff", outline="", tags="ui")
            else:
                self.canvas.create_oval(111, 199, 199, 287, outline="#263b72",
                                        width=3, tags="ui")
                self.canvas.create_arc(111, 199, 199, 287, start=90, extent=0,
                                       style=tk.ARC, outline="#7863ed", width=5,
                                       tags=("ui", "leg-gyro-arc"))
                self.canvas.create_oval(130, 218, 180, 268, outline="#1b527b",
                                        width=2, tags="ui")
                self.canvas.create_oval(150, 238, 160, 248, fill="#d4ebff",
                                        outline="", tags=("ui", "leg-gyro-dot"))
            self.canvas.create_line(226, 175, 226, 309, fill="#1a4474",
                                    width=1, tags="ui")
            source = acceleration if page == 0 else angular_velocity
            colors = ("#35d8f7", "#8af055", "#fcbf48")
            for index, axis in enumerate("XYZ"):
                value = f"{source[index]:+.2f}" if source is not None else "--"
                self.canvas.create_text(240, 190 + index * 47, anchor="w",
                                        text=axis, fill=colors[index],
                                        font=(FONT_TECH, 18, "bold"), tags="ui")
                self.canvas.create_text(271, 190 + index * 47, anchor="w",
                                        text=value, fill=TEXT,
                                        font=(FONT_TECH, 17, "bold"),
                                        tags=("ui", f"leg-single-value-{index}"))
            self.canvas.create_text(301, 315, text="m/s2" if page == 0 else "deg/s",
                                    fill="#a9bce7", font=(FONT_TECH, 9), tags="ui")
        elif page == 2:
            self.canvas.create_line(233, 178, 233, 310, fill="#1a4474",
                                    width=1, tags="ui")
            for x, heading, unit, values, tag in (
                (99, "ACCEL", "m/s2", acceleration, "accel"),
                (248, "GIRO", "deg/s", angular_velocity, "gyro"),
            ):
                self.canvas.create_text(x, 186, anchor="w", text=heading,
                                        fill="#35d8f7" if heading == "ACCEL" else "#b18bff",
                                        font=(FONT_DISPLAY, 15, "bold"), tags="ui")
                self.canvas.create_text(x, 208, anchor="w", text=unit,
                                        fill="#a9bce7", font=(FONT_TECH, 9), tags="ui")
                for index, axis in enumerate("XYZ"):
                    value = f"{values[index]:+.2f}" if values is not None else "--"
                    self.canvas.create_text(x, 242 + index * 29, anchor="w",
                                            text=axis, fill=TEXT,
                                            font=(FONT_TECH, 15, "bold"), tags="ui")
                    value_x = 222 if tag == "accel" else 387
                    self.canvas.create_text(value_x, 242 + index * 29, anchor="e",
                                            text=value, fill=TEXT,
                                            font=(FONT_TECH, 15, "bold"),
                                            tags=("ui", f"leg-{tag}-value-{index}"))
        elif page == 3:
            self.canvas.create_text(81, 168, anchor="w", text="INTENSITA",
                                    fill="#9bb3db", font=(FONT_DISPLAY, 9), tags="ui")
            self.canvas.create_text(387, 169, anchor="e",
                                    text="SYNTHETIC · CAMPIONI | NON MISURA IL CARICO",
                                    fill="#9bb3db", font=(FONT_TECH, 7),
                                    tags=("ui", "leg-motion-note"))
            self.canvas.create_text(77, 223, anchor="w", text="FORTE", fill="#ff6b67",
                                    font=(FONT_DISPLAY, 8), tags="ui")
            self.canvas.create_text(77, 272, anchor="w", text="ELEVATO", fill="#efb84b",
                                    font=(FONT_DISPLAY, 8), tags="ui")
            self.canvas.create_text(77, 303, anchor="w", text="FERMO", fill="#57d8ef",
                                    font=(FONT_DISPLAY, 8), tags="ui")
            for x in (211, 299):
                self.canvas.create_line(x, 190, x, 304, fill="#20375a", width=1,
                                        tags="ui")
            self.canvas.create_line(123, 190, 123, 304, fill="#2f5687", width=1,
                                    tags="ui")
            self.canvas.create_line(123, 304, 387, 304, fill="#2f5687", width=1,
                                    tags="ui")
            self.canvas.create_line(123, 223, 387, 223, fill="#a93c5c", width=1,
                                    tags="ui")
            self.canvas.create_line(123, 272, 387, 272, fill="#b98635", width=1,
                                    tags="ui")
            self.canvas.create_line(123, 304, 387, 304, fill="#35d8f7", width=3,
                                    smooth=True, tags=("ui", "leg-motion-trace"))
            self.canvas.create_line(89, 341, 89, 330,
                                    fill="#855024", width=3,
                                    tags=("ui", "leg-motion-acc-color"))
            self.canvas.create_polygon(82, 331, 89, 322, 96, 331,
                                       fill="#855024", outline="",
                                       tags=("ui", "leg-motion-acc-color"))
            self.canvas.create_line(81, 342, 97, 342, fill="#855024", width=2,
                                    tags=("ui", "leg-motion-acc-color"))
            self.canvas.create_text(120, 332, text="ACC", fill="#855024",
                                    font=(FONT_TECH, 10, "bold"),
                                    tags=("ui", "leg-motion-acc-color"))
            self.canvas.create_arc(327, 323, 348, 344, start=35, extent=290,
                                   style=tk.ARC, outline="#855024", width=2,
                                   tags=("ui", "leg-motion-rot-arc"))
            self.canvas.create_line(343, 325, 348, 325, 347, 330,
                                    fill="#855024", width=2,
                                    tags=("ui", "leg-motion-rot-color"))
            self.canvas.create_text(369, 332, text="ROT", fill="#855024",
                                    font=(FONT_TECH, 10, "bold"),
                                    tags=("ui", "leg-motion-rot-color"))
            for bounds in ((147, 319, 174, 346), (292, 319, 319, 346)):
                self.canvas.create_oval(*bounds, fill="#091c31", outline="",
                                        state=tk.HIDDEN,
                                        tags=("ui", "leg-motion-alert",
                                              "leg-motion-alert-fill"))
            self.canvas.create_rectangle(160, 319, 306, 346,
                                         fill="#091c31", outline="",
                                         state=tk.HIDDEN,
                                         tags=("ui", "leg-motion-alert",
                                               "leg-motion-alert-fill"))
            self.canvas.create_text(233, 332, text="ATTENZIONE", fill="#11151c",
                                    font=(FONT_DISPLAY, 16, "bold"), state=tk.HIDDEN,
                                    tags=("ui", "leg-motion-alert"))
        else:
            labels = ("Rotazione automatica", "Flip 180 gradi", "Suoni", "Risveglio movimento")
            for index, label in enumerate(labels):
                y = 180 + index * 46
                self.canvas.create_text(78, y, anchor="w", text=label, fill=TEXT,
                                        font=(FONT_DISPLAY, 15), tags="ui")
                self.canvas.create_rectangle(332, y - 14, 384, y + 16,
                                             fill="#00c9f4" if index != 1 else "#263744",
                                             outline="#256ab7", width=1, tags="ui")
                knob_x = 369 if index != 1 else 347
                self.canvas.create_oval(knob_x - 11, y - 11, knob_x + 11, y + 11,
                                        fill=TEXT, outline="", tags="ui")
                if index < 3:
                    self.canvas.create_line(78, y + 23, 384, y + 23,
                                            fill="#174274", width=1, tags="ui")
        if page < 4 and page != 3:
            self.canvas.create_text(233, 341,
                                    text="SYNTHETIC · DISPOSITIVO ORIZZONTALE" if current
                                    else "SYNTHETIC · ORIENTAMENTO NON DISPONIBILE",
                                    fill="#57caff", font=(FONT_DISPLAY, 8),
                                    tags=("ui", "leg-orientation"))
        elif page == 4:
            self.canvas.create_text(233, 143, text="SYNTHETIC · IMPOSTAZIONI DEMO",
                                    fill="#57caff", font=(FONT_DISPLAY, 8), tags="ui")
        for x, direction, tag in ((139, -1, "leg-previous"),
                                  (327, 1, "leg-next")):
            self.canvas.create_oval(x - 20.5, 356.5, x + 20.5, 397.5,
                                    fill="#278f80", outline="#52ac9f",
                                    width=1, tags=("ui", tag))
            self.canvas.create_line(x - direction * 4, 369,
                                    x + direction * 4, 377,
                                    x - direction * 4, 385,
                                    fill="#f0fbf8", width=4,
                                    capstyle=tk.ROUND, joinstyle=tk.ROUND,
                                    tags=("ui", tag))
        if page == 3:
            self.canvas.create_text(233, 376, text="PICCO REL. --", fill=TEXT,
                                    font=(FONT_TECH, 10, "bold"),
                                    tags=("ui", "leg-motion-peak"))
        self._move_canvas_items_from(first_item, offset)
        self._draw_edge_back()
        self._draw_safe_guide()
        self._refresh_leg_visuals()

    def _set_leg_navigation_visible(self, visible: bool) -> None:
        if self.leg_navigation_visible == visible:
            return
        self.leg_navigation_visible = visible
        state = tk.NORMAL if visible else tk.HIDDEN
        self.canvas.itemconfigure("leg-previous", state=state)
        self.canvas.itemconfigure("leg-next", state=state)

    def _reveal_leg_navigation(self) -> None:
        self.leg_navigation_last_activity_at = time.monotonic()
        self._set_leg_navigation_visible(True)

    def _refresh_leg_navigation(self) -> None:
        snapshot = self._snapshot()
        imu = snapshot["imu"]
        fusion = snapshot["sensor_fusion"]
        stationary = (
            component_current(imu)
            and component_current(fusion)
            and fusion["stationary"] is True
        )
        if not stationary:
            self._reveal_leg_navigation()
        elif time.monotonic() - self.leg_navigation_last_activity_at >= 5.0:
            self._set_leg_navigation_visible(False)

    def _refresh_leg_visuals(self) -> None:
        imu = self._snapshot()["imu"]
        now = time.monotonic()
        animation_step = min(max(now - self.leg_animation_last_at, 0.0), 0.5)
        self.leg_animation_last_at = now
        current = component_current(imu)
        if current:
            acceleration = tuple(float(value) for value in imu["acceleration_mps2"])
            angular_velocity = tuple(float(value) for value in imu["angular_velocity_dps"])
            current = all(math.isfinite(value) for value in (*acceleration, *angular_velocity))
        if current:
            sample_at_us = int(imu["last_success_at_us"])
            if sample_at_us != self.leg_previous_imu_sample_at_us:
                previous = self.leg_previous_imu_values
                if previous is None:
                    self.leg_current_intensity = 0.0
                else:
                    accel_step = max(abs(a - b) for a, b in zip(acceleration, previous[0]))
                    gyro_step = max(abs(a - b) for a, b in zip(angular_velocity, previous[1]))
                    self.leg_current_intensity = max(accel_step, gyro_step * 0.04)
                    if accel_step >= 0.25:
                        cycle_start = self.leg_indicator_cycle_started_at["acc"]
                        if cycle_start is None or now - cycle_start >= LEG_PULSE_SECONDS:
                            self.leg_indicator_cycle_started_at["acc"] = now
                        self.leg_acc_pulse_at = now
                    if gyro_step * 0.04 >= 0.25:
                        cycle_start = self.leg_indicator_cycle_started_at["rot"]
                        if cycle_start is None or now - cycle_start >= LEG_PULSE_SECONDS:
                            self.leg_indicator_cycle_started_at["rot"] = now
                        self.leg_rot_pulse_at = now
                self.leg_movement_history.append(self.leg_current_intensity)
                self.leg_previous_imu_values = (acceleration, angular_velocity)
                self.leg_previous_imu_sample_at_us = sample_at_us
                if self.leg_current_intensity >= 0.25:
                    self._reveal_leg_navigation()
            for index in range(3):
                self.leg_display_acceleration[index] += (
                    acceleration[index] - self.leg_display_acceleration[index]
                ) * 0.34
                self.leg_display_angular_velocity[index] += (
                    angular_velocity[index] - self.leg_display_angular_velocity[index]
                ) * 0.34
            intensity = self.leg_current_intensity
            if intensity >= 3.0:
                health, health_color = "SCOSSONE - NON CARICARE", "#ffad45"
            elif intensity >= 1.2:
                health, health_color = "MOVIMENTO ELEVATO", "#f0c251"
            elif intensity >= 0.25:
                health, health_color = "MOVIMENTO LIEVE", "#57d8ef"
            else:
                health, health_color = "FERMO", "#57caff"
            norm = math.sqrt(sum(value * value for value in acceleration))
            if norm <= 1.0:
                orientation = "ORIENTAMENTO NON DISPONIBILE"
            else:
                vertical_ratio = abs(acceleration[2]) / norm
                orientation = (
                    "DISPOSITIVO VERTICALE" if vertical_ratio < 0.35
                    else "DISPOSITIVO ORIZZONTALE" if vertical_ratio > 0.80
                    else "DISPOSITIVO INCLINATO"
                )
        else:
            acceleration = angular_velocity = None
            self.leg_display_acceleration[:] = [0.0, 0.0, 0.0]
            self.leg_display_angular_velocity[:] = [0.0, 0.0, 0.0]
            self.leg_previous_imu_values = None
            self.leg_previous_imu_sample_at_us = 0
            self.leg_movement_history.clear()
            self.leg_current_intensity = 0.0
            self.leg_acc_pulse_at = None
            self.leg_rot_pulse_at = None
            self.leg_indicator_cycle_started_at = {"acc": None, "rot": None}
            self.leg_indicator_levels = {"acc": 0.0, "rot": 0.0}
            self.leg_alert_level = 0.0
            health, health_color = "IMU NON DISPONIBILE", MUTED
            orientation = "ORIENTAMENTO NON DISPONIBILE"

        self.canvas.itemconfigure("leg-health", text=health, fill=health_color)
        self.canvas.itemconfigure("leg-orientation", text=f"SYNTHETIC · {orientation}")
        for index in range(3):
            single = angular_velocity if self.screen == "leg-gyro" else acceleration
            value = f"{single[index]:+.2f}" if single is not None else "--"
            self.canvas.itemconfigure(f"leg-single-value-{index}", text=value)
            accel_value = f"{acceleration[index]:+.2f}" if acceleration is not None else "--"
            gyro_value = f"{angular_velocity[index]:+.2f}" if angular_velocity is not None else "--"
            self.canvas.itemconfigure(f"leg-accel-value-{index}",
                                      text=accel_value)
            self.canvas.itemconfigure(f"leg-gyro-value-{index}",
                                      text=gyro_value)

        state = tk.NORMAL if current else tk.HIDDEN
        offset = self._fixed_canvas_offset_y()
        for index, (dx, dy) in enumerate(((-0.72, 0.68), (0.86, 0.50), (0.0, -1.0))):
            value = self.leg_display_acceleration[index]
            distance = 8 + min(abs(value) / 9.81, 1.0) * 43
            direction = -1 if value < 0 else 1
            self.canvas.coords(f"leg-accel-spoke-{index}",
                               155, 243 + offset, 155 + dx * distance * direction,
                               243 + offset + dy * distance * direction)
            self.canvas.itemconfigure(f"leg-accel-spoke-{index}", state=state)

        gx, gy, gz = self.leg_display_angular_velocity
        angular_speed = math.sqrt(gx * gx + gy * gy + gz * gz)
        radius = 44 + min(angular_speed / 30.0, 1.0) * 7
        self.canvas.coords("leg-gyro-arc", 155 - radius, 243 + offset - radius,
                           155 + radius, 243 + offset + radius)
        self.canvas.itemconfigure(
            "leg-gyro-arc", start=90 - math.degrees(math.atan2(gy, gx)),
            extent=(1 if gz >= 0 else -1) * min(300, 16 + angular_speed * 5),
            outline="#7863ed" if gz >= 0 else "#35d8f7", state=state,
        )
        dot_x = 155 + max(-24.0, min(24.0, gx * 1.2))
        dot_y = 243 + offset - max(-24.0, min(24.0, gy * 1.2))
        self.canvas.coords("leg-gyro-dot", dot_x - 5, dot_y - 5,
                           dot_x + 5, dot_y + 5)
        self.canvas.itemconfigure("leg-gyro-dot", state=state)

        if self.screen == "leg-motion":
            for signal, pulse_at in (("acc", self.leg_acc_pulse_at),
                                     ("rot", self.leg_rot_pulse_at)):
                cycle_start = self.leg_indicator_cycle_started_at[signal]
                age = now - cycle_start if cycle_start is not None else LEG_PULSE_SECONDS
                if (current and pulse_at is not None and cycle_start is not None
                        and pulse_at >= cycle_start and 0 <= age < LEG_PULSE_SECONDS):
                    # Each sensor event starts its own 250 ms rise and 1.5 s
                    # fall. Repeated events start a new pulse after this one.
                    level = (age / LEG_FADE_IN_SECONDS if age < LEG_FADE_IN_SECONDS
                             else (LEG_PULSE_SECONDS - age) / LEG_FADE_OUT_SECONDS)
                else:
                    level = 0.0
                    self.leg_indicator_cycle_started_at[signal] = None
                self.leg_indicator_levels[signal] = level
                color = ("#677383" if not current else
                         blend_hex_color("#855024", "#ffad45", level))
                self.canvas.itemconfigure(f"leg-motion-{signal}-color", fill=color)
                if signal == "rot":
                    self.canvas.itemconfigure("leg-motion-rot-arc", outline=color)
            if current and len(self.leg_movement_history) >= 2:
                history = list(self.leg_movement_history)
                self.canvas.itemconfigure("leg-motion-peak",
                                          text=f"PICCO REL. {max(history):.1f}")
                points: list[float] = []
                for index, value in enumerate(history):
                    y = 304 - min(value / 4.0, 1.0) * 108
                    points.extend((123 + index * 264 / (len(history) - 1), y + offset))
                self.canvas.coords("leg-motion-trace", *points)
                color = ("#ff6b4a" if self.leg_current_intensity >= 3.0
                         else "#f0c251" if self.leg_current_intensity >= 1.2
                         else "#35d8f7")
                self.canvas.itemconfigure("leg-motion-trace", fill=color,
                                          state=tk.NORMAL)
            else:
                self.canvas.itemconfigure("leg-motion-trace", state=tk.HIDDEN)
                self.canvas.itemconfigure("leg-motion-peak", text="PICCO REL. --")
            alert_target = 1.0 if current and self.leg_current_intensity >= 1.2 else 0.0
            if alert_target:
                self.leg_alert_color = ("#ff9b9b" if self.leg_current_intensity >= 3.0
                                        else "#ffd291")
            if alert_target > self.leg_alert_level:
                self.leg_alert_level = min(alert_target,
                                           self.leg_alert_level
                                           + animation_step / LEG_FADE_IN_SECONDS)
            else:
                self.leg_alert_level = max(alert_target,
                                           self.leg_alert_level
                                           - animation_step / LEG_FADE_OUT_SECONDS)
            if current and self.leg_alert_level > 0.0:
                self.canvas.itemconfigure(
                    "leg-motion-alert-fill",
                    fill=blend_hex_color("#091c31", self.leg_alert_color,
                                         self.leg_alert_level),
                )
                self.canvas.itemconfigure("leg-motion-alert", state=tk.NORMAL)
            else:
                self.canvas.itemconfigure("leg-motion-alert", state=tk.HIDDEN)
            self.canvas.itemconfigure(
                "leg-motion-note",
                text=("SYNTHETIC · CAMPIONI | NON MISURA IL CARICO" if current
                      else "SYNTHETIC · IMU NON DISPONIBILE"),
            )

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

        gps = self._snapshot()["gps"]
        current = component_current(gps)
        has_fix = bool(current and gps["has_fix"])
        searching = bool(current and not has_fix)
        if has_fix:
            receiver_state = "LC76G READY | RECEIVER ONLINE"
            fix_state = "FIX ACTIVE"
            accent = STATUS_READY
        elif searching:
            receiver_state = "LC76G READY | RECEIVER ONLINE"
            fix_state = "NO FIX"
            accent = STATUS_WARNING
        else:
            component_state, accent = component_status(gps)
            receiver_state = f"LC76G {component_state}"
            fix_state = "STALE" if gps["data_stale"] else "UNAVAILABLE"
        self.canvas.create_text(
            84,
            166,
            anchor="w",
            text=receiver_state,
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
        if has_fix:
            latitude = float(gps["latitude_degrees"])
            longitude = float(gps["longitude_degrees"])
            position = (
                f"{abs(latitude):.6f} {'N' if latitude >= 0 else 'S'}",
                f"{abs(longitude):.6f} {'E' if longitude >= 0 else 'W'}",
            )
        else:
            unavailable = "NO FIX" if searching else fix_state
            position = (unavailable, unavailable)
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
        if has_fix:
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
                "SYNTHETIC | RECEIVER READY | FIX ACTIVE"
                if has_fix
                else "SYNTHETIC | RECEIVER READY | NO FIX"
                if searching
                else f"SYNTHETIC | {fix_state} | {gps['last_error']}"
            ),
            fill=accent,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        unavailable = "NO FIX" if searching else fix_state
        metrics = (
            (315, "SATELLITES", str(gps["satellites"]) if current else unavailable, accent),
            (333, "HDOP", f"{gps['hdop']:.1f}" if has_fix else unavailable, "#51c7f2"),
            (351, "ALTITUDE", f"{gps['altitude_m']:.1f} m" if has_fix else unavailable, STATUS_WARNING),
            (369, "SPEED", f"{gps['speed_mps']:.2f} m/s" if has_fix else unavailable, "#c168ff"),
            (387, "COURSE", f"{gps['course_degrees']:.1f} deg" if has_fix else unavailable, TEXT),
            (405, "GNSS TIME", f"{gps['utc_time']} UTC" if has_fix and gps["utc_valid"] else unavailable, TEXT),
            (
                423,
                "SAMPLE",
                f"QUALITY {gps['fix_quality']} | QUEUE {gps['pending_nmea_bytes']} B"
                if has_fix
                else "QUALITY 0 | SEARCHING"
                if searching
                else f"{fix_state} | {gps['last_error']}",
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
        distance = self._snapshot()["distance"]
        current = component_current(distance)
        state_text, accent = (
            ("LIVE SYNTHETIC SAMPLE", STATUS_READY)
            if current
            else component_status(distance)
        )
        self.canvas.create_text(
            233,
            172,
            text=state_text,
            fill=accent,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            201,
            text=f"{distance['vertical_distance_mm']:,} mm".replace(",", " ") if current else "UNAVAILABLE",
            fill=accent,
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
        sensor_values: list[tuple[str, str]] = []
        for sensor in distance["sensors"]:
            if component_current(sensor):
                sensor_values.append((f"{sensor['filtered_distance_mm']:,} mm".replace(",", " "), STATUS_READY))
            else:
                sensor_values.append(component_status(sensor))
        self._draw_tof_sensor_panel(94, "TOF 1", sensor_values[0][0], sensor_values[0][1])
        self._draw_tof_sensor_panel(240, "TOF 2", sensor_values[1][0], sensor_values[1][1])
        self.canvas.create_text(
            233,
            310,
            text=(
                f"SYNTHETIC · Δ {distance['sensor_spread_mm']} mm · SLANT {distance['slant_distance_mm']} mm"
                if current
                else "SYNTHETIC · RESULT UNAVAILABLE"
            ),
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 7, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            330,
            text=(
                f"TILT {distance['tilt_degrees']:+.1f}° · {'COMPENSATED' if distance['tilt_compensated'] else 'RAW'}"
                if current
                else f"{distance['last_error']}"
            ),
            fill=STATUS_WARNING,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            349,
            text=(
                f"CONF {distance['confidence_percent']}% · CAL {distance['calibration']['state'].upper()}"
                if current
                else "CONF UNAVAILABLE"
            ),
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
