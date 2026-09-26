"""Host-only preview of the reviewed ui-bootstrap board and packaged shell view."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml


def resource_path(relative: str) -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2])) / relative


sys.path.insert(0, str(resource_path("scripts")))
from board_config import UniqueKeyLoader, validate_board_config_data  # noqa: E402


DEFAULT_BOARD_CONFIG = "config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml"
SHELL_VIEW = "components/LUNASHELL_app/package_shell/res/root.json"


def _pixels(value: str) -> int:
    if not isinstance(value, str) or not value.endswith("dp"):
        raise ValueError(f"shell placement must use integer dp: {value!r}")
    return int(value[:-2])


def _font_pixels(value: str) -> int:
    if not isinstance(value, str) or not value.endswith("sp"):
        raise ValueError(f"shell font size must use integer sp: {value!r}")
    return int(value[:-2])


@dataclass(frozen=True)
class ShellContract:
    board: str
    profile: str
    background: str
    surface: tuple[int, int, int, int]
    title: str
    title_position: tuple[int, int, int, int]
    title_color: str
    title_size: int
    prompt: str
    prompt_position: tuple[int, int, int, int]
    prompt_color: str
    prompt_size: int
    action: str

    @classmethod
    def load(cls, board_path: Path | None = None) -> "ShellContract":
        board_path = board_path or resource_path(DEFAULT_BOARD_CONFIG)
        data = yaml.load(board_path.read_text(encoding="utf-8"), Loader=UniqueKeyLoader)
        build = validate_board_config_data(data)
        document = json.loads(resource_path(SHELL_VIEW).read_text(encoding="utf-8"))
        screens = [asset for asset in document["assets"] if asset["type"] == "viewScreen"]
        if len(screens) != 1 or screens[0]["id"] != "LUNASHELL_shell":
            raise ValueError("ui-bootstrap shell must contain one LUNASHELL_shell screen")
        flows = [asset for asset in document["assets"] if asset["type"] == "screenFlow"]
        if len(flows) != 1 or flows[0]["screens"] != ["LUNASHELL_shell"] or flows[0]["initial"] != "LUNASHELL_shell":
            raise ValueError("ui-bootstrap shell flow must launch only LUNASHELL_shell")
        view = screens[0]
        if len(view["children"]) != 1 or view["children"][0]["id"] != "touch_surface":
            raise ValueError("ui-bootstrap shell touch surface is missing")
        surface = view["children"][0]
        if len(surface["events"]) != 1 or surface["events"][0] != {
            "type": "clicked", "action": "LUNASHELL.shell.touch"
        }:
            raise ValueError("ui-bootstrap shell touch action differs from firmware")
        labels = {child["id"]: child for child in surface["children"]}
        if set(labels) != {"title", "touch_count"}:
            raise ValueError("ui-bootstrap shell labels differ from firmware")
        def rectangle(node: dict) -> tuple[int, int, int, int]:
            place = node["placement"]
            if place["mode"] != "absolute":
                raise ValueError("ui-bootstrap shell requires absolute placement")
            return tuple(_pixels(place[key]) for key in ("x", "y", "width", "height"))

        title = labels["title"]
        prompt = labels["touch_count"]
        return cls(
            board=build["board"],
            profile=build["profile"],
            background=view["style"]["bgColor"],
            surface=rectangle(surface),
            title=title["labelProps"]["text"],
            title_position=rectangle(title),
            title_color=title["style"]["textColor"],
            title_size=_font_pixels(title["style"]["fontSize"]),
            prompt=prompt["labelProps"]["text"],
            prompt_position=rectangle(prompt),
            prompt_color=prompt["style"]["textColor"],
            prompt_size=_font_pixels(prompt["style"]["fontSize"]),
            action=surface["events"][0]["action"],
        )

    def status(self, taps: int = 0) -> dict:
        if taps < 0:
            raise ValueError("shell tap count must be non-negative")
        return {
            "synthetic": True,
            "board": self.board,
            "profile": self.profile,
            "screen": "LUNASHELL_shell",
            "screens": ["LUNASHELL_shell"],
            "surface": dict(zip(("x", "y", "width", "height"), self.surface)),
            "touch_action": self.action,
            "title": self.title,
            "touch_count": taps,
            "touch_label": self.prompt if taps == 0 else f"TOUCH {taps}",
        }

    def contains_touch(self, x: int, y: int) -> bool:
        sx, sy, width, height = self.surface
        return sx <= x < sx + width and sy <= y < sy + height


class ShellPreview:
    def __init__(self, root, contract: ShellContract, taps: int = 0) -> None:
        import tkinter as tk

        root.tk.call("tk", "scaling", 1.0)
        self.contract = contract
        self.taps = taps
        self.canvas = tk.Canvas(root, width=466, height=466, highlightthickness=0,
                                background="#101820")
        self.canvas.pack(padx=18, pady=18)
        self.canvas.create_oval(0, 0, 465, 465, fill=contract.background,
                                outline="#245363", width=1)
        x, y, width, height = contract.surface
        self._label(contract.title, contract.title_position, contract.title_color,
                    contract.title_size)
        self.prompt_item = self._label(contract.status(taps)["touch_label"],
                                       contract.prompt_position, contract.prompt_color,
                                       contract.prompt_size)
        self.canvas.create_text(233, 419, text="SYNTHETIC · UI BOOTSTRAP",
                                fill="#8aa7b6", font=("Montserrat", 9))
        self.canvas.bind("<Button-1>", self._touch)

    def _label(self, text, position, color, size):
        sx, sy, _, _ = self.contract.surface
        x, y, width, height = position
        return self.canvas.create_text(sx + x + width // 2, sy + y + height // 2,
                                       text=text, fill=color,
                                       font=("Montserrat", -size, "bold"))

    def _touch(self, event) -> None:
        if self.contract.contains_touch(event.x, event.y):
            self.taps += 1
            self.canvas.itemconfigure(self.prompt_item,
                                      text=self.contract.status(self.taps)["touch_label"])
            print(f"SYNTHETIC {self.contract.action}: TOUCH {self.taps}", flush=True)
