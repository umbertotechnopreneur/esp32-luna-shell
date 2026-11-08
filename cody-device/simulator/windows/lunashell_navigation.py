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

"""Touch dispatch, confirmation, and screen navigation."""

from __future__ import annotations

import tkinter as tk

from lunashell_style import PAIRING_SCREENS, command_geometry


class LunaShellNavigation:
    # self: Synthetic navigation owner.
    # event: Host mouse event, never a physical device observation.
    # Raises tk.TclError or the original rejected synthetic action error.
    def _on_click(self, event: tk.Event) -> None:
        if self.screen == "boot":
            self.logger.info("LUNASHELL_boot", "Synthetic touch received; full-cycle startup handoff owns this screen")
            return

        if self.idle_state == "standby":
            self.logger.info(
                "LUNASHELL_power_manager",
                "Synthetic touch ignored in standby; use PWR or IMU wake",
            )
            return
        self._apply_action("touch")
        leg_arrows_were_hidden = (
            self.screen in {"leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings"}
            and not self.leg_navigation_visible
        )
        if self.screen in {"leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings"}:
            self._reveal_leg_navigation()
        if self.idle_state not in {"active", "fullscreen"}:
            self.idle_state = "active"
            self.logger.info("LUNASHELL_ui", "Synthetic touch restored Adaptive Chrome")
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
                "leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings",
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
        elif self.screen in {"leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings"}:
            if leg_arrows_were_hidden:
                return
            pages = ("leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings")
            nav_top = 352.5 + self._fixed_canvas_offset_y()
            if 112 <= event.x <= 166 and nav_top <= event.y <= nav_top + 49:
                self.show_leg_demo(pages[(pages.index(self.screen) - 1) % 5])
            elif 300 <= event.x <= 354 and nav_top <= event.y <= nav_top + 49:
                self.show_leg_demo(pages[(pages.index(self.screen) + 1) % 5])
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
                    "LUNASHELL_nav",
                    "Synthetic AXP2101 hardware row selected",
                )
                self.show_battery_demo(return_screen="hardware")
            elif (
                88 <= event.x <= 378
                and tof_row_y - 12 <= event.y <= tof_row_y + 30
            ):
                self.logger.info(
                    "LUNASHELL_nav",
                    "Synthetic TOF hardware row selected",
                )
                self.show_tof_demo()
        elif self.screen == "tof" and command_arc_visible:
            x, y, diameter = command_geometry(1, 0)
            if x <= event.x <= x + diameter and y <= event.y <= y + diameter:
                self.logger.info(
                    "LUNASHELL_nav",
                    f"Synthetic {self.screen.upper()} settings gear selected",
                )
                self.show_tof_settings_demo(return_screen=self.screen)
        elif self.screen == "tof-settings" and command_arc_visible:
            x, y, diameter = command_geometry(1, 0)
            if x <= event.x <= x + diameter and y <= event.y <= y + diameter:
                self._apply_action("calibrate:tof")
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
                    "LUNASHELL_performance",
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
                            self._apply_action("toggle:wireless")
                            self.show_settings_demo()
                        elif index == 3:
                            self.logger.info(
                                "LUNASHELL_nav",
                                "Synthetic Settings GPS command selected",
                            )
                            self.show_gps_demo()
                        else:
                            self.logger.info(
                                "LUNASHELL_nav",
                                "Synthetic Settings SD command selected",
                            )
                            self.show_storage_demo()
                        return
        elif (
            self.screen == "storage"
            and 128 <= event.x <= 338
            and 358 <= event.y <= 402
        ):
            if self._storage_format_allowed():
                self.logger.info(
                    "LUNASHELL_storage",
                    "Synthetic explicit format confirmation requested",
                )
                self.show_confirmation("confirm:format-sd")
            else:
                self.logger.info(
                    "LUNASHELL_storage",
                    "Synthetic format rejected: no verified format-required state",
                )

    def show_confirmation(self, action: str | None = None) -> None:
        if self.screen == "boot":
            return
        if self.screen != "confirmation":
            self.confirmation_return_screen = self.screen
        if action is not None:
            self.pending_action = action
        self.screen = "confirmation"
        self.logger.info("LUNASHELL_nav", "Synthetic confirmation screen presented")
        self._draw_shell(
            "Confirm action?",
            (
                "Format the verified synthetic SD card once."
                if self.pending_action == "confirm:format-sd"
                else "The side margins are reserved for deterministic Cancel and OK actions."
            ),
            primary=False,
            actions=True,
        )

    def confirm(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("LUNASHELL_nav", "Synthetic confirmation accepted")
        action = self.pending_action
        self.pending_action = None
        if action is not None:
            accepted = self._apply_action(action)
            message = (
                "Synthetic SD formatted and mounted"
                if accepted and action == "confirm:format-sd"
                else "Synthetic action rejected"
            )
        else:
            message = "Confirmed"
        self.result_message = message
        self.screen = "result"
        self._draw_shell("ESP32 Luna Shell", message, primary=True, actions=False)

    def cancel(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("LUNASHELL_nav", "Synthetic confirmation cancelled")
        self.pending_action = None
        if self.confirmation_return_screen == "storage":
            self.show_storage_demo()
        else:
            self.show_home()

    def back(self) -> None:
        if self.screen == "battery":
            self.logger.info(
                "LUNASHELL_nav",
                f"Synthetic Battery closed; returning to {self.battery_return_screen}",
            )
            if self.battery_return_screen == "hardware":
                self.show_hardware_demo()
            else:
                self.show_home()
            return
        if self.screen == "logs":
            self.logger.info(
                "LUNASHELL_nav",
                "Synthetic Logs closed; returning to Settings",
            )
            self.show_settings_demo()
            return
        if self.screen == "gps":
            self.logger.info(
                "LUNASHELL_nav",
                "Synthetic GPS closed; returning to Settings",
            )
            self.show_settings_demo()
            return
        if self.screen == "tof-settings":
            self.logger.info(
                "LUNASHELL_nav",
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
            self.logger.info("LUNASHELL_pairing", "Synthetic pairing Back")
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
            self.logger.info("LUNASHELL_ui", "Diagnostic page closed")
            if self.screen == "storage":
                self.show_settings_demo()
            else:
                self.show_home()
        elif self.screen in {"confirmation", "result"}:
            self.logger.info("LUNASHELL_nav", "Synthetic back navigation")
            self.show_home()

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
            "leg-accel": self.show_leg_demo,
            "leg-gyro": self.show_leg_demo,
            "leg-combo": self.show_leg_demo,
            "leg-motion": self.show_leg_demo,
            "leg-settings": self.show_leg_demo,
            "gps": self.show_gps_demo,
            "tof": self.show_tof_demo,
            "tof-settings": self.show_tof_settings_demo,
            "confirmation": self.show_confirmation,
        }.get(current_screen)
        if redraw is not None:
            redraw()
        elif current_screen == "result":
            self._draw_shell("ESP32 Luna Shell", self.result_message, primary=True, actions=False)
            self.screen = "result"
        else:
            self.show_home()
        self.apply_idle_overlay()
