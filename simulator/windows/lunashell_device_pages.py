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

"""Home, battery, logs, hardware, and storage views."""

from __future__ import annotations

import tkinter as tk

from lunashell_state import component_current, component_status
from lunashell_style import (
    BLUE,
    FONT_BODY,
    FONT_DISPLAY,
    FONT_TECH,
    MUTED,
    MUTED_BRIGHT,
    STATUS_FAULT,
    STATUS_GLOW,
    STATUS_READY,
    STATUS_WARNING,
    TEXT,
    TRACK,
)


class LunaShellDevicePages:
    def _draw_home_telemetry(self) -> None:
        snapshot = self._snapshot()
        memory = snapshot["memory"]
        processor = snapshot["processor"]

        def ratio(used: int, total: int) -> float:
            return min(1.0, max(0.0, used / total)) if total > 0 else 0.0

        internal_total = int(memory["internal_heap_total_bytes"])
        internal_free = int(memory["internal_free_bytes"])
        internal_used = internal_total - internal_free
        psram_total = int(memory["psram_heap_total_bytes"])
        psram_used = int(memory["psram_used_bytes"])
        app_total = int(memory["app_partition_bytes"])
        app_used = int(memory["firmware_image_bytes"])
        telemetry: list[tuple[str, str, float, str]] = [
            (
                "INTERNAL RAM",
                f"{internal_used // 1024}/{internal_total // 1024}K F{internal_free // 1024}",
                ratio(internal_used, internal_total),
                STATUS_WARNING if memory["internal_memory_pressure"] else STATUS_READY,
            ),
            (
                "PSRAM",
                f"{psram_used // 1024}/{psram_total // 1024}K F{memory['psram_free_bytes'] // 1024}",
                ratio(psram_used, psram_total),
                STATUS_READY,
            ),
            (
                "FLASH APP",
                f"{app_used // 1024}/{app_total // 1024} KB · {round(ratio(app_used, app_total) * 100)}%",
                ratio(app_used, app_total),
                BLUE,
            ),
        ]
        for index, core in enumerate(processor["cores"]):
            available = bool(core["available"] and core["utilization_available"])
            utilization = int(core["utilization_percent"]) if available else 0
            telemetry.append(
                (
                    f"CORE {index}",
                    f"{utilization}%" if available else "UNAVAILABLE",
                    utilization / 100.0 if available else 0.0,
                    STATUS_READY if available else STATUS_WARNING,
                )
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

        wifi = snapshot["radios"]["wifi"]
        gps = snapshot["gps"]
        if component_current(wifi):
            wifi_text = "WIFI  CONNECTED" if wifi["connected"] else "WIFI  READY"
            wifi_color = STATUS_READY
        elif not wifi["started"] and wifi["last_error"] == "ESP_ERR_INVALID_STATE":
            wifi_text = "WIFI  OFF"
            wifi_color = STATUS_WARNING
        else:
            wifi_text, wifi_color = component_status(wifi)
            wifi_text = f"WIFI  {wifi_text}"
        if component_current(gps):
            gps_text = "GPS  3D FIX" if gps["has_fix"] else "GPS  SEARCHING"
            gps_color = STATUS_READY if gps["has_fix"] else STATUS_WARNING
        else:
            gps_text, gps_color = component_status(gps)
            gps_text = f"GPS  {gps_text}"
        self.canvas.create_text(
            116,
            337,
            anchor="w",
            text=wifi_text,
            fill=wifi_color,
            font=(FONT_TECH, 8, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            350,
            337,
            anchor="e",
            text=gps_text,
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
        snapshot = self._snapshot()
        gps = snapshot["gps"]
        wifi = snapshot["radios"]["wifi"]
        touch = snapshot["ui"]["touch"]
        imu = snapshot["imu"]
        gps_log = (
            f"SYN LC76G FIX Q{gps['fix_quality']} SAT {gps['satellites']}"
            if component_current(gps) and gps["has_fix"]
            else "SYN LC76G SEARCHING"
            if component_current(gps)
            else f"SYN LC76G {gps['last_error']}"
        )
        wifi_log = (
            f"SYN WIFI {'CONNECTED' if wifi['connected'] else 'STARTED'}"
            if component_current(wifi)
            else "SYN WIFI OFF"
            if not wifi["started"]
            else f"SYN WIFI {wifi['last_error']}"
        )
        touch_log = (
            "SYN CST9217 READY"
            if component_current(touch)
            else f"SYN CST9217 {component_status(touch)[0]}"
        )
        imu_log = (
            "SYN QMI8658 "
            f"ACC {imu['acceleration_mps2'][0]:+.2f}/"
            f"{imu['acceleration_mps2'][1]:+.2f}/"
            f"{imu['acceleration_mps2'][2]:+.2f}"
            if component_current(imu)
            else f"SYN QMI8658 {component_status(imu)[0]}"
        )
        snapshot_log = (
            f"SYN SNAPSHOT {snapshot['sequence']} {snapshot['reason']}"
            if snapshot["last_update_complete"]
            else f"SYN SNAPSHOT INCOMPLETE {snapshot['last_update_error']}"
        )
        timestamp = f"{self.engine.elapsed_ms:06d}"
        lines = (
            f"{timestamp} LOGGER SYN RING ONLINE",
            f"{timestamp} POWER  {battery_log}",
            f"{timestamp} TOUCH  {touch_log}",
            f"{timestamp} IMU    {imu_log}",
            f"{timestamp} GPS    {gps_log}",
            f"{timestamp} RADIO  {wifi_log}",
            f"{timestamp} APP    {snapshot_log}",
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
        snapshot = self._snapshot()
        battery = snapshot["battery"]
        rtc = snapshot["rtc"]
        imu = snapshot["imu"]
        gps = snapshot["gps"]
        wifi = snapshot["radios"]["wifi"]
        distance = snapshot["distance"]

        def state_for(component: dict, ready_text: str = "READY") -> tuple[str, str]:
            return (ready_text, STATUS_READY) if component_current(component) else component_status(component)

        battery_state = state_for(
            battery,
            "CHARGING"
            if battery["power_state"] == "charging"
            else "LOW"
            if self.scenario.battery_percent is not None and self.scenario.battery_percent <= 12
            else "READY",
        )
        gps_state = state_for(gps, "3D FIX" if gps["has_fix"] else "SEARCHING")
        if component_current(wifi):
            wifi_state = ("CONNECTED" if wifi["connected"] else "STARTED", STATUS_READY)
        elif not wifi["started"] and wifi["last_error"] == "ESP_ERR_INVALID_STATE":
            wifi_state = ("OFF", STATUS_WARNING)
        else:
            wifi_state = component_status(wifi)
        rows = (
            ("BATTERY", "AXP2101", *battery_state),
            ("RTC", "PCF85063", *state_for(rtc)),
            ("IMU", "QMI8658", *state_for(imu)),
            ("GPS", "LC76G", *gps_state),
            ("WIFI", "ESP32-S3", *wifi_state),
            ("DISTANCE", "2 × TOF400F", *state_for(distance, "SYNTHETIC READY")),
        )
        for index, (name, code, state, state_color) in enumerate(rows):
            y = 173 + y_offset + index * 42
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

        storage = self._snapshot()["storage"]
        format_required = self._storage_format_allowed()
        mounted = bool(component_current(storage) and storage["mounted"])
        if format_required:
            status = "FORMAT REQUIRED"
            status_color = STATUS_WARNING
            detail = "Verified card needs explicit FAT formatting."
        elif mounted:
            status = "READY"
            status_color = STATUS_READY
            detail = "FAT filesystem mounted."
        else:
            status, status_color = component_status(storage)
            detail = (
                "Storage state is poisoned; repair is required."
                if storage["poisoned"]
                else f"Mount outcome: {storage['mount_outcome']}."
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
            text=(
                "NOT MOUNTED"
                if not mounted
                else "MOUNTED | LOG MIRROR ON"
                if storage["logger_mirror_enabled"]
                else "MOUNTED | LOG MIRROR OFF"
            ),
            fill="#51c7f2",
            font=(FONT_TECH, 9, "bold"),
            tags="ui",
        )

        total_bytes = int(storage["filesystem_total_bytes"]) if mounted else 0
        free_bytes = int(storage["free_bytes"]) if mounted else 0
        used_bytes = total_bytes - free_bytes
        total = f"{total_bytes // 1_048_576} MB" if mounted else "UNAVAILABLE"
        free = f"{free_bytes // 1_048_576} MB" if mounted else "UNAVAILABLE"
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

        if mounted:
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
                94 + round(278 * (used_bytes / total_bytes)) if total_bytes else 94,
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
                if not mounted
                else f"USED {used_bytes // 1_048_576} MB | {round((used_bytes / total_bytes) * 100) if total_bytes else 0}%"
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
