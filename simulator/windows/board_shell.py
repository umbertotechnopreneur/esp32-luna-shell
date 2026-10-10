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

"""Host-only preview of the reviewed ui-bootstrap board and packaged shell view."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml


def resource_path(relative: str) -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[2])) / relative


sys.path.insert(0, str(resource_path("")))
from scripts.common.board_config import UniqueKeyLoader, validate_board_config_data  # noqa: E402


DEFAULT_BOARD_CONFIG = "config/boards/waveshare-esp32s3-touch-amoled-1.75/ui-bootstrap.yaml"
SHELL_VIEW = "components/LUNASHELL_app/package_shell/res/root.json"


def _pixels(value: str) -> int:
    if not isinstance(value, str) or not value.endswith("dp"):
        raise ValueError(f"shell placement must use integer dp: {value!r}")
    return int(value[:-2])


@dataclass(frozen=True)
class ShellContract:
    board: str
    profile: str
    background_top: str
    background_bottom: str
    surface: tuple[int, int, int, int]
    mascot: tuple[int, int, int, int]
    face_parts: tuple[tuple[str, tuple[int, int, int, int], str], ...]
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
        children = {child["id"]: child for child in surface["children"]}
        if set(children) != {"mascot"}:
            raise ValueError("ui-bootstrap shell mascot differs from firmware")
        def rectangle(node: dict) -> tuple[int, int, int, int]:
            place = node["placement"]
            if place["mode"] != "absolute":
                raise ValueError("ui-bootstrap shell requires absolute placement")
            return tuple(_pixels(place[key]) for key in ("x", "y", "width", "height"))

        mascot = children["mascot"]
        mascot_children = {child["id"]: child for child in mascot["children"]}
        if set(mascot_children) != {"body", "face"}:
            raise ValueError("ui-bootstrap mascot body/face rig differs from firmware")
        if mascot_children["body"]["bindings"] != {"imageProps.src": "src"}:
            raise ValueError("ui-bootstrap mascot image binding differs from firmware")
        face = mascot_children["face"]
        face_nodes = {child["id"]: child for child in face["children"]}
        if set(face_nodes) != {"left_eye", "right_eye", "mouth", "left_cheek", "right_cheek"}:
            raise ValueError("ui-bootstrap idle face rig differs from firmware")
        face_parts = tuple(
            (name, rectangle(face_nodes[name]), face_nodes[name]["style"]["bgColor"])
            for name in ("left_eye", "right_eye", "mouth", "left_cheek", "right_cheek")
        )
        return cls(
            board=build["board"],
            profile=build["profile"],
            background_top=view["style"]["bgColor"],
            background_bottom=view["style"]["bgGradientColor"],
            surface=rectangle(surface),
            mascot=rectangle(mascot),
            face_parts=face_parts,
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
            "background": {"top": self.background_top, "bottom": self.background_bottom},
            "mascot": {
                "pose": "front_idle",
                "face": "rigged",
                "breathing": True,
                "bounds": dict(zip(("x", "y", "width", "height"), self.mascot)),
            },
            "touch_count": taps,
        }

    def contains_touch(self, x: int, y: int) -> bool:
        sx, sy, width, height = self.surface
        return sx <= x < sx + width and sy <= y < sy + height


class ShellPreview:
    def __init__(self, root, contract: ShellContract, taps: int = 0) -> None:
        import tkinter as tk
        from PIL import Image, ImageColor, ImageDraw, ImageTk

        root.tk.call("tk", "scaling", 1.0)
        self.contract = contract
        self.taps = taps
        self.canvas = tk.Canvas(root, width=466, height=466, highlightthickness=0,
                                background="#101820")
        self.canvas.pack(padx=18, pady=18)
        backdrop = Image.new("RGBA", (466, 466))
        draw = ImageDraw.Draw(backdrop)
        top = ImageColor.getrgb(contract.background_top)
        bottom = ImageColor.getrgb(contract.background_bottom)
        for y in range(466):
            fraction = y / 465
            color = tuple(round(a + (b - a) * fraction) for a, b in zip(top, bottom))
            draw.line((0, y, 465, y), fill=(*color, 255))
        mask = Image.new("L", (466, 466), 0)
        ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
        backdrop.putalpha(mask)
        self.backdrop_image = ImageTk.PhotoImage(backdrop)
        self.canvas.create_image(0, 0, image=self.backdrop_image, anchor="nw")

        sprite = Image.open(resource_path("assets/mascot/generated/luna-idle-front-232x250.png"))
        sprite = sprite.convert("RGBA")
        if sprite.size != contract.mascot[2:]:
            raise ValueError("ui-bootstrap mascot preview size differs from firmware")
        face = ImageDraw.Draw(sprite)
        for name, (x, y, width, height), color in contract.face_parts:
            bounds = (x, y, x + width - 1, y + height - 1)
            if name.endswith("cheek"):
                face.ellipse(bounds, fill=color)
            else:
                face.rectangle(bounds, fill=color)
        self.breath_frames = [ImageTk.PhotoImage(
            sprite.resize((round(sprite.width * scale), round(sprite.height * scale)),
                          Image.Resampling.NEAREST)
        ) for scale in (1.0, 1.005, 1.01, 1.015, 1.01, 1.005)]
        sx, sy, _, _ = contract.surface
        mx, my, width, height = contract.mascot
        self.mascot_item = self.canvas.create_image(sx + mx + width // 2,
                                                     sy + my + height // 2,
                                                     image=self.breath_frames[0])
        self.breath_index = 0
        self.canvas.create_text(233, 419, text="SYNTHETIC · UI BOOTSTRAP",
                                fill="#8aa7b6", font=("Montserrat", 9))
        self.canvas.bind("<Button-1>", self._touch)
        root.after(300, self._breathe)

    def _breathe(self) -> None:
        self.breath_index = (self.breath_index + 1) % len(self.breath_frames)
        self.canvas.itemconfigure(self.mascot_item, image=self.breath_frames[self.breath_index])
        self.canvas.after(300, self._breathe)

    def _touch(self, event) -> None:
        if self.contract.contains_touch(event.x, event.y):
            self.taps += 1
            print(f"SYNTHETIC {self.contract.action}: TOUCH {self.taps}", flush=True)
