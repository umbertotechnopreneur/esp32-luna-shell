"""QSee.ai round-device UI simulator for Windows.

The simulator mirrors the firmware flow without pretending to emulate physical
drivers. Mouse clicks stand in for touch and deterministic scenarios provide
synthetic PMU, IMU, GPS, Wi-Fi, BLE, SD, and logger data.
"""

from __future__ import annotations

import argparse
import math
import queue
import sys
import time
import tkinter as tk
import winsound
from collections import deque
from dataclasses import dataclass
from pathlib import Path


DISPLAY_SIZE = 466
SAFE_ORIGIN = 73
SAFE_SIZE = 320
BLUE = "#2f80ed"
NAVY = "#071a2c"
MUTED = "#69849c"
BACKGROUND = "#f7fbff"


def resource_path(relative: str) -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2]))
    return base / relative


class CanonicalLogger:
    def __init__(self) -> None:
        self.lines: deque[str] = deque(maxlen=32)
        self.events: queue.SimpleQueue[str] = queue.SimpleQueue()

    def info(self, tag: str, message: str) -> None:
        line = f"I ({int(time.monotonic() * 1000):06d}) {tag}: {message}"
        self.lines.append(line)
        self.events.put(line)
        print(line, flush=True)

    def snapshot(self, count: int = 7, max_chars: int | None = None) -> str:
        lines = list(self.lines)[-count:]
        if max_chars is not None:
            lines = [line if len(line) <= max_chars else f"{line[: max_chars - 1]}…" for line in lines]
        return "\n".join(lines)


@dataclass(frozen=True)
class Scenario:
    name: str
    sd_message: str
    gps_message: str
    final_message: str


SCENARIOS = {
    "healthy": Scenario(
        "HEALTHY",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Synthetic hardware ready",
    ),
    "sd-unformatted": Scenario(
        "SD UNFORMATTED",
        "SD: mount failed; synthetic card requires FAT32 formatting",
        "GPS LC76G: synthetic fix 10.7769, 106.7009",
        "Ready with SD degraded",
    ),
    "gps-searching": Scenario(
        "GPS SEARCHING",
        "SD: synthetic FAT32 card mounted, capacity=8192 MB",
        "GPS LC76G: transport ready, waiting for synthetic fix",
        "Ready; GPS still searching",
    ),
}


class QSeeSimulator:
    def __init__(self, root: tk.Tk, scenario: Scenario) -> None:
        self.root = root
        self.scenario = scenario
        self.logger = CanonicalLogger()
        self.canvas = tk.Canvas(
            root,
            width=DISPLAY_SIZE,
            height=DISPLAY_SIZE,
            highlightthickness=0,
            background="#101820",
        )
        self.canvas.pack(padx=18, pady=18)
        self.canvas.create_oval(0, 0, 465, 465, fill="white", outline=BLUE, width=3, tags="bezel")
        self.screen = "boot"
        self.audio_phase = 0
        self.audio_timer: str | None = None
        self.ready = False
        self.safe_guide = False
        self.timeline_index = 0
        self.boot_image: tk.PhotoImage | None = None
        self._load_assets()
        self._bind_controls()
        self.show_boot()
        self.root.after(150, self._advance_timeline)
        self.root.after(120, self._refresh_log)

    def _load_assets(self) -> None:
        image_path = resource_path("assets/branding/qsee/generated/qsee-boot-466x466.png")
        if image_path.exists():
            self.boot_image = tk.PhotoImage(file=image_path)
        sound_path = resource_path("assets/sfx/generated/qsee-boot.wav")
        if sound_path.exists():
            winsound.PlaySound(str(sound_path), winsound.SND_FILENAME | winsound.SND_ASYNC)

    def _bind_controls(self) -> None:
        self.canvas.bind("<Button-1>", self._on_click)
        self.root.bind("<Escape>", lambda _event: self.back())
        self.root.bind("<Return>", lambda _event: self.confirm())
        self.root.bind("c", lambda _event: self.show_confirmation())
        self.root.bind("r", lambda _event: self.reboot())
        self.root.bind("<F2>", lambda _event: self.toggle_safe_guide())

    def _clear(self) -> None:
        self.canvas.delete("ui", "audio")

    def _draw_safe_guide(self) -> None:
        if self.safe_guide:
            self.canvas.create_rectangle(
                SAFE_ORIGIN,
                SAFE_ORIGIN,
                SAFE_ORIGIN + SAFE_SIZE,
                SAFE_ORIGIN + SAFE_SIZE,
                outline="#f2994a",
                width=2,
                dash=(6, 4),
                tags="ui",
            )

    def _draw_scenario_label(self) -> None:
        self.canvas.create_text(
            DISPLAY_SIZE // 2,
            30,
            text=f"SYNTHETIC · {self.scenario.name}",
            fill=BLUE,
            font=("Segoe UI", 9, "bold"),
            tags="ui",
        )

    def show_boot(self) -> None:
        self.screen = "boot"
        self._clear()
        if self.boot_image is not None:
            self.canvas.create_image(233, 233, image=self.boot_image, tags="ui")
        else:
            self.canvas.create_text(233, 135, text="QSee.ai", fill=BLUE, font=("Segoe UI", 36, "bold"), tags="ui")
        self._draw_scenario_label()
        self.canvas.create_rectangle(73, 222, 393, 374, fill=NAVY, outline=BLUE, width=1, tags="ui")
        self.canvas.create_text(
            87,
            234,
            anchor="nw",
            width=292,
            text=self.logger.snapshot(count=5, max_chars=58),
            fill="#d8ecff",
            font=("Consolas", 8),
            tags=("ui", "boot_log"),
        )
        self.canvas.create_text(
            233,
            430,
            text="TOUCH TO CONTINUE" if self.ready else "INITIALIZING",
            fill=BLUE,
            font=("Segoe UI", 11, "bold"),
            tags=("ui", "continue"),
        )
        self._draw_safe_guide()

    def _refresh_log(self) -> None:
        if self.screen == "boot":
            self.canvas.itemconfigure("boot_log", text=self.logger.snapshot(count=5, max_chars=58))
        self.root.after(120, self._refresh_log)

    def _advance_timeline(self) -> None:
        events = [
            ("qsee_logger", "Canonical logger online: console + RAM ring"),
            ("qsee_main", self.scenario.sd_message),
            ("qsee_boot", "Boot screen online: QSee.ai + canonical live log"),
            ("qsee_main", "Touch CST9217: synthetic input ready"),
            ("qsee_hw", "PMU AXP2101: synthetic battery 83%, discharging, 4021mV"),
            ("qsee_hw", "IMU QMI8658: synthetic orientation x=12 y=-18 z=998mg"),
            ("qsee_hw", self.scenario.gps_message),
            ("qsee_radios", "Wi-Fi: synthetic STA ready, credentials not configured"),
            ("qsee_radios", "BLE: synthetic advertising as QSEE-SIM1"),
            ("qsee_main", self.scenario.final_message),
        ]
        if self.timeline_index < len(events):
            tag, message = events[self.timeline_index]
            self.logger.info(tag, message)
            self.timeline_index += 1
            self.root.after(430, self._advance_timeline)
            return

        self.ready = True
        self.logger.info("qsee_boot", "Startup complete; touch to continue")
        self.canvas.itemconfigure("continue", text="TOUCH TO CONTINUE")

    def _on_click(self, event: tk.Event) -> None:
        if self.screen == "boot":
            if self.ready:
                self.logger.info("qsee_boot", "Synthetic touch confirmed; opening QSee.ai")
                self.show_home()
            else:
                self.logger.info("qsee_boot", "Synthetic touch received; startup still in progress")
            return

        if 6 <= event.x <= 68 and 190 <= event.y <= 276 and self.screen == "confirmation":
            self.cancel()
        elif 398 <= event.x <= 460 and 190 <= event.y <= 276 and self.screen == "confirmation":
            self.confirm()
        elif 160 <= event.x <= 306 and 275 <= event.y <= 307 and self.screen == "home":
            self.show_imu_demo()
        elif 135 <= event.x <= 331 and 315 <= event.y <= 367:
            self.show_audio_demo()
            self.show_confirmation()

    def _draw_shell(self, title: str, message: str, primary: bool, actions: bool) -> None:
        self._clear()
        self._draw_scenario_label()
        self.canvas.create_rectangle(
            SAFE_ORIGIN,
            SAFE_ORIGIN,
            SAFE_ORIGIN + SAFE_SIZE,
            SAFE_ORIGIN + SAFE_SIZE,
            fill="white",
            outline="#d9e9f7",
            width=2,
            tags="ui",
        )
        self.canvas.create_text(233, 126, text=title, fill=NAVY, font=("Segoe UI", 25, "bold"), tags="ui")
        self.canvas.create_text(
            233,
            222,
            text=message,
            width=258,
            justify="center",
            fill=MUTED,
            font=("Segoe UI", 12),
            tags="ui",
        )
        if primary:
            self.canvas.create_oval(160, 275, 306, 307, fill="#16384a", outline="", tags="ui")
            self.canvas.create_text(233, 291, text="OPEN IMU LEVEL", fill="#61d8ff", font=("Segoe UI", 8, "bold"), tags="ui")
            self.canvas.create_oval(135, 315, 331, 367, fill=BLUE, outline="", tags="ui")
            self.canvas.create_text(233, 341, text="OPEN AUDIO SCOPE", fill="white", font=("Segoe UI", 10, "bold"), tags="ui")
        if actions:
            self.canvas.create_oval(6, 190, 68, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(37, 233, text="CANCEL", fill="white", font=("Segoe UI", 8, "bold"), tags="ui")
            self.canvas.create_oval(398, 190, 460, 276, fill=NAVY, outline="", tags="ui")
            self.canvas.create_text(429, 233, text="OK", fill="white", font=("Segoe UI", 9, "bold"), tags="ui")
        self._draw_safe_guide()

    def show_home(self) -> None:
        self.screen = "home"
        self._draw_shell(
            "QSee.ai",
            "Device ready.\nOpen the live two-microphone audio scope.",
            primary=True,
            actions=False,
        )

    def _draw_audio_bars(self, y: int, color_start: str, color_end: str, offset: int) -> None:
        # The simulator keeps the same fast, low-memory bar shape as the target UI.
        widths = (8, 11, 14, 18, 22, 28, 35, 43, 51, 59, 67, 73, 78, 70, 61, 50)
        for index, base in enumerate(widths):
            value = base + int(10 * math.sin((self.audio_phase + index * 3 + offset) / 4.0))
            x = 98 + index * 16
            self.canvas.create_rectangle(
                x,
                y + 82 - value,
                x + 10,
                y + 82,
                fill=color_start if index < 8 else color_end,
                outline="",
                tags="audio",
            )

    def show_audio_demo(self) -> None:
        self.screen = "audio"
        self._clear()
        self.canvas.create_oval(1, 1, 465, 465, fill="#030508", outline="#16202b", width=2, tags="ui")
        self.canvas.create_text(233, 46, text="AUDIO SCOPE", fill="#f4f8fb", font=("Segoe UI", 20, "bold"), tags="ui")
        self.canvas.create_text(233, 72, text="LIVE · ES7210 · 22.05 kHz · DEMO", fill="#6f8292", font=("Consolas", 8), tags="ui")

        self.canvas.create_text(84, 112, text="MIC 1", anchor="w", fill="#61d8ff", font=("Segoe UI", 10, "bold"), tags="ui")
        self.canvas.create_text(84, 200, text="MIC 2", anchor="w", fill="#ff9d5c", font=("Segoe UI", 10, "bold"), tags="ui")
        self._draw_audio_bars(112, "#27bdf2", "#c168ff", 0)
        self._draw_audio_bars(200, "#ffad3d", "#77e37c", 9)

        self.canvas.create_line(84, 302, 382, 302, fill="#17212a", width=1, tags="ui")
        self.canvas.create_text(84, 324, text="CPU LOAD", anchor="w", fill="#718493", font=("Segoe UI", 9, "bold"), tags="ui")
        self.canvas.create_text(84, 349, text="CORE 0", anchor="w", fill="#d8e6ef", font=("Consolas", 9), tags="ui")
        self.canvas.create_text(382, 349, text="23%", anchor="e", fill="#61d8ff", font=("Consolas", 13, "bold"), tags="cpu0")
        self.canvas.create_text(84, 373, text="CORE 1", anchor="w", fill="#d8e6ef", font=("Consolas", 9), tags="ui")
        self.canvas.create_text(382, 373, text="18%", anchor="e", fill="#ff9d5c", font=("Consolas", 13, "bold"), tags="cpu1")

        self.canvas.create_text(84, 407, text="AUDIO DIAGNOSTICS", anchor="w", fill="#718493", font=("Segoe UI", 9, "bold"), tags="ui")
        self.canvas.create_text(84, 429, text="L/R SYNC   0.4 ms", anchor="w", fill="#9aabb7", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 446, text="PEAK        -12.6 dBFS", anchor="w", fill="#9aabb7", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(382, 438, text="● READY", anchor="e", fill="#61e294", font=("Consolas", 9, "bold"), tags="ui")
        self._draw_safe_guide()

    def show_imu_demo(self) -> None:
        self.screen = "imu"
        self._clear()
        self.canvas.create_oval(1, 1, 465, 465, fill="#030508", outline="#16202b", width=2, tags="ui")
        self.canvas.create_text(233, 42, text="IMU LEVEL", fill="#f4f8fb", font=("Segoe UI", 20, "bold"), tags="ui")
        self.canvas.create_text(233, 68, text="QMI8658 · CALIBRATION VIEW · SIMULATOR", fill="#6f8292", font=("Consolas", 8), tags="ui")
        self.canvas.create_oval(148, 88, 318, 258, outline="#2a6174", width=2, fill="#0d171e", tags="ui")
        self.canvas.create_line(168, 173, 298, 173, fill="#29404b", tags="ui")
        self.canvas.create_line(233, 108, 233, 238, fill="#29404b", tags="ui")
        self.canvas.create_oval(218, 158, 248, 188, fill="#5be4ff", outline="#d8faff", width=2, tags="ui")
        self.canvas.create_text(84, 285, text="ACC X +0.02  Y -0.04  Z +9.79 m/s²", anchor="w", fill="#61d8ff", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 307, text="GYRO X +0.10  Y -0.03  Z +0.02 dps", anchor="w", fill="#ff9d5c", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 329, text="ROLL +0.2°  PITCH -0.1°  |g| 9.80", anchor="w", fill="#f4d35e", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 351, text="TEMP +28.4 C     IMU READY", anchor="w", fill="#61e294", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 373, text="CALIBRATION READY (64/64)", anchor="w", fill="#61e294", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(84, 395, text="COMPASS N/A · no magnetometer", anchor="w", fill="#c5a6ff", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(233, 395, text="Hold flat; bubble centers at 0°", fill="#6f8292", font=("Consolas", 8), tags="ui")
        self.canvas.create_text(233, 423, text="ESC · BACK", fill="#6f8292", font=("Consolas", 9, "bold"), tags="ui")
        self._animate_audio()

    def _animate_audio(self) -> None:
        if self.screen != "audio":
            return
        self.audio_phase += 1
        self.canvas.delete("audio")
        self._draw_audio_bars(112, "#27bdf2", "#c168ff", 0)
        self._draw_audio_bars(200, "#ffad3d", "#77e37c", 9)
        cpu0 = 20 + int(7 * math.sin(self.audio_phase / 8.0))
        cpu1 = 17 + int(6 * math.sin(self.audio_phase / 9.0 + 1.2))
        self.canvas.itemconfigure("cpu0", text=f"{cpu0}%")
        self.canvas.itemconfigure("cpu1", text=f"{cpu1}%")
        self.audio_timer = self.root.after(70, self._animate_audio)

    def show_confirmation(self) -> None:
        if self.screen == "boot":
            return
        self.screen = "confirmation"
        self.logger.info("qsee_nav", "Synthetic confirmation screen presented")
        self._draw_shell(
            "Confirm action?",
            "The side margins are reserved for deterministic Cancel and OK actions.",
            primary=False,
            actions=True,
        )

    def confirm(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("qsee_nav", "Synthetic confirmation accepted")
        self.screen = "result"
        self._draw_shell("QSee.ai", "Confirmed", primary=True, actions=False)

    def cancel(self) -> None:
        if self.screen != "confirmation":
            return
        self.logger.info("qsee_nav", "Synthetic confirmation cancelled")
        self.show_home()

    def back(self) -> None:
        if self.screen in {"audio", "imu"}:
            if self.audio_timer is not None:
                self.root.after_cancel(self.audio_timer)
                self.audio_timer = None
            self.logger.info("qsee_ui", "Diagnostic page closed")
            self.show_home()
        elif self.screen in {"confirmation", "result"}:
            self.logger.info("qsee_nav", "Synthetic back navigation")
            self.show_home()

    def reboot(self) -> None:
        self.ready = False
        self.timeline_index = 0
        self.logger = CanonicalLogger()
        self.show_boot()
        self.root.after(150, self._advance_timeline)

    def toggle_safe_guide(self) -> None:
        self.safe_guide = not self.safe_guide
        if self.screen == "boot":
            self.show_boot()
        elif self.screen == "confirmation":
            self.show_confirmation()
        else:
            self.show_home()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=SCENARIOS, default="healthy")
    args = parser.parse_args()

    root = tk.Tk()
    root.title(f"QSee.ai Device Simulator · {SCENARIOS[args.scenario].name}")
    root.resizable(False, False)
    root.configure(background="#101820")
    QSeeSimulator(root, SCENARIOS[args.scenario])
    root.mainloop()


if __name__ == "__main__":
    main()
