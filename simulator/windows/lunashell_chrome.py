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

"""Shared round-canvas geometry, status, and navigation controls."""

from __future__ import annotations

import math
import tkinter as tk

from PIL import Image, ImageColor, ImageDraw, ImageFilter, ImageTk

from lunashell_state import component_current, component_status
from lunashell_style import (
    CIRCULAR_CONTENT_INSET,
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
    MUTED,
    MUTED_BRIGHT,
    NAVY,
    NEUTRAL_BORDER,
    NEUTRAL_BOTTOM,
    NEUTRAL_GLOW,
    NEUTRAL_TOP,
    PRIMARY_BORDER,
    PRIMARY_BOTTOM,
    PRIMARY_GLOW,
    PRIMARY_TOP,
    SAFE_ORIGIN,
    SAFE_SIZE,
    STATUS_EDGE,
    STATUS_FAULT,
    STATUS_GLOW,
    STATUS_READY,
    STATUS_WARNING,
    SUCCESS_BORDER,
    SUCCESS_BOTTOM,
    SUCCESS_GLOW,
    SUCCESS_TOP,
    TEXT,
    WARNING_BORDER,
    WARNING_BOTTOM,
    WARNING_GLOW,
    WARNING_TOP,
    WIDE_PAGE_SCREENS,
    command_geometry,
)


class LunaShellChrome:
    def _status_arc_visible(self) -> bool:
        return self.idle_state in {"active", "command-hidden"}

    def _command_arc_visible(self) -> bool:
        return self.idle_state == "active" and self.screen in COMMAND_SCREENS

    def _content_viewport(self) -> tuple[int, int, int, int]:
        top = SAFE_ORIGIN if self._status_arc_visible() else 0
        bottom_inset = SAFE_ORIGIN if self._command_arc_visible() else 0
        return (
            SAFE_ORIGIN,
            top,
            SAFE_SIZE,
            DISPLAY_SIZE - top - bottom_inset,
        )

    def _content_offset_y(self) -> int:
        _, top, _, _ = self._content_viewport()
        return top - SAFE_ORIGIN

    def _wide_page_offset_y(self) -> int:
        _, top, _, _ = self._content_viewport()
        return -35 if top == 0 else 0

    def _fixed_canvas_offset_y(self) -> int:
        _, top, _, height = self._content_viewport()
        centered_top = top + ((height - SAFE_SIZE) // 2)
        return centered_top - SAFE_ORIGIN

    def _next_canvas_item_id(self) -> int:
        items = self.canvas.find_all()
        return items[-1] + 1 if items else 1

    def _move_canvas_items_from(self, first_item_id: int, y_offset: int) -> None:
        if y_offset == 0:
            return
        for item_id in self.canvas.find_all():
            if item_id >= first_item_id:
                self.canvas.move(item_id, 0, y_offset)

    def _scrollable_content_bounds(self) -> tuple[int, int]:
        _, top, _, height = self._content_viewport()
        bottom = top + height
        return (
            max(top, CIRCULAR_CONTENT_INSET),
            min(bottom, DISPLAY_SIZE - CIRCULAR_CONTENT_INSET),
        )

    def _draw_safe_guide(self) -> None:
        if self.safe_guide:
            x, y, width, height = self._content_viewport()
            if (
                self.screen not in WIDE_PAGE_SCREENS
                and self.screen not in FIXED_CANVAS_SCREENS
                and (y != SAFE_ORIGIN or height != SAFE_SIZE)
            ):
                self.canvas.create_rectangle(
                    SAFE_ORIGIN,
                    SAFE_ORIGIN,
                    SAFE_ORIGIN + SAFE_SIZE - 1,
                    SAFE_ORIGIN + SAFE_SIZE - 1,
                    outline="#755c3b",
                    width=1,
                    dash=(2, 5),
                    tags="ui",
                )
            self.canvas.create_rectangle(
                x,
                y,
                x + width - 1,
                y + height - 1,
                outline="#f2994a",
                width=2,
                dash=(6, 4),
                tags="ui",
            )
            self.canvas.create_text(
                DISPLAY_SIZE - 14,
                y + (height // 2),
                text=f"VIEWPORT  {width} × {height}  ·  Y {y}",
                fill="#ffc879",
                font=(FONT_TECH, 8, "bold"),
                angle=90,
                tags="ui",
            )
            if self.screen in WIDE_PAGE_SCREENS:
                content_top, content_bottom = self._scrollable_content_bounds()
                if content_top != y or content_bottom != y + height:
                    self.canvas.create_rectangle(
                        x + 4,
                        content_top,
                        x + width - 5,
                        content_bottom,
                        outline="#63ffd0",
                        width=1,
                        dash=(3, 3),
                        tags="ui",
                    )
                    self.canvas.create_text(
                        x + 13,
                        content_top + ((content_bottom - content_top) // 2),
                        text=f"WIDE CONTENT  ·  Y {content_top}..{content_bottom}",
                        fill="#8fffe3",
                        font=(FONT_TECH, 7, "bold"),
                        angle=90,
                        tags="ui",
                    )
            elif self.screen in FIXED_CANVAS_SCREENS:
                fixed_top = SAFE_ORIGIN + self._fixed_canvas_offset_y()
                if fixed_top != y or height != SAFE_SIZE:
                    self.canvas.create_rectangle(
                        x + 4,
                        fixed_top,
                        x + width - 5,
                        fixed_top + SAFE_SIZE - 1,
                        outline="#8f7dff",
                        width=1,
                        dash=(3, 3),
                        tags="ui",
                    )
                    self.canvas.create_text(
                        x + 13,
                        fixed_top + (SAFE_SIZE // 2),
                        text=f"FIXED CANVAS  ·  Y {fixed_top}..{fixed_top + SAFE_SIZE}",
                        fill="#b8adff",
                        font=(FONT_TECH, 7, "bold"),
                        angle=90,
                        tags="ui",
                    )

    def _draw_atmosphere(self) -> None:
        self.canvas.create_image(
            0,
            0,
            image=self.atmosphere_image,
            anchor="nw",
            tags="ui",
        )

    def _build_atmosphere(self) -> ImageTk.PhotoImage:
        start = ImageColor.getrgb("#0d3547")
        end = ImageColor.getrgb("#010407")
        lower_blue = ImageColor.getrgb("#071b34")
        pixels: list[tuple[int, int, int, int]] = []
        for y in range(DISPLAY_SIZE):
            for x in range(DISPLAY_SIZE):
                screen_distance = math.hypot(x - 232.5, y - 232.5)
                if screen_distance >= 233:
                    alpha = max(0, int((234 - screen_distance) * 255))
                else:
                    alpha = 255

                progress = min(math.hypot(x - 233, y - 158) / 368, 1.0)
                progress = progress * progress * (3.0 - (2.0 * progress))
                upper_channels = tuple(
                    round(a + ((b - a) * progress))
                    for a, b in zip(start, end)
                )
                lower_strength = max(
                    0.0,
                    1.0 - (math.hypot(x - 278, y - 286) / 282),
                ) ** 2
                vignette = 1.0 - (
                    max(0.0, min((screen_distance - 152) / 82, 1.0)) * 0.72
                )
                channels = tuple(
                    round(
                        min(
                            255,
                            (channel * vignette) +
                            (lower * lower_strength * 0.42),
                        )
                    )
                    for channel, lower in zip(upper_channels, lower_blue)
                )
                pixels.append((*channels, alpha))

        image = Image.new("RGBA", (DISPLAY_SIZE, DISPLAY_SIZE))
        image.putdata(pixels)
        draw = ImageDraw.Draw(image)
        draw.ellipse(
            (2, 2, DISPLAY_SIZE - 3, DISPLAY_SIZE - 3),
            outline=(*ImageColor.getrgb(STATUS_EDGE), 190),
            width=1,
        )
        return ImageTk.PhotoImage(image)

    def _draw_panel(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        *,
        top: str = "#0d2735",
        bottom: str = "#07131c",
        border: str = "#24536a",
        glow: str = STATUS_GLOW,
        glow_alpha: int = 42,
        radius: int = 14,
    ) -> None:
        scale = 4
        pad = 10
        image_size = ((width + (pad * 2)) * scale, (height + (pad * 2)) * scale)
        image = Image.new("RGBA", image_size, (0, 0, 0, 0))
        bounds = (
            pad * scale,
            pad * scale,
            (pad + width - 1) * scale,
            (pad + height - 1) * scale,
        )

        if glow_alpha > 0:
            glow_layer = Image.new("RGBA", image_size, (0, 0, 0, 0))
            ImageDraw.Draw(glow_layer).rounded_rectangle(
                bounds,
                radius=radius * scale,
                fill=(*ImageColor.getrgb(glow), glow_alpha),
            )
            image.alpha_composite(
                glow_layer.filter(ImageFilter.GaussianBlur(7 * scale))
            )

        mask = Image.new("L", image_size, 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            bounds,
            radius=radius * scale,
            fill=255,
        )
        gradient = Image.new("RGBA", image_size, (0, 0, 0, 0))
        gradient_draw = ImageDraw.Draw(gradient)
        top_rgb = ImageColor.getrgb(top)
        bottom_rgb = ImageColor.getrgb(bottom)
        for row in range(height * scale):
            progress = row / max(1, (height * scale) - 1)
            color = tuple(
                round(a + ((b - a) * progress))
                for a, b in zip(top_rgb, bottom_rgb)
            )
            gradient_draw.line(
                (
                    (pad * scale, (pad * scale) + row),
                    ((pad + width) * scale, (pad * scale) + row),
                ),
                fill=(*color, 244),
            )
        image.alpha_composite(
            Image.composite(gradient, Image.new("RGBA", image_size), mask)
        )

        image = image.resize(
            (width + (pad * 2), height + (pad * 2)),
            Image.Resampling.LANCZOS,
        )
        ImageDraw.Draw(image).rounded_rectangle(
            (pad, pad, pad + width - 1, pad + height - 1),
            radius=radius,
            outline=(*ImageColor.getrgb(border), 225),
            width=CONTROL_BORDER_PX,
        )
        photo = ImageTk.PhotoImage(image)
        self.effect_images.append(photo)
        self.canvas.create_image(
            x - pad,
            y - pad,
            image=photo,
            anchor="nw",
            tags="ui",
        )

    def _build_command_button(
        self,
        role: str,
        circle_size: int,
    ) -> ImageTk.PhotoImage:
        scale = 4
        sprite_size = circle_size + 24
        pad = (sprite_size - circle_size) // 2
        top, bottom, border, glow = {
            "primary": (
                PRIMARY_TOP,
                PRIMARY_BOTTOM,
                PRIMARY_BORDER,
                PRIMARY_GLOW,
            ),
            "success": (
                SUCCESS_TOP,
                SUCCESS_BOTTOM,
                SUCCESS_BORDER,
                SUCCESS_GLOW,
            ),
            "warning": (
                WARNING_TOP,
                WARNING_BOTTOM,
                WARNING_BORDER,
                WARNING_GLOW,
            ),
            "danger": (
                DANGER_TOP,
                DANGER_BOTTOM,
                DANGER_BORDER,
                DANGER_GLOW,
            ),
            "neutral": (
                NEUTRAL_TOP,
                NEUTRAL_BOTTOM,
                NEUTRAL_BORDER,
                NEUTRAL_GLOW,
            ),
        }[role]

        canvas = Image.new(
            "RGBA",
            (sprite_size * scale, sprite_size * scale),
            (0, 0, 0, 0),
        )
        glow_layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        glow_draw = ImageDraw.Draw(glow_layer)
        glow_draw.ellipse(
            (
                pad * scale,
                pad * scale,
                (pad + circle_size) * scale,
                (pad + circle_size) * scale,
            ),
            fill=(*ImageColor.getrgb(glow), 175),
        )
        glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(7 * scale))
        canvas.alpha_composite(glow_layer)

        mask = Image.new("L", canvas.size, 0)
        mask_draw = ImageDraw.Draw(mask)
        bounds = (
            pad * scale,
            pad * scale,
            (pad + circle_size) * scale,
            (pad + circle_size) * scale,
        )
        mask_draw.ellipse(bounds, fill=255)

        gradient = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        gradient_draw = ImageDraw.Draw(gradient)
        top_rgb = ImageColor.getrgb(top)
        bottom_rgb = ImageColor.getrgb(bottom)
        for row in range(circle_size * scale):
            progress = row / max(1, (circle_size * scale) - 1)
            color = tuple(
                round(a + ((b - a) * progress))
                for a, b in zip(top_rgb, bottom_rgb)
            )
            y = (pad * scale) + row
            gradient_draw.line(
                ((pad * scale, y), ((pad + circle_size) * scale, y)),
                fill=(*color, 255),
                width=1,
            )
        canvas.alpha_composite(Image.composite(gradient, Image.new("RGBA", canvas.size), mask))

        canvas = canvas.resize(
            (sprite_size, sprite_size),
            Image.Resampling.LANCZOS,
        )
        ImageDraw.Draw(canvas).ellipse(
            (
                pad,
                pad,
                pad + circle_size,
                pad + circle_size,
            ),
            outline=(*ImageColor.getrgb(border), 255),
            width=CONTROL_BORDER_PX,
        )
        return ImageTk.PhotoImage(canvas)

    def _draw_glow_disc(
        self,
        x: int,
        y: int,
        radius: int,
        color: str,
        alpha: int = 115,
    ) -> None:
        blur = 8
        size = (radius + blur) * 2
        image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.ellipse(
            (blur, blur, size - blur, size - blur),
            fill=(*ImageColor.getrgb(color), alpha),
        )
        image = image.filter(ImageFilter.GaussianBlur(blur))
        photo = ImageTk.PhotoImage(image)
        self.effect_images.append(photo)
        self.canvas.create_image(x, y, image=photo, tags="ui")

    def _status_colors(self) -> list[str]:
        snapshot = self._snapshot()
        battery = snapshot["battery"]
        wifi = snapshot["radios"]["wifi"]
        ble = snapshot["radios"]["ble"]
        gps = snapshot["gps"]
        storage = snapshot["storage"]

        if component_current(battery):
            usb_color = STATUS_READY if battery["usb_present"] else STATUS_WARNING
            level = self.scenario.battery_percent
            battery_color = (
                STATUS_FAULT
                if level is None or level <= 12
                else STATUS_WARNING if level <= 30 else STATUS_READY
            )
        else:
            _, usb_color = component_status(battery)
            battery_color = usb_color

        def radio_color(component: dict, running: bool) -> str:
            if component_current(component):
                return STATUS_READY
            if not running and component.get("last_error") == "ESP_ERR_INVALID_STATE":
                return STATUS_WARNING
            return component_status(component)[1]

        if component_current(gps):
            gps_color = STATUS_READY if gps["has_fix"] else STATUS_WARNING
        else:
            gps_color = component_status(gps)[1]

        if self._storage_format_allowed():
            storage_color = STATUS_WARNING
        elif component_current(storage) and storage["mounted"]:
            storage_color = STATUS_READY
        else:
            storage_color = component_status(storage)[1]

        return [
            usb_color,
            battery_color,
            radio_color(ble, component_current(ble)),
            radio_color(wifi, wifi["started"]),
            gps_color,
            storage_color,
        ]

    def _draw_status_icon(self, kind: str, x: int, y: int, color: str) -> None:
        if kind == "usb":
            self.canvas.create_line(x, y - 10, x, y + 8, fill=color, width=3, tags="ui")
            self.canvas.create_line(x - 7, y - 6, x + 7, y - 6, fill=color, width=3, tags="ui")
            self.canvas.create_line(x - 5, y - 12, x - 5, y - 5, fill=color, width=2, tags="ui")
            self.canvas.create_line(x + 5, y - 12, x + 5, y - 5, fill=color, width=2, tags="ui")
            self.canvas.create_line(x - 4, y + 8, x + 4, y + 8, fill=color, width=3, tags="ui")
        elif kind == "battery":
            self.canvas.create_rectangle(
                x - 11,
                y - 8,
                x + 10,
                y + 8,
                outline=color,
                width=2,
                tags="ui",
            )
            self.canvas.create_rectangle(
                x + 11,
                y - 4,
                x + 14,
                y + 4,
                fill=color,
                outline=color,
                tags="ui",
            )
            if self.scenario.battery_percent is not None:
                fill_width = round(13 * self.scenario.battery_percent / 100)
                if fill_width > 0:
                    self.canvas.create_rectangle(
                        x - 8,
                        y - 5,
                        x - 8 + fill_width,
                        y + 5,
                        fill=color,
                        outline="",
                        tags="ui",
                    )
        elif kind == "ble":
            points = (x, y - 13, x + 8, y - 6, x, y, x + 8, y + 7, x, y + 13, x, y - 13)
            self.canvas.create_line(*points, fill=color, width=2, tags="ui")
            self.canvas.create_line(x - 7, y - 7, x, y, x - 7, y + 7, fill=color, width=2, tags="ui")
        elif kind == "wifi":
            for inset in (0, 6, 12):
                self.canvas.create_arc(
                    x - 15 + inset,
                    y - 12 + inset,
                    x + 15 - inset,
                    y + 14 - inset,
                    start=35,
                    extent=110,
                    style=tk.ARC,
                    outline=color,
                    width=2,
                    tags="ui",
                )
            self.canvas.create_oval(x - 2, y + 8, x + 2, y + 12, fill=color, outline="", tags="ui")
        elif kind == "gps":
            self.canvas.create_oval(x - 10, y - 12, x + 10, y + 8, outline=color, width=2, tags="ui")
            self.canvas.create_oval(x - 3, y - 5, x + 3, y + 1, fill=color, outline="", tags="ui")
            self.canvas.create_line(x - 7, y + 5, x, y + 14, x + 7, y + 5, fill=color, width=2, tags="ui")
        elif kind == "sd":
            self.canvas.create_polygon(
                x - 11,
                y - 13,
                x + 4,
                y - 13,
                x + 11,
                y - 6,
                x + 11,
                y + 13,
                x - 11,
                y + 13,
                outline=color,
                fill="",
                width=2,
                tags="ui",
            )
            for offset in (-5, 0, 5):
                self.canvas.create_line(
                    x + offset,
                    y - 9,
                    x + offset,
                    y - 3,
                    fill=color,
                    width=2,
                    tags="ui",
                )

    def _draw_status_bar(self) -> None:
        if not self._status_arc_visible():
            return
        positions = (
            ("usb", 106, 53, 78),
            ("battery", 156, 33, 58),
            ("ble", 206, 22, 47),
            ("wifi", 256, 22, 47),
            ("gps", 306, 33, 58),
            ("sd", 356, 53, 78),
        )
        for (kind, x, icon_y, dot_y), color in zip(positions, self._status_colors()):
            self._draw_glow_disc(x, icon_y, 12, color, 105)
            self._draw_glow_disc(x, dot_y, 5, color, 150)
            self._draw_status_icon(kind, x, icon_y, color)
            self.canvas.create_oval(
                x - 4,
                dot_y - 4,
                x + 4,
                dot_y + 4,
                fill=color,
                outline="",
                tags="ui",
            )

    def _draw_command_button(
        self,
        x: int,
        y: int,
        icon: str,
        role: str | bool,
        diameter: int = EDGE_COMMAND_DIAMETER,
    ) -> None:
        if isinstance(role, bool):
            role = "success" if role else "primary"
        icon_x, icon_y = {
            "➤": (1, 0),
            "i": (0, -1),
            "‹": (0, -2),
        }.get(icon, (0, 0))
        self.canvas.create_image(
            x + (diameter / 2),
            y + (diameter / 2),
            image=self.button_images[(role, diameter)],
            tags="ui",
        )
        if icon == "battery":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_rectangle(
                center_x - 12,
                center_y - 8,
                center_x + 9,
                center_y + 8,
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_rectangle(
                center_x + 10,
                center_y - 4,
                center_x + 13,
                center_y + 4,
                fill="#ffffff",
                outline="#ffffff",
                tags="ui",
            )
            self.canvas.create_rectangle(
                center_x - 9,
                center_y - 5,
                center_x + 3,
                center_y + 5,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            return
        if icon == "wifi":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2) + 4
            for radius in (5, 10, 15):
                self.canvas.create_arc(
                    center_x - radius,
                    center_y - radius,
                    center_x + radius,
                    center_y + radius,
                    start=35,
                    extent=110,
                    style=tk.ARC,
                    outline="#ffffff",
                    width=2,
                    tags="ui",
                )
            self.canvas.create_oval(
                center_x - 2,
                center_y - 2,
                center_x + 2,
                center_y + 2,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            return
        if icon == "power":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_arc(
                center_x - 12,
                center_y - 12,
                center_x + 12,
                center_y + 12,
                start=40,
                extent=280,
                style=tk.ARC,
                outline="#ffffff",
                width=3,
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y - 14,
                center_x,
                center_y,
                fill="#ffffff",
                width=3,
                capstyle=tk.ROUND,
                tags="ui",
            )
            return
        if icon == "gps":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_oval(
                center_x - 10,
                center_y - 10,
                center_x + 10,
                center_y + 10,
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_oval(
                center_x - 3,
                center_y - 3,
                center_x + 3,
                center_y + 3,
                fill="#ffffff",
                outline="",
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y - 15,
                center_x,
                center_y - 8,
                fill="#ffffff",
                width=2,
                tags="ui",
            )
            self.canvas.create_line(
                center_x,
                center_y + 8,
                center_x,
                center_y + 15,
                fill="#ffffff",
                width=2,
                tags="ui",
            )
            return
        if icon == "sd":
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            self.canvas.create_polygon(
                center_x - 10,
                center_y - 14,
                center_x + 5,
                center_y - 14,
                center_x + 11,
                center_y - 8,
                center_x + 11,
                center_y + 14,
                center_x - 10,
                center_y + 14,
                fill="",
                outline="#ffffff",
                width=2,
                tags="ui",
            )
            for offset in (-5, 0, 5):
                self.canvas.create_line(
                    center_x + offset,
                    center_y - 11,
                    center_x + offset,
                    center_y - 6,
                    fill="#ffffff",
                    width=2,
                    tags="ui",
                )
            return
        if icon in {"‹", "›"}:
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            direction = -1 if icon == "‹" else 1
            tip_x = center_x + (direction * 6)
            tail_x = center_x - (direction * 5)
            self.canvas.create_line(
                tail_x,
                center_y - 10,
                tip_x,
                center_y,
                tail_x,
                center_y + 10,
                fill="#ffffff",
                width=3,
                capstyle=tk.ROUND,
                joinstyle=tk.ROUND,
                tags="ui",
            )
            return
        if icon in {"fft", "dual"}:
            center_x = x + (diameter / 2)
            center_y = y + (diameter / 2)
            channel_offsets = (-5, 5) if icon == "dual" else (0,)
            for channel_offset in channel_offsets:
                for index, height in enumerate((7, 14, 10)):
                    bar_x = center_x + channel_offset + ((index - 1) * 4)
                    self.canvas.create_line(
                        bar_x,
                        center_y + (height / 2),
                        bar_x,
                        center_y - (height / 2),
                        fill="#ffffff",
                        width=2,
                        capstyle=tk.ROUND,
                        tags="ui",
                    )
            return
        self.canvas.create_text(
            x + (diameter / 2) + icon_x,
            y + (diameter / 2) + icon_y,
            text=icon,
            fill="#ffffff",
            font=("Segoe UI Symbol", 18, "bold"),
            tags="ui",
        )

    def _draw_command_arc(self) -> None:
        if not self._command_arc_visible():
            return
        commands = (
            ("battery", False),
            ("♫", False),
            ("▰", True),
            ("➤", True),
            ("⚙", False),
        )
        for index, (icon, success) in enumerate(commands):
            x, y, diameter = command_geometry(len(commands), index)
            self._draw_command_button(x, y, icon, success, diameter)

    def _draw_edge_back(self) -> None:
        self._draw_command_button(10, 203, "‹", True)

    def _draw_edge_next(self) -> None:
        self._draw_command_button(396, 203, "›", True)

    def _draw_scenario_label(self) -> None:
        self.canvas.create_text(
            DISPLAY_SIZE // 2,
            108,
            text=f"SYNTHETIC · {self.scenario.name}",
            fill=MUTED,
            font=(FONT_DISPLAY, 8),
            tags="ui",
        )

    def _draw_page_heading(
        self,
        title: str,
        subtitle: str,
        y_offset: int | None = None,
        *,
        title_size: int = 21,
        subtitle_baseline: int = 136,
        divider_y: int = 154,
    ) -> None:
        if y_offset is None:
            y_offset = self._content_offset_y()
        self.canvas.create_text(
            94,
            110 + y_offset,
            anchor="w",
            text=title,
            fill=TEXT,
            font=(FONT_DISPLAY, title_size),
            tags="ui",
        )
        self.canvas.create_text(
            94,
            subtitle_baseline + y_offset,
            anchor="w",
            text=subtitle,
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 12),
            tags="ui",
        )
        self.canvas.create_line(
            94,
            divider_y + y_offset,
            126,
            divider_y + y_offset,
            fill=STATUS_READY,
            width=2,
            capstyle=tk.ROUND,
            tags="ui",
        )
        self.canvas.create_line(
            134,
            divider_y + y_offset,
            372,
            divider_y + y_offset,
            fill="#163545",
            width=1,
            tags="ui",
        )

    def _draw_performance_disabled(
        self,
        service: str,
        show_next_command: bool = False,
        y_offset: int = 0,
    ) -> None:
        self.canvas.create_text(
            233,
            190 + y_offset,
            text="PERFORMANCE MODE",
            fill=STATUS_WARNING,
            font=(FONT_DISPLAY, 14),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            226 + y_offset,
            text=f"{service} DISABLED",
            fill=TEXT,
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            254 + y_offset,
            text="CONNECTIVITY REMAINS AVAILABLE",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        if show_next_command and self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▥", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def _draw_settings_control(self, y: int, value: str) -> None:
        self._draw_panel(
            92,
            y,
            282,
            44,
            top="#173145",
            bottom="#0d1a24",
            border="#3a6c83",
            glow="#0b55d9",
            glow_alpha=25,
            radius=10,
        )
        self.canvas.create_text(
            106,
            y + 22,
            anchor="w",
            text=value,
            fill=TEXT,
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self.canvas.create_text(
            356,
            y + 22,
            text="⌄",
            fill="#d8ecff",
            font=("Segoe UI Symbol", 15, "bold"),
            tags="ui",
        )

    def _draw_settings_switch(
        self,
        y: int,
        title: str,
        subtitle: str | None,
        checked: bool,
    ) -> None:
        self.canvas.create_text(
            94,
            y,
            anchor="w",
            text=title,
            fill=TEXT,
            font=(FONT_DISPLAY, 11),
            tags="ui",
        )
        if subtitle is not None:
            self.canvas.create_text(
                94,
                y + 20,
                anchor="w",
                text=subtitle,
                fill=MUTED_BRIGHT,
                font=(FONT_BODY, 8),
                tags="ui",
            )
        self.canvas.create_oval(
            318,
            y - 4,
            372,
            y + 26,
            fill=STATUS_READY if checked else "#243744",
            outline="#63ffd0" if checked else "#506675",
            width=2,
            tags="ui",
        )
        knob_x = 356 if checked else 334
        self.canvas.create_oval(
            knob_x - 10,
            y + 1,
            knob_x + 10,
            y + 21,
            fill="#ffffff",
            outline="",
            tags="ui",
        )

    def _draw_shell(self, title: str, message: str, primary: bool, actions: bool) -> None:
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        self._draw_scenario_label()
        self.canvas.create_text(
            118,
            132,
            anchor="w",
            text=title.upper(),
            fill=TEXT,
            font=(FONT_DISPLAY, 19),
            tags="ui",
        )
        self.canvas.create_text(
            118,
            160,
            anchor="nw",
            text=message,
            width=300 if primary else 258,
            justify="left",
            fill=MUTED_BRIGHT,
            font=(FONT_BODY, 9),
            tags="ui",
        )
        if primary:
            self.canvas.create_line(
                118,
                188,
                150,
                188,
                fill=STATUS_READY,
                width=2,
                capstyle=tk.ROUND,
                tags="ui",
            )
            self.canvas.create_line(
                158,
                188,
                350,
                184,
                fill="#163545",
                width=1,
                tags="ui",
            )
            self._draw_home_telemetry()
            self._draw_command_arc()
        if actions:
            self.canvas.create_oval(6, 190, 68, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(37, 233, text="CANCEL", fill="white", font=("Segoe UI", 8, "bold"), tags="ui")
            self.canvas.create_oval(398, 190, 460, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(429, 233, text="OK", fill="white", font=("Segoe UI", 9, "bold"), tags="ui")
        self._draw_safe_guide()
