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

"""Read-only scenario projections, pairing previews, and simulated event logs."""

from __future__ import annotations

import queue
from collections import deque
from dataclasses import dataclass

from device_model import ScenarioEngine
from lunashell_style import STATUS_FAULT, STATUS_MISSING, STATUS_READY, STATUS_WARNING


class CanonicalLogger:
    def __init__(self, now_ms=None) -> None:
        self.lines: deque[str] = deque(maxlen=32)
        self.events: queue.SimpleQueue[str] = queue.SimpleQueue()
        self._now_ms = now_ms or (lambda: 0)

    def info(self, tag: str, message: str, *, at_ms: int | None = None) -> None:
        timestamp_ms = int(self._now_ms() if at_ms is None else at_ms)
        line = f"I ({timestamp_ms:06d}) {tag}: {message}"
        self.lines.append(line)
        self.events.put(line)
        print(line, flush=True)

    def snapshot(self, count: int = 7, max_chars: int | None = None) -> str:
        lines = list(self.lines)[-count:]
        if max_chars is not None:
            lines = [line if len(line) <= max_chars else f"{line[: max_chars - 1]}…" for line in lines]
        return "\n".join(lines)


def component_current(component: dict) -> bool:
    return (
        component.get("health") == "ready"
        and component.get("last_error") == "ESP_OK"
        and component.get("data_valid") is True
        and component.get("data_stale") is False
    )


def component_status(component: dict) -> tuple[str, str]:
    if component_current(component):
        return "READY", STATUS_READY
    if component.get("health") == "missing":
        return "MISSING", STATUS_MISSING
    if component.get("data_stale"):
        return "STALE", STATUS_WARNING
    error = str(component.get("last_error", "ESP_ERR_INVALID_STATE"))
    if error != "ESP_OK":
        return error, STATUS_FAULT
    return str(component.get("health", "unknown")).upper(), STATUS_WARNING


def seconds_label(seconds: int) -> str:
    if seconds > 0 and seconds % 60 == 0:
        minutes = seconds // 60
        return f"{minutes} minute" if minutes == 1 else f"{minutes} minutes"
    return f"{seconds} second" if seconds == 1 else f"{seconds} seconds"


class ScenarioView:
    """Compatibility view whose values always come from the live snapshot."""

    def __init__(self, engine: ScenarioEngine) -> None:
        self.engine = engine

    @property
    def snapshot(self) -> dict:
        return self.engine.snapshot()

    @property
    def name(self) -> str:
        return self.engine.scenario.title.upper()

    @property
    def battery(self) -> dict:
        return self.snapshot["battery"]

    @property
    def battery_percent(self) -> int | None:
        battery = self.battery
        if not (
            component_current(battery)
            and battery["battery_present"]
            and battery["level_percent_valid"]
        ):
            return None
        return int(battery["level_percent"])

    @property
    def battery_mv(self) -> int | None:
        battery = self.battery
        if not (
            component_current(battery)
            and battery["battery_present"]
            and battery["battery_mv_valid"]
        ):
            return None
        return int(battery["battery_mv"])

    @property
    def usb_present(self) -> bool | None:
        return self.battery["usb_present"] if component_current(self.battery) else None

    @property
    def usb_mv(self) -> int | None:
        return int(self.battery["usb_mv"]) if component_current(self.battery) else None

    @property
    def system_mv(self) -> int | None:
        return int(self.battery["system_mv"]) if component_current(self.battery) else None

    @property
    def die_temperature_c(self) -> float | None:
        return float(self.battery["die_temperature_c"]) if component_current(self.battery) else None

    @property
    def battery_present(self) -> bool | None:
        return self.battery["battery_present"] if component_current(self.battery) else None

    @property
    def battery_state(self) -> str:
        return str(self.battery["power_state"]).upper() if component_current(self.battery) else "UNAVAILABLE"

    @property
    def battery_health(self) -> str:
        return str(self.battery["health"]).upper()

    @property
    def battery_error(self) -> str:
        return str(self.battery["last_error"])

    @property
    def battery_sample_age_s(self) -> float | None:
        success = int(self.battery["last_success_at_us"])
        if not component_current(self.battery) or success <= 0:
            return None
        return max(0.0, (self.engine.clock.now_us - success) / 1_000_000.0)

    @property
    def charger_phase(self) -> str | None:
        return str(self.battery["charger_phase"]).upper() if component_current(self.battery) else None

    @property
    def batfet_enabled(self) -> bool | None:
        return self.battery["batfet_enabled"] if component_current(self.battery) else None

    @property
    def battery_active_mode(self) -> bool | None:
        return self.battery["battery_active_mode"] if component_current(self.battery) else None

    @property
    def thermal_regulation_active(self) -> bool | None:
        return self.battery["thermal_regulation_active"] if component_current(self.battery) else None

    @property
    def input_current_limit_active(self) -> bool | None:
        return self.battery["input_current_limit_active"] if component_current(self.battery) else None

    @property
    def sd_message(self) -> str:
        storage = self.snapshot["storage"]
        if storage["mounted"] and component_current(storage):
            return f"SD: synthetic FAT mounted, capacity={storage['capacity_bytes'] // 1_048_576} MB"
        if storage["format_required"]:
            return "SD: verified synthetic card requires explicit FAT formatting"
        state, _ = component_status(storage)
        return f"SD: synthetic storage {state}"

    @property
    def gps_message(self) -> str:
        gps = self.snapshot["gps"]
        if component_current(gps) and gps["has_fix"]:
            return f"GPS LC76G: synthetic fix {gps['latitude_degrees']:.4f}, {gps['longitude_degrees']:.4f}"
        if component_current(gps):
            return "GPS LC76G: receiver ready, waiting for synthetic fix"
        state, _ = component_status(gps)
        return f"GPS LC76G: synthetic receiver {state}"

    @property
    def final_message(self) -> str:
        snapshot = self.snapshot
        if not snapshot["last_update_complete"]:
            return (
                "Synthetic snapshot incomplete: "
                f"{snapshot['last_update_error']}"
            )
        degraded = [
            name
            for name, component in (
                ("battery", snapshot["battery"]),
                ("imu", snapshot["imu"]),
                ("gps", snapshot["gps"]),
                ("storage", snapshot["storage"]),
                ("distance", snapshot["distance"]),
                ("memory", snapshot["memory"]),
            )
            if not component_current(component)
        ]
        return (
            f"Synthetic snapshot {snapshot['sequence']} ready"
            + (f"; unavailable: {', '.join(degraded)}" if degraded else "")
        )


@dataclass(frozen=True)
class PairingPreview:
    title: str
    step: str
    message: str
    detail: str
    progress: int
    show_qr: bool = False
    result: str = "normal"


PAIRING_PREVIEWS = {
    "pairing-intro": PairingPreview(
        "PAIR DEVICE",
        "STEP 1 OF 5",
        "Connect LUNASHELL to your Wi-Fi network.",
        "Bluetooth carries the Wi-Fi setup details.",
        1,
    ),
    "pairing-bluetooth": PairingPreview(
        "BLUETOOTH",
        "STEP 2 OF 5",
        "Turn on Bluetooth on your phone.",
        "Allow Nearby devices when prompted.",
        2,
    ),
    "pairing-app": PairingPreview(
        "GET THE APP",
        "STEP 3 OF 5",
        "Install ESP BLE Provisioning on your phone.",
        "Next shows the official Google Play download.",
        3,
    ),
    "pairing-app-qr": PairingPreview(
        "INSTALL APP",
        "STEP 4 OF 5",
        "Scan to install ESP BLE Provisioning.",
        "Then return here and press Next.",
        4,
        show_qr=True,
    ),
    "pairing-device-qr": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "In the app, scan this device QR.",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-credentials": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "Wi-Fi details received. Preparing the connection...",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-connecting": PairingPreview(
        "SCAN DEVICE",
        "STEP 5 OF 5",
        "Wi-Fi details received. Connecting...",
        "PROV_DEVICE-SIM1",
        5,
        show_qr=True,
    ),
    "pairing-success": PairingPreview(
        "WI-FI READY",
        "CONNECTED",
        "Wireless state comes from the canonical synthetic snapshot.",
        "Connection details are shown only after a current model fix.",
        5,
        result="success",
    ),
    "pairing-failed": PairingPreview(
        "SETUP FAILED",
        "ACTION REQUIRED",
        "Wi-Fi password was rejected. Check it and try again.",
        "Press Next to try again.",
        0,
        result="failed",
    ),
}
