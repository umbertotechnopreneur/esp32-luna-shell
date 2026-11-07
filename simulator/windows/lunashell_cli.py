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

"""Command-line, headless scenario, and capture entry points for Luna Shell."""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
import time
import tkinter as tk
from pathlib import Path

from device_model import (
    ScenarioEngine,
    ScenarioValidationError,
    SemanticActionError,
    load_scenario,
    scenario_names,
)
from lunashell_style import PAIRING_SCREENS
from runtime_paths import resolve_runtime_output


def _pump_capture_events(root: tk.Tk, delay_ms: int) -> None:
    deadline = time.monotonic() + delay_ms / 1000.0
    while True:
        root.update()
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        time.sleep(min(0.01, remaining))


# simulator_factory: Existing Luna Shell window constructor used by the interactive and capture routes.
# Raises SystemExit when arguments or an explicitly requested scenario are invalid.
def run_cli(simulator_factory) -> None:
    available_scenarios = scenario_names()
    parser = argparse.ArgumentParser(
        description="LUNASHELL deterministic Windows device simulator",
    )
    parser.add_argument("--scenario", choices=available_scenarios, default="healthy")
    parser.add_argument("--scenario-file", type=Path)
    parser.add_argument("--at-ms", type=int, default=0)
    parser.add_argument("--list-scenarios", action="store_true")
    parser.add_argument("--validate-scenarios", action="store_true")
    parser.add_argument("--dump-snapshot", action="store_true")
    parser.add_argument(
        "--dump-api",
        choices=(
            "/api/v1/health",
            "/api/v1/capabilities",
            "/api/v1/info",
            "/api/v1/diagnostics",
            "/api/v1/rtc",
            "/api/v1/gps",
            "/api/v1/status",
        ),
        metavar="PATH",
    )
    parser.add_argument("--action", action="append", default=[])
    parser.add_argument("--advance-ms", type=int, default=0)
    parser.add_argument("--output", type=Path,
                        help="Output file; relative names use artifacts/simulator, explicit paths must stay under artifacts")
    parser.add_argument(
        "--screen",
        choices=(
            "home",
            "battery",
            "logs",
            "settings",
            "storage",
            "chrome-settings",
            "power-settings",
            "hardware",
            "audio",
            "fft",
            "fft-detail",
            "imu",
            "leg-accel", "leg-gyro", "leg-combo", "leg-motion", "leg-settings",
            "gps",
            "tof",
            "tof-settings",
            *sorted(PAIRING_SCREENS),
        ),
        default="home",
    )
    parser.add_argument(
        "--idle-state",
        choices=(
            "active",
            "command-hidden",
            "chrome-hidden",
            "fullscreen",
            "dimmed",
            "off",
            "standby",
        ),
        default="active",
    )
    parser.add_argument("--capture", type=Path,
                        help="Capture PNG; relative names use artifacts/simulator, explicit paths must stay under artifacts")
    parser.add_argument("--capture-after-ms", type=int, default=250,
                        help="Pump simulator events for this many real milliseconds before capture")
    parser.add_argument("--safe-guide", action="store_true")
    parser.add_argument("--no-sound", action="store_true")
    parser.add_argument("--performance-mode", action="store_true")
    parser.add_argument("--wireless-running", action="store_true")
    parser.add_argument("--board-config", action="store_true",
                        help="Preview the reviewed ui-bootstrap YAML and firmware shell")
    parser.add_argument("--dump-board-shell", action="store_true",
                        help="Print the synthetic ui-bootstrap shell contract as JSON")
    parser.add_argument("--shell-taps", type=int, default=0,
                        help="Set synthetic ui-bootstrap touch count for headless checks")
    args = parser.parse_args()
    if args.capture_after_ms < 0:
        parser.error("--capture-after-ms must be non-negative")
    try:
        if args.capture is not None:
            args.capture = resolve_runtime_output(args.capture)
        if args.output is not None:
            args.output = resolve_runtime_output(args.output)
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))

    if args.board_config:
        import yaml
        from board_shell import ShellContract, ShellPreview

        incompatible = {
            "--scenario", "--scenario-file", "--at-ms", "--list-scenarios",
            "--validate-scenarios", "--dump-snapshot", "--dump-api", "--action",
            "--advance-ms", "--screen", "--idle-state", "--safe-guide",
            "--no-sound", "--performance-mode", "--wireless-running",
        }
        supplied_options = {argument.split("=", 1)[0] for argument in sys.argv[1:]}
        if incompatible.intersection(supplied_options):
            parser.error("ui-bootstrap supports only the shell; device scenarios and pages are unavailable")
        if args.capture is not None and (
            args.dump_board_shell or args.output is not None or "--shell-taps" in supplied_options
        ):
            parser.error("--capture cannot be combined with headless shell output")
        if args.shell_taps < 0:
            parser.error("--shell-taps must be non-negative")
        try:
            contract = ShellContract.load()
            status = contract.status(args.shell_taps)
        except (OSError, ValueError, KeyError, TypeError, yaml.YAMLError) as error:
            parser.error(f"ui-bootstrap contract failed: {error}")
        if args.dump_board_shell or args.output is not None or "--shell-taps" in supplied_options:
            output_text = json.dumps(status, indent=2, sort_keys=True)
            if args.output is not None:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(output_text + "\n", encoding="utf-8")
            else:
                print(output_text)
            return
        if sys.platform == "win32":
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        root = tk.Tk()
        root.title("ESP32 Luna Shell Simulator · UI BOOTSTRAP · SYNTHETIC")
        root.resizable(False, False)
        root.configure(background="#101820")
        preview = ShellPreview(root, contract)
        if args.capture is not None:
            from PIL import ImageGrab

            root.geometry("+100+100")
            root.deiconify()
            root.attributes("-topmost", True)
            root.lift()
            root.focus_force()
            root.update_idletasks()
            _pump_capture_events(root, args.capture_after_ms)
            args.capture.parent.mkdir(parents=True, exist_ok=True)
            captured = ImageGrab.grab(window=preview.canvas.winfo_id(),
                                      include_layered_windows=True)
            captured.save(args.capture)
            root.destroy()
            return
        root.mainloop()
        return
    if args.dump_board_shell or "--shell-taps" in {
        argument.split("=", 1)[0] for argument in sys.argv[1:]
    }:
        parser.error("--dump-board-shell and --shell-taps require --board-config")

    if args.at_ms < 0:
        parser.error("--at-ms must be non-negative")
    if args.advance_ms < 0:
        parser.error("--advance-ms must be non-negative")
    if args.scenario_file is not None and "--scenario" in sys.argv[1:]:
        parser.error("--scenario and --scenario-file are mutually exclusive")

    if args.list_scenarios:
        catalog = [
            {
                "name": definition.name,
                "title": definition.title,
                "description": definition.description,
            }
            for definition in (load_scenario(name) for name in available_scenarios)
        ]
        output_text = json.dumps(
            {"synthetic": True, "scenarios": catalog},
            indent=2,
            sort_keys=True,
        )
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output_text + "\n", encoding="utf-8")
        else:
            print(output_text)
        return

    if args.validate_scenarios:
        validated = []
        for name in available_scenarios:
            definition = load_scenario(name)
            engine = ScenarioEngine.from_scenario(name)
            if definition.timeline:
                engine.advance_to_ms(definition.timeline[-1].at_ms)
            validated.append(
                {
                    "name": name,
                    "events": len(definition.timeline),
                    "final_sequence": engine.generation.sequence,
                }
            )
        output_text = json.dumps(
            {"synthetic": True, "valid": True, "scenarios": validated},
            indent=2,
            sort_keys=True,
        )
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output_text + "\n", encoding="utf-8")
        else:
            print(output_text)
        return

    scenario_source: str | Path = args.scenario_file or args.scenario
    try:
        engine = ScenarioEngine.from_scenario(
            scenario_source,
            start_at_ms=args.at_ms,
        )
        if args.wireless_running and not engine.snapshot()["radios"]["wifi"]["started"]:
            engine.action("start:wireless")
        for action in args.action:
            engine.action(action)
        if args.advance_ms:
            engine.advance_ms(args.advance_ms)
    except (ScenarioValidationError, SemanticActionError) as error:
        parser.error(str(error))

    headless = bool(
        args.dump_snapshot
        or args.dump_api is not None
        or args.action
        or args.advance_ms
        or args.output is not None
    )
    if headless:
        try:
            output_text = (
                engine.dump_api(args.dump_api)
                if args.dump_api is not None
                else engine.dump_snapshot()
            )
        except ScenarioValidationError as error:
            parser.error(str(error))
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output_text + "\n", encoding="utf-8")
        else:
            print(output_text)
        return

    if sys.platform == "win32":
        ctypes.windll.shcore.SetProcessDpiAwareness(2)

    root = tk.Tk()
    root.title(f"ESP32 Luna Shell Device Simulator · {engine.scenario.title.upper()}")
    root.resizable(False, False)
    root.configure(background="#101820")
    simulator = simulator_factory(
        root,
        engine,
        play_sound=not args.no_sound and args.capture is None,
        idle_state=args.idle_state,
    )
    simulator.performance_mode = args.performance_mode
    simulator.safe_guide = args.safe_guide
    screen_drawers = {
        "home": simulator.show_home,
        "battery": simulator.show_battery_demo,
        "logs": simulator.show_logs_demo,
        "settings": simulator.show_settings_demo,
        "storage": simulator.show_storage_demo,
        "chrome-settings": simulator.show_chrome_settings_demo,
        "power-settings": simulator.show_power_settings_demo,
        "hardware": simulator.show_hardware_demo,
        "audio": simulator.show_audio_demo,
        "fft": simulator.show_audio_fft_demo,
        "fft-detail": simulator.show_audio_detail_demo,
        "imu": simulator.show_imu_demo,
        "leg-accel": lambda: simulator.show_leg_demo("leg-accel"),
        "leg-gyro": lambda: simulator.show_leg_demo("leg-gyro"),
        "leg-combo": lambda: simulator.show_leg_demo("leg-combo"),
        "leg-motion": lambda: simulator.show_leg_demo("leg-motion"),
        "leg-settings": lambda: simulator.show_leg_demo("leg-settings"),
        "gps": simulator.show_gps_demo,
        "tof": simulator.show_tof_demo,
        "tof-settings": simulator.show_tof_settings_demo,
    }
    simulator.ready = True
    if args.screen in PAIRING_SCREENS:
        simulator.show_pairing_demo(args.screen)
    else:
        screen_drawers[args.screen]()
    simulator.apply_idle_overlay()

    if args.capture is not None:
        from PIL import ImageGrab

        root.geometry("+100+100")
        root.deiconify()
        root.attributes("-topmost", True)
        root.lift()
        root.focus_force()
        root.update_idletasks()
        _pump_capture_events(root, args.capture_after_ms)
        args.capture.parent.mkdir(parents=True, exist_ok=True)
        x = simulator.canvas.winfo_rootx()
        y = simulator.canvas.winfo_rooty()
        width = simulator.canvas.winfo_width()
        height = simulator.canvas.winfo_height()
        if sys.platform == "win32":
            # Capture the simulator window directly so validation also works
            # when the desktop session is occluded or not foreground-visible.
            captured = ImageGrab.grab(
                window=simulator.canvas.winfo_id(),
                include_layered_windows=True,
            )
        else:
            captured = ImageGrab.grab(
                bbox=(x, y, x + width, y + height),
                include_layered_windows=True,
            )
        captured.save(args.capture)
        root.destroy()
        return
    root.mainloop()
