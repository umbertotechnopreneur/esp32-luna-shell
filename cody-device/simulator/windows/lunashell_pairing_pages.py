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

"""Synthetic pairing presentation and its local navigation."""

from __future__ import annotations

from lunashell_state import PAIRING_PREVIEWS, PairingPreview, component_current, component_status
from lunashell_style import (
    DISPLAY_SIZE,
    FONT_BODY,
    FONT_DISPLAY,
    MUTED,
    MUTED_BRIGHT,
    STATUS_FAULT,
    STATUS_READY,
    TEXT,
)


class LunaShellPairingPages:
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
        if screen == "pairing-success" and not self._wireless_running():
            self._apply_action("start:wireless")
        preview = PAIRING_PREVIEWS[screen]
        if screen == "pairing-success":
            wifi = self._snapshot()["radios"]["wifi"]
            connected = bool(
                component_current(wifi)
                and wifi["connected"]
                and wifi["ip_acquired"]
                and wifi["ssid"]
                and wifi["ip_address"]
            )
            if connected:
                preview = PairingPreview(
                    "WI-FI READY",
                    "CONNECTED",
                    f"Network: {wifi['ssid']}\nIP: {wifi['ip_address']}",
                    "Canonical synthetic connection is current. Next returns to Settings.",
                    5,
                    result="success",
                )
            else:
                wifi_state, _ = component_status(wifi)
                lifecycle = "STARTED · NO LEASE" if wifi["started"] else wifi_state
                preview = PairingPreview(
                    "WI-FI NOT READY",
                    "NOT CONNECTED",
                    f"Canonical synthetic state: {lifecycle}",
                    "No SSID or IP is shown until the model reports a current connection.",
                    5,
                    result="failed",
                )
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
            "LUNASHELL_pairing",
            f"Synthetic pairing navigation: {self.screen} -> {target}",
        )
        if target == "settings":
            self.show_settings_demo()
        else:
            self.show_pairing_demo(target)
