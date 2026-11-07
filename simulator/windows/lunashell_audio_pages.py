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

"""Audio, spectrum, and band detail views."""

from __future__ import annotations

from lunashell_state import component_current, component_status
from lunashell_style import (
    FONT_DISPLAY,
    FONT_TECH,
    MUTED,
    MUTED_BRIGHT,
    STATUS_READY,
    TEXT,
    command_geometry,
)


class LunaShellAudioPages:
    def _draw_audio_bars(
        self,
        baseline: int,
        max_height: int,
        color_start: str,
        color_end: str,
        values: list[int],
        available: bool,
    ) -> None:
        normalized = [max(0, int(value)) for value in values[:16]]
        peak = max(normalized, default=0)
        for index in range(16):
            x = 102 + (index * 16)
            self.canvas.create_rectangle(
                x,
                baseline - max_height,
                x + 9,
                baseline,
                fill="#0b2632",
                outline="",
                tags="audio",
            )
            if not available or index >= len(normalized) or peak <= 0:
                continue
            value = max(1, round(max_height * normalized[index] / peak))
            self.canvas.create_rectangle(
                x,
                baseline - value,
                x + 9,
                baseline,
                fill=color_start if index < 8 else color_end,
                outline="",
                tags="audio",
            )
            self.canvas.create_line(
                x + 1,
                baseline - value,
                x + 8,
                baseline - value,
                fill="#c9f7ff",
                width=1,
                tags="audio",
            )

    def show_audio_demo(self) -> None:
        self.screen = "audio"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        snapshot = self._snapshot()
        audio = snapshot["audio"]
        microphone = audio["microphone"]
        spectrum = audio["spectrum"]
        microphone_current = component_current(microphone)
        spectrum_current = component_current(spectrum)
        sample_rate_hz = int(audio["sample_rate_hz"])
        sample_rate_label = (
            f"{sample_rate_hz / 1000:g} kHz"
            if microphone_current and sample_rate_hz > 0
            else component_status(microphone)[0]
        )
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO SCOPE",
            "Microphones · diagnostics"
            if self.performance_mode
            else f"Synthetic PCM16 · {sample_rate_label}",
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "AUDIO CAPTURE",
                True,
                y_offset,
            )
            return
        channel_means = audio["channel_mean_level"]
        channel_peaks = audio["channel_peak_level"]
        channel_spectrum = audio["channel_spectrum"]
        microphone_state = component_status(microphone)[0]
        channel_values = [
            (
                f"MEAN {channel_means[index]} · PEAK {channel_peaks[index]}"
                if microphone_current
                else microphone_state
            )
            for index in range(2)
        ]
        self.canvas.create_text(
            102,
            180,
            text="MIC 1",
            anchor="w",
            fill="#61d8ff",
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self.canvas.create_text(
            364,
            180,
            text=channel_values[0],
            anchor="e",
            fill=TEXT,
            font=(FONT_TECH, 10, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            102,
            254,
            text="MIC 2",
            anchor="w",
            fill="#ff9d5c",
            font=(FONT_DISPLAY, 12),
            tags="ui",
        )
        self.canvas.create_text(
            364,
            254,
            text=channel_values[1],
            anchor="e",
            fill=TEXT,
            font=(FONT_TECH, 10, "bold"),
            tags="ui",
        )
        self.canvas.create_line(
            102,
            246,
            364,
            246,
            fill="#183949",
            width=1,
            tags="ui",
        )
        self._draw_audio_bars(
            239,
            43,
            "#27bdf2",
            "#c168ff",
            channel_spectrum[0],
            spectrum_current,
        )
        self._draw_audio_bars(
            313,
            43,
            "#ffad3d",
            "#77e37c",
            channel_spectrum[1],
            spectrum_current,
        )

        utilization = [
            int(core["utilization_percent"])
            for core in snapshot["processor"]["cores"]
            if core["available"] and core["utilization_available"]
        ]
        cpu_state = (
            f"CPU {round(sum(utilization) / len(utilization))}%"
            if utilization
            else "CPU UNAVAILABLE"
        )
        stream_state = (
            f"PCM16 · {audio['channel_count']} CHANNELS · {cpu_state}"
            if microphone_current and spectrum_current
            else f"AUDIO {microphone_state} · SPECTRUM {component_status(spectrum)[0]}"
        )

        self.canvas.create_text(
            233,
            347,
            text=stream_state,
            fill=(
                STATUS_READY
                if microphone_current and spectrum_current
                else component_status(microphone)[1]
            ),
            font=(FONT_TECH, 9, "bold"),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            role = "success" if spectrum_current else "warning"
            commands = (("fft", role), ("dual", role))
            for index, (icon, role) in enumerate(commands):
                x, y, diameter = command_geometry(len(commands), index)
                self._draw_command_button(x, y, icon, role, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_audio_fft_demo(self) -> None:
        if self.audio_timer is not None:
            self.root.after_cancel(self.audio_timer)
            self.audio_timer = None
        self.screen = "fft"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        audio = self._snapshot()["audio"]
        spectrum = audio["spectrum"]
        spectrum_current = component_current(spectrum)
        spectrum_state, spectrum_color = component_status(spectrum)
        bins = [
            round((left + right) / 2)
            for left, right in zip(
                audio["channel_spectrum"][0],
                audio["channel_spectrum"][1],
            )
        ]
        peak_value = max(bins, default=0) if spectrum_current else 0
        dominant_index = bins.index(peak_value) if spectrum_current and bins else -1
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO FFT",
            "Spectrum diagnostics"
            if self.performance_mode
            else (
                "Canonical mean of Mic 1 + Mic 2 | 16 bins"
                if spectrum_current
                else f"Synthetic spectrum {spectrum_state}"
            ),
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "FFT SAMPLING",
                True,
                y_offset,
            )
            return
        self.canvas.create_text(
            104,
            179,
            anchor="w",
            text="DOMINANT",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            104,
            194,
            anchor="w",
            text=(
                f"BIN {dominant_index + 1}"
                if dominant_index >= 0
                else "UNAVAILABLE"
            ),
            fill=STATUS_READY if spectrum_current else spectrum_color,
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        self.canvas.create_text(
            362,
            179,
            anchor="e",
            text="PEAK",
            fill=MUTED,
            font=(FONT_DISPLAY, 9),
            tags="ui",
        )
        self.canvas.create_text(
            362,
            194,
            anchor="e",
            text=f"MAG {peak_value}" if spectrum_current else spectrum_state,
            fill="#f0b44d" if spectrum_current else spectrum_color,
            font=(FONT_TECH, 11, "bold"),
            tags="ui",
        )
        for y, label in ((218, "MAX"), (263, "MID"), (308, "0")):
            self.canvas.create_line(
                108,
                y,
                360,
                y,
                fill="#173746",
                width=1,
                tags="ui",
            )
            self.canvas.create_text(
                102,
                y,
                anchor="e",
                text=label,
                fill="#55778a",
                font=(FONT_TECH, 8),
                tags="ui",
            )
        for index, magnitude in enumerate(bins):
            height = (
                max(1, round(102 * max(0, magnitude) / peak_value))
                if spectrum_current and peak_value > 0
                else 0
            )
            x = 108 + index * 15
            color = "#27bdf2" if index < 6 else "#2be7d0" if index < 11 else "#f0b44d"
            if height <= 0:
                continue
            self.canvas.create_rectangle(
                x,
                309 - height,
                x + 9,
                309,
                fill=color,
                outline="",
                tags="ui",
            )
            self.canvas.create_line(
                x + 1,
                309 - height,
                x + 8,
                309 - height,
                fill="#d5fbff",
                width=1,
                tags="ui",
            )
        self.canvas.create_text(
            108,
            325,
            anchor="w",
            text="BIN 1",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            234,
            325,
            text="BIN 8",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            360,
            325,
            anchor="e",
            text="BIN 16",
            fill=MUTED,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self.canvas.create_text(
            233,
            352,
            text="SELECTED-BIN SPECTRUM · CANONICAL SNAPSHOT",
            fill=MUTED_BRIGHT,
            font=(FONT_TECH, 8),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        if self._command_arc_visible():
            x, y, diameter = command_geometry(1, 0)
            self._draw_command_button(x, y, "▥", True, diameter)
        self._draw_edge_back()
        self._draw_safe_guide()

    def show_audio_detail_demo(self) -> None:
        self.screen = "fft-detail"
        self._clear()
        self._draw_atmosphere()
        self._draw_status_bar()
        audio = self._snapshot()["audio"]
        spectrum = audio["spectrum"]
        spectrum_current = component_current(spectrum)
        spectrum_state, spectrum_color = component_status(spectrum)
        content_start = self._next_canvas_item_id()
        self._draw_page_heading(
            "AUDIO DETAIL",
            "Two-channel spectrum"
            if self.performance_mode
            else (
                "Canonical Mic 1 + Mic 2 · 16 bins"
                if spectrum_current
                else f"Synthetic spectrum {spectrum_state}"
            ),
            0,
        )
        if self.performance_mode:
            y_offset = self._fixed_canvas_offset_y()
            self._move_canvas_items_from(content_start, y_offset)
            self._draw_performance_disabled(
                "AUDIO DETAIL",
                y_offset=y_offset,
            )
            return
        self.canvas.create_text(
            104,
            179,
            anchor="w",
            text="MIC 1  ·  BLUE → MAGENTA",
            fill="#78cfff",
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        self.canvas.create_text(
            104,
            265,
            anchor="w",
            text="MIC 2  ·  ORANGE → GREEN",
            fill="#ffca70",
            font=(FONT_DISPLAY, 10),
            tags="ui",
        )
        mic1_colors = (
            "#078dff", "#0b9dff", "#10afff", "#12c5ff",
            "#16d9ff", "#28e4f2", "#45d9ee", "#5abcfb",
            "#709dff", "#8780ff", "#a466ff", "#bd51ff",
            "#d342ff", "#e834f8", "#f62de7", "#ff35cf",
        )
        mic2_colors = (
            "#ff8700", "#ff9500", "#ffa400", "#ffb300",
            "#ffc000", "#ffcb19", "#ffd43b", "#f4dd4b",
            "#e6e45a", "#d4e660", "#bfe365", "#a8df6b",
            "#8fdb77", "#72d98b", "#4dd6a3", "#25d3bb",
        )
        channels = audio["channel_spectrum"]
        peak = max(
            (max(max(0, int(value)) for value in channel) for channel in channels),
            default=0,
        ) if spectrum_current else 0
        for baseline, colors, magnitudes in (
            (250, mic1_colors, channels[0]),
            (336, mic2_colors, channels[1]),
        ):
            for index, magnitude in enumerate(magnitudes):
                height = (
                    max(1, round(55 * max(0, int(magnitude)) / peak))
                    if spectrum_current and peak > 0
                    else 0
                )
                if height <= 0:
                    continue
                x = 108 + index * 15
                self.canvas.create_rectangle(
                    x,
                    baseline - height,
                    x + 9,
                    baseline,
                    fill=colors[index],
                    outline="",
                    tags="ui",
                )
                self.canvas.create_line(
                    x + 1,
                    baseline - height,
                    x + 8,
                    baseline - height,
                    fill="#f3ffff",
                    width=1,
                    tags="ui",
                )
        self.canvas.create_text(
            233,
            370,
            text=(
                f"CANONICAL SEQUENCE {audio['spectrum_sequence']} · TWO MICS"
                if spectrum_current
                else f"SPECTRUM {spectrum_state}"
            ),
            fill=MUTED_BRIGHT if spectrum_current else spectrum_color,
            font=(FONT_TECH, 9),
            tags="ui",
        )
        self._move_canvas_items_from(
            content_start,
            self._fixed_canvas_offset_y(),
        )
        self._draw_edge_back()
        self._draw_safe_guide()

    def _animate_audio(self) -> None:
        if self.screen != "audio" or self.idle_state in {"off", "standby"}:
            return
        audio = self._snapshot()["audio"]
        spectrum_current = component_current(audio["spectrum"])
        self.canvas.delete("audio")
        y_offset = self._fixed_canvas_offset_y()
        self._draw_audio_bars(
            239 + y_offset,
            43,
            "#27bdf2",
            "#c168ff",
            audio["channel_spectrum"][0],
            spectrum_current,
        )
        self._draw_audio_bars(
            313 + y_offset,
            43,
            "#ffad3d",
            "#77e37c",
            audio["channel_spectrum"][1],
            spectrum_current,
        )
