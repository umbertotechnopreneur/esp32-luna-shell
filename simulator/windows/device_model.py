"""Deterministic, simulator-only LUNASHELL device state.

The raw ``snapshot`` mapping intentionally mirrors the firmware's
``DeviceSnapshot`` shape.  Synthetic provenance is carried only by the outer
``SnapshotGeneration.simulator`` mapping and must never be copied into a
firmware snapshot.
"""

from __future__ import annotations

import copy
import json
import math
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any, Iterable, Mapping, Sequence


SCENARIO_SCHEMA = "LUNASHELL.simulator.scenario.v1"
API_VERSION = 1
SIMULATOR_MODEL_VERSION = 1

HEALTH_VALUES = frozenset({"unknown", "ready", "degraded", "missing"})
ERROR_VALUES = frozenset(
    {
        "ESP_OK",
        "ESP_FAIL",
        "ESP_ERR_NO_MEM",
        "ESP_ERR_INVALID_ARG",
        "ESP_ERR_INVALID_STATE",
        "ESP_ERR_NOT_FOUND",
        "ESP_ERR_NOT_SUPPORTED",
        "ESP_ERR_TIMEOUT",
        "ESP_ERR_INVALID_RESPONSE",
    }
)
STORAGE_OUTCOMES = frozenset(
    {
        "not-attempted",
        "mounted",
        "card-or-transport-failed",
        "format-required",
        "filesystem-failed",
        "format-failed",
        "internal-failed",
    }
)
STORAGE_STAGES = frozenset(
    {
        "none",
        "request",
        "host-init",
        "slot-init",
        "card-init",
        "card-identity",
        "drive-alloc",
        "disk-io-register",
        "vfs-register",
        "fat-mount",
        "pre-format-unmount",
        "partition",
        "format",
        "post-format-mount",
        "runtime-status",
        "runtime-filesystem",
        "runtime-unmount",
    }
)
STORAGE_CLEANUP_STAGES = frozenset(
    {"none", "fat-unmount", "vfs-unregister", "host-deinit"}
)
CALIBRATION_STATES = frozenset(
    {"required", "ready", "running", "succeeded", "failed"}
)
POWER_STATES = frozenset({"active", "entering-standby", "standby", "waking"})
POWER_REASONS = frozenset(
    {"startup", "boot-button", "power-button", "touch", "imu-motion", "inactivity"}
)
DISPLAY_MODES = frozenset({"unknown", "active", "dimmed", "display-off"})


class ScenarioValidationError(ValueError):
    """Raised when a fixture or resulting generation is incoherent."""


class UnknownScenarioError(ScenarioValidationError):
    """Raised when a built-in scenario name cannot be resolved."""


class SemanticActionError(RuntimeError):
    """Raised when a semantic action is invalid for the current state."""


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ScenarioValidationError(message)


def _component(
    timestamp_us: int,
    *,
    health: str = "ready",
    error: str = "ESP_OK",
    valid: bool = True,
    stale: bool = False,
) -> dict[str, Any]:
    success_at_us = timestamp_us if valid or stale else 0
    return {
        "health": health,
        "last_error": error,
        "data_valid": valid,
        "data_stale": stale,
        "last_attempt_at_us": timestamp_us if timestamp_us > 0 else 0,
        "last_success_at_us": success_at_us,
    }


def _off_component() -> dict[str, Any]:
    return _component(
        0,
        health="unknown",
        error="ESP_ERR_INVALID_STATE",
        valid=False,
    )


def _base_snapshot(timestamp_us: int) -> dict[str, Any]:
    component = lambda: _component(timestamp_us)
    tof_1 = {
        **component(),
        "raw_distance_mm": 1018,
        "filtered_distance_mm": 1019,
        "window_sample_count": 40,
        "inlier_count": 38,
        "median_absolute_deviation_mm": 3,
        "calibration_offset_mm": 2,
        "frame_sequence": 120,
        "crc_error_count": 0,
        "protocol_error_count": 0,
        "timeout_count": 0,
        "uart_error_count": 0,
    }
    tof_2 = {
        **component(),
        "raw_distance_mm": 1030,
        "filtered_distance_mm": 1027,
        "window_sample_count": 40,
        "inlier_count": 37,
        "median_absolute_deviation_mm": 4,
        "calibration_offset_mm": -3,
        "frame_sequence": 120,
        "crc_error_count": 0,
        "protocol_error_count": 0,
        "timeout_count": 0,
        "uart_error_count": 0,
    }
    return {
        "sequence": 1,
        "last_update_attempt_at_us": timestamp_us,
        "last_update_complete": True,
        "last_update_error": "ESP_OK",
        "reason": "synthetic-initial",
        "battery": {
            **component(),
            "battery_present": True,
            "usb_present": False,
            "vbus_good": False,
            "batfet_enabled": True,
            "battery_active_mode": False,
            "thermal_regulation_active": False,
            "input_current_limit_active": False,
            "level_percent_valid": True,
            "battery_mv_valid": True,
            "level_percent": 83,
            "battery_mv": 4021,
            "usb_mv": 0,
            "system_mv": 3708,
            "die_temperature_c": 31.6,
            "power_state": "discharging",
            "charger_phase": "stopped",
        },
        "imu": {
            **component(),
            "calibration": component(),
            "acceleration_mps2": [0.03, -0.02, 9.81],
            "angular_velocity_dps": [0.1, -0.1, 0.05],
            "temperature_c": 30.4,
            "calibrated": True,
            "calibration_stationary": True,
            "calibration_samples": 128,
            "calibration_accel_max_span_mps2": 0.06,
            "calibration_gyro_max_span_dps": 0.25,
            "acceleration_bias_mps2": [0.01, -0.01, 0.02],
            "angular_velocity_bias_dps": [0.02, -0.03, 0.01],
        },
        "sensor_fusion": {
            **component(),
            "gravity_dominant": True,
            "stationary": True,
            "acceleration_magnitude_mps2": 9.8101,
            "angular_speed_dps": 0.15,
        },
        "distance": {
            **component(),
            "sensors": [tof_1, tof_2],
            "settings": {
                **component(),
                "sample_count": 40,
                "reference_distance_mm": 1000,
                "sample_interval_ms": 250,
            },
            "calibration": {
                **component(),
                "state": "succeeded",
                "record_valid": True,
                "requested_samples": 40,
                "accepted_samples": [38, 37],
                "rejected_samples": [2, 3],
                "reference_distance_mm": 1000,
                "progress_percent": 100,
                "offsets_mm": [2, -3],
                "z_axis_sign": 1,
                "started_at_us": max(1, timestamp_us - 500_000),
                "completed_at_us": timestamp_us,
            },
            "slant_distance_mm": 1023,
            "vertical_distance_mm": 1023,
            "sensor_spread_mm": 8,
            "samples_used": 37,
            "tilt_degrees": 0.2,
            "confidence_percent": 96,
            "tilt_compensated": True,
            "acquisition_timing_valid": True,
            "tof_pair_skew_us": 240,
            "geometry_skew_us": 400,
            "rejected_pair_count": 2,
            "timing_rejection_count": 0,
            "consecutive_transport_failure_count": 0,
            "recovery_attempt_count": 0,
            "current_backoff_ms": 0,
            "worker_stack_high_water_valid": True,
            "worker_stack_min_free_bytes": 4096,
        },
        "rtc": {
            **component(),
            "system_time": component(),
            "clock_integrity": True,
            "system_time_valid": True,
            "synchronized": True,
            "year": 2026,
            "month": 8,
            "day": 24,
            "hour": 10,
            "minute": 30,
            "second": 0,
            "source": "sntp",
            "timezone": "ICT-7",
        },
        "gps": {
            **component(),
            "pending_nmea_bytes": 0,
            "has_fix": True,
            "utc_valid": True,
            "fix_quality": 1,
            "satellites": 9,
            "latitude_degrees": 10.7769,
            "longitude_degrees": 106.7009,
            "altitude_m": 12.4,
            "speed_mps": 0.0,
            "course_degrees": 0.0,
            "hdop": 0.9,
            "utc_time": "03:30:00Z",
            "utc_date": "2026-08-24",
            "first_sentence": "$GNGGA,SYNTHETIC,VALID*00",
        },
        "audio": {
            "speaker": component(),
            "microphone": component(),
            "spectrum": component(),
            "channel_count": 2,
            "sample_rate_hz": 16000,
            "microphone_mean_level": 620,
            "microphone_peak_level": 1880,
            "channel_mean_level": [600, 640],
            "channel_peak_level": [1800, 1880],
            "channel_spectrum": [
                [8, 12, 18, 24, 31, 38, 44, 49, 45, 39, 33, 27, 21, 16, 12, 9],
                [9, 13, 19, 25, 32, 40, 46, 50, 47, 41, 35, 29, 23, 18, 13, 10],
            ],
            "spectrum_sequence": 42,
        },
        "radios": {
            "wifi": {
                **_off_component(),
                "started": False,
                "credentials_configured": True,
                "connect_requested": False,
                "connected": False,
                "ip_acquired": False,
                "reconnect_pending": False,
                "rssi": 0,
                "channel": 0,
                "last_disconnect_reason": 0,
                "event_sequence": 0,
                "connect_attempt_count": 0,
                "reconnect_attempt_count": 0,
                "disconnect_count": 0,
                "ssid": "",
                "ip_address": "",
                "mac_address": "",
            },
            "ble": {
                **_off_component(),
                "provisioning": False,
                "advertising": False,
                "connected": False,
                "connection_handle": 65535,
                "connection_count": 0,
                "disconnection_count": 0,
                "device_name": "LUNASHELL-SIM",
            },
        },
        "storage": {
            **component(),
            "logger_mirror": component(),
            "mount_outcome": "mounted",
            "mount_stage": "fat-mount",
            "cleanup_stage": "none",
            "cleanup_error": "ESP_OK",
            "initial_fatfs_result_valid": True,
            "initial_fatfs_result": 0,
            "fatfs_result_valid": True,
            "fatfs_result": 0,
            "cleanup_fatfs_result_valid": False,
            "cleanup_fatfs_result": 0,
            "cleanup_complete": True,
            "poisoned": False,
            "mounted": True,
            "card_response_verified": True,
            "format_required": False,
            "authorization_consumed": False,
            "authorization_identity_matched": False,
            "format_attempted": False,
            "partition_written": False,
            "formatted": True,
            "mount_attempt_count": 1,
            "capacity_bytes": 8_589_934_592,
            "filesystem_total_bytes": 8_573_157_376,
            "free_bytes": 7_905_918_976,
            "logger_mirror_requested": True,
            "logger_mirror_enabled": True,
            "logger_mirror_errno": 0,
            "logger_mirror_changed_at_us": timestamp_us,
            "logger_mirror_failure_count": 0,
        },
        "diagnostics": {
            "sample_period": component(),
            "imu_smoothing": component(),
            "monitor": component(),
            "full_snapshot_period": component(),
            "full_snapshot_monitor": component(),
            "sample_period_ms": 300,
            "imu_smoothing_percent": 70,
            "full_snapshot_period_ms": 1000,
            "monitor_active": False,
            "imu_monitor_active": False,
            "microphone_monitor_active": False,
            "microphone_spectrum_monitor_active": False,
            "full_snapshot_monitor_active": True,
        },
        "ui": {
            "display": component(),
            "touch": component(),
            "brightness": component(),
            "display_power_mode": component(),
            "touch_activity_sequence": 0,
            "touch_last_activity_at_us": timestamp_us,
            "brightness_percent": 80,
            "configured_brightness_percent": 80,
            "display_power_mode_state": "active",
            "orientation": {
                **component(),
                "auto_rotation_enabled": True,
                "gravity_valid": True,
                "rotation_pending": False,
                "physical_orientation_degrees": 0,
                "rendering_rotation_degrees": 0,
                "change_sequence": 0,
                "sampled_at_us": timestamp_us,
                "changed_at_us": timestamp_us,
            },
        },
        "input": {
            "io_expander": component(),
            "boot_button": component(),
            "power_button": component(),
            "boot_pressed": False,
            "power_pressed": False,
            "boot_press_count": 0,
            "boot_long_press_count": 0,
            "power_short_press_count": 0,
            "power_long_press_count": 0,
            "wakeup_cause": 0,
        },
        "power": {
            **component(),
            "state": "active",
            "reason": "startup",
            "audio_suspended": False,
            "fullscreen_active": False,
            "command_arc_visible": True,
            "status_arc_visible": True,
            "display_idle_stage": "active",
            "command_arc_timeout_seconds": 10,
            "status_arc_timeout_seconds": 20,
            "display_dim_timeout_seconds": 30,
            "screen_sleep_delay_seconds": 60,
            "transition_sequence": 0,
            "last_activity_at_us": timestamp_us,
            "changed_at_us": timestamp_us,
        },
        "memory": {
            **component(),
            "flash_capacity_bytes": 16_777_216,
            "app_partition_bytes": 6_291_456,
            "firmware_image_bytes": 2_650_112,
            "heap_total_bytes": 8_650_752,
            "heap_free_bytes": 6_240_256,
            "heap_minimum_free_bytes": 5_910_528,
            "internal_heap_total_bytes": 327_680,
            "internal_free_bytes": 118_784,
            "internal_largest_block_bytes": 65_536,
            "internal_minimum_free_bytes": 96_512,
            "internal_allocated_blocks": 418,
            "internal_free_blocks": 57,
            "internal_total_blocks": 475,
            "internal_memory_pressure": False,
            "psram_capacity_bytes": 8_388_608,
            "psram_heap_total_bytes": 8_323_072,
            "psram_free_bytes": 6_121_472,
            "psram_used_bytes": 2_201_600,
            "psram_largest_block_bytes": 4_194_304,
            "psram_minimum_free_bytes": 5_890_048,
            "psram_allocated_blocks": 144,
            "psram_free_blocks": 31,
            "psram_total_blocks": 175,
            "allocation_failure_monitor_active": True,
            "allocation_failure_count": 0,
            "largest_failed_allocation_bytes": 0,
            "failed_allocation_caps_seen_mask": 0,
        },
        "processor": {
            **component(),
            "core_count": 2,
            "sampling_core": 0,
            "cores": [
                {
                    "available": True,
                    "utilization_available": False,
                    "utilization_percent": 0,
                    "current_task": "unavailable",
                    "stack_high_water_words": 0,
                    "priority": 0,
                },
                {
                    "available": True,
                    "utilization_available": False,
                    "utilization_percent": 0,
                    "current_task": "unavailable",
                    "stack_high_water_words": 0,
                    "priority": 0,
                },
            ],
        },
    }


COMPONENT_PATHS = (
    "battery",
    "imu",
    "imu.calibration",
    "sensor_fusion",
    "distance",
    "distance.sensors.0",
    "distance.sensors.1",
    "distance.settings",
    "distance.calibration",
    "rtc",
    "rtc.system_time",
    "gps",
    "audio.speaker",
    "audio.microphone",
    "audio.spectrum",
    "radios.wifi",
    "radios.ble",
    "storage",
    "storage.logger_mirror",
    "diagnostics.sample_period",
    "diagnostics.imu_smoothing",
    "diagnostics.monitor",
    "diagnostics.full_snapshot_period",
    "diagnostics.full_snapshot_monitor",
    "ui.display",
    "ui.touch",
    "ui.brightness",
    "ui.display_power_mode",
    "ui.orientation",
    "input.io_expander",
    "input.boot_button",
    "input.power_button",
    "power",
    "memory",
    "processor",
)

_COMPONENT_PATH_SET = frozenset(COMPONENT_PATHS)
_RESERVED_PATCH_PATHS = frozenset({"sequence", "last_update_attempt_at_us"})


def _parts(path: str) -> tuple[str, ...]:
    _require(isinstance(path, str) and path and not path.startswith("."), "patch path must be a non-empty dotted string")
    values = tuple(path.split("."))
    _require(all(values), f"patch path {path!r} contains an empty segment")
    return values


def _resolve(root: Any, path: str) -> Any:
    current = root
    for part in _parts(path):
        if isinstance(current, dict):
            if part not in current:
                raise ScenarioValidationError(f"unknown snapshot path {path!r}")
            current = current[part]
        elif isinstance(current, list):
            if not part.isdigit() or int(part) >= len(current):
                raise ScenarioValidationError(f"invalid list index in snapshot path {path!r}")
            current = current[int(part)]
        else:
            raise ScenarioValidationError(f"snapshot path {path!r} traverses a scalar")
    return current


def _set_path(root: Any, path: str, value: Any) -> None:
    parts = _parts(path)
    current = root
    for part in parts[:-1]:
        if isinstance(current, dict):
            if part not in current:
                raise ScenarioValidationError(f"unknown snapshot path {path!r}")
            current = current[part]
        elif isinstance(current, list):
            if not part.isdigit() or int(part) >= len(current):
                raise ScenarioValidationError(f"invalid list index in snapshot path {path!r}")
            current = current[int(part)]
        else:
            raise ScenarioValidationError(f"snapshot path {path!r} traverses a scalar")
    leaf = parts[-1]
    if isinstance(current, dict):
        if leaf not in current:
            raise ScenarioValidationError(f"unknown snapshot path {path!r}")
        old = current[leaf]
        _validate_patch_type(path, old, value)
        current[leaf] = copy.deepcopy(value)
        return
    if isinstance(current, list):
        if not leaf.isdigit() or int(leaf) >= len(current):
            raise ScenarioValidationError(f"invalid list index in snapshot path {path!r}")
        old = current[int(leaf)]
        _validate_patch_type(path, old, value)
        current[int(leaf)] = copy.deepcopy(value)
        return
    raise ScenarioValidationError(f"snapshot path {path!r} terminates below a scalar")


def _validate_patch_type(path: str, old: Any, new: Any) -> None:
    if isinstance(old, bool):
        ok = isinstance(new, bool)
    elif _is_int(old):
        ok = _is_int(new)
    elif isinstance(old, float):
        ok = (isinstance(new, (int, float)) and not isinstance(new, bool) and math.isfinite(float(new)))
    elif isinstance(old, str):
        ok = isinstance(new, str)
    elif isinstance(old, list):
        ok = isinstance(new, list)
    elif isinstance(old, dict):
        ok = isinstance(new, dict)
    else:
        ok = type(new) is type(old)
    _require(ok, f"patch {path!r} has {type(new).__name__}, expected {type(old).__name__}")


def _apply_flat_patch(state: dict[str, Any], patch: Mapping[str, Any]) -> None:
    _require(isinstance(patch, Mapping), "scenario patch must be an object")
    for path, value in patch.items():
        _require(path not in _RESERVED_PATCH_PATHS, f"scenario patch cannot set reserved field {path!r}")
        _set_path(state, path, value)


def _component_for_patch(path: str) -> str | None:
    parts = path.split(".")
    candidates = [candidate for candidate in COMPONENT_PATHS if path == candidate or path.startswith(f"{candidate}.")]
    if candidates:
        return max(candidates, key=len)
    if parts[0] == "audio":
        return "audio.spectrum" if "spectrum" in path else "audio.microphone"
    if parts[0] == "diagnostics":
        return "diagnostics.monitor"
    if parts[0] == "ui":
        if parts[1] in {"brightness_percent", "configured_brightness_percent"}:
            return "ui.brightness"
        if parts[1] == "display_power_mode_state":
            return "ui.display_power_mode"
        return "ui.touch"
    if parts[0] == "input":
        return "input.power_button" if "power" in parts[1] else "input.boot_button"
    return None


def _component_current(component: Mapping[str, Any]) -> bool:
    return (
        component["health"] == "ready"
        and component["last_error"] == "ESP_OK"
        and component["data_valid"] is True
        and component["data_stale"] is False
    )


def _touch_component(state: dict[str, Any], path: str, at_us: int) -> None:
    _require(path in _COMPONENT_PATH_SET, f"unknown component path {path!r}")
    component = _resolve(state, path)
    _require(isinstance(component, dict), f"component path {path!r} is not an object")
    component["last_attempt_at_us"] = at_us
    if _component_current(component):
        component["last_success_at_us"] = at_us


def _validate_shape(value: Any, template: Any, path: str = "snapshot") -> None:
    if isinstance(template, dict):
        _require(isinstance(value, dict), f"{path} must be an object")
        missing = sorted(set(template) - set(value))
        extra = sorted(set(value) - set(template))
        _require(not missing, f"{path} is missing fields: {', '.join(missing)}")
        _require(not extra, f"{path} has unknown fields: {', '.join(extra)}")
        for key, child in template.items():
            _validate_shape(value[key], child, f"{path}.{key}")
        return
    if isinstance(template, list):
        _require(isinstance(value, list), f"{path} must be an array")
        _require(len(value) == len(template), f"{path} must contain {len(template)} entries")
        for index, child in enumerate(template):
            _validate_shape(value[index], child, f"{path}.{index}")
        return
    _validate_patch_type(path, template, value)


def _validate_component(component: Mapping[str, Any], path: str, now_us: int) -> None:
    _require(component["health"] in HEALTH_VALUES, f"{path}.health has an unknown value")
    _require(component["last_error"] in ERROR_VALUES, f"{path}.last_error has an unknown value")
    for field in ("data_valid", "data_stale"):
        _require(isinstance(component[field], bool), f"{path}.{field} must be boolean")
    attempt = component["last_attempt_at_us"]
    success = component["last_success_at_us"]
    _require(_is_int(attempt) and 0 <= attempt <= now_us, f"{path}.last_attempt_at_us is outside the virtual clock")
    _require(_is_int(success) and 0 <= success <= now_us, f"{path}.last_success_at_us is outside the virtual clock")
    _require(attempt > 0 or success == 0, f"{path} cannot have success without an attempt")
    _require(not (component["data_valid"] and component["data_stale"]), f"{path} cannot be valid and stale")
    if component["data_valid"]:
        _require(success > 0, f"{path} valid data requires a successful sample timestamp")
        _require(component["last_error"] == "ESP_OK", f"{path} valid data cannot carry an error")
    if component["data_stale"]:
        _require(success > 0, f"{path} stale data requires a prior successful sample")
    if component["health"] == "ready":
        _require(component["last_error"] == "ESP_OK", f"{path} ready health cannot carry an error")
    if component["health"] == "missing":
        _require(not component["data_valid"], f"{path} missing hardware cannot publish valid data")
        _require(component["last_error"] != "ESP_OK", f"{path} missing hardware requires a real error")
    if _component_current(component):
        _require(success > 0, f"{path} current readiness requires a successful timestamp")


def _validate_battery(snapshot: Mapping[str, Any]) -> None:
    battery = snapshot["battery"]
    current = _component_current(battery)
    level_effective = current and battery["level_percent_valid"]
    mv_effective = current and battery["battery_mv_valid"]
    if level_effective:
        _require(battery["battery_present"], "battery level cannot be valid without a cell")
        _require(0 <= battery["level_percent"] <= 100, "battery level must be between 0 and 100")
    if mv_effective:
        _require(battery["battery_present"], "battery voltage cannot be valid without a cell")
        _require(battery["battery_mv"] > 0, "valid battery voltage must be positive")
    if current and not battery["battery_present"]:
        _require(not battery["level_percent_valid"] and not battery["battery_mv_valid"], "PMIC without a cell must invalidate cell-only fields")
    _require(battery["usb_mv"] >= 0 and battery["system_mv"] >= 0, "battery rails cannot be negative")
    if current and not battery["usb_present"]:
        _require(not battery["vbus_good"] and battery["usb_mv"] == 0, "absent USB must not report a valid VBUS rail")
    if battery["power_state"] == "charging":
        _require(current and battery["battery_present"] and battery["usb_present"] and battery["vbus_good"], "charging requires a current PMIC sample, cell, and good VBUS")
        _require(battery["charger_phase"] != "stopped", "charging requires an active charger phase")


def _validate_gps(snapshot: Mapping[str, Any]) -> None:
    gps = snapshot["gps"]
    if gps["has_fix"]:
        _require(_component_current(gps), "GPS fix requires a current receiver sample")
        _require(gps["utc_valid"], "GPS fix requires valid UTC")
        _require(gps["fix_quality"] > 0 and gps["satellites"] > 0, "GPS fix requires quality and satellites")
        _require(-90.0 <= gps["latitude_degrees"] <= 90.0, "GPS latitude is out of range")
        _require(-180.0 <= gps["longitude_degrees"] <= 180.0, "GPS longitude is out of range")
        _require(gps["hdop"] >= 0.0 and gps["speed_mps"] >= 0.0, "GPS HDOP and speed cannot be negative")
        _require(0.0 <= gps["course_degrees"] <= 360.0, "GPS course is out of range")
        _require(bool(gps["utc_time"] and gps["utc_date"]), "GPS fix requires UTC text")
    else:
        _require(gps["fix_quality"] == 0 and gps["satellites"] >= 0, "GPS without a fix cannot report fix quality")


def _validate_wifi(snapshot: Mapping[str, Any]) -> None:
    wifi = snapshot["radios"]["wifi"]
    if wifi["ip_acquired"]:
        _require(wifi["connected"], "Wi-Fi cannot own an IP without a station connection")
    if wifi["connected"]:
        _require(wifi["started"] and wifi["connect_requested"] and wifi["credentials_configured"], "Wi-Fi connection requires a started, requested station with credentials")
        _require(_component_current(wifi), "connected Wi-Fi requires current radio state")
        _require(bool(wifi["ssid"] and wifi["mac_address"]), "connected Wi-Fi requires SSID and MAC identity")
    if wifi["ip_acquired"]:
        _require(bool(wifi["ip_address"]), "acquired Wi-Fi lease requires an IP address")
        _require(wifi["channel"] > 0 and -127 <= wifi["rssi"] <= 0, "connected Wi-Fi channel or RSSI is invalid")
    if not wifi["started"]:
        _require(not wifi["connect_requested"] and not wifi["connected"] and not wifi["ip_acquired"] and not wifi["reconnect_pending"], "stopped Wi-Fi cannot retain active lifecycle flags")
        _require(wifi["channel"] == 0 and wifi["rssi"] == 0 and not wifi["ip_address"], "stopped Wi-Fi must not expose live link data")


def _storage_format_required(storage: Mapping[str, Any]) -> bool:
    return (
        storage["format_required"]
        and storage["mount_outcome"] == "format-required"
        and storage["card_response_verified"]
        and storage["cleanup_complete"]
        and storage["cleanup_error"] == "ESP_OK"
        and not storage["poisoned"]
        and not storage["mounted"]
    )


def _validate_storage(snapshot: Mapping[str, Any]) -> None:
    storage = snapshot["storage"]
    _require(storage["mount_outcome"] in STORAGE_OUTCOMES, "storage mount outcome is unknown")
    _require(storage["mount_stage"] in STORAGE_STAGES, "storage mount stage is unknown")
    _require(storage["cleanup_stage"] in STORAGE_CLEANUP_STAGES, "storage cleanup stage is unknown")
    _require(storage["cleanup_error"] in ERROR_VALUES, "storage cleanup error is unknown")
    if storage["mounted"]:
        _require(_component_current(storage), "mounted storage requires current ready state")
        _require(storage["mount_outcome"] == "mounted" and storage["card_response_verified"], "mounted storage requires a verified card and mounted outcome")
        _require(not storage["format_required"] and storage["cleanup_complete"] and not storage["poisoned"] and storage["cleanup_error"] == "ESP_OK", "mounted storage has an incomplete or poisoned lifecycle")
        _require(0 < storage["filesystem_total_bytes"] <= storage["capacity_bytes"], "mounted storage capacity is incoherent")
        _require(0 <= storage["free_bytes"] <= storage["filesystem_total_bytes"], "mounted storage free space is incoherent")
    if storage["format_required"]:
        _require(_storage_format_required(storage), "format-required storage does not satisfy the guarded predicate")
    if storage["formatted"]:
        _require(storage["card_response_verified"], "formatted state requires a verified card")
    if storage["format_attempted"]:
        _require(storage["authorization_consumed"] and storage["authorization_identity_matched"], "format attempt requires consumed matching authorization")
    if storage["partition_written"]:
        _require(storage["format_attempted"], "partition write requires a format attempt")
    if storage["logger_mirror_enabled"]:
        _require(storage["mounted"] and _component_current(storage["logger_mirror"]), "logger mirror requires mounted storage and current mirror state")


def _validate_distance(snapshot: Mapping[str, Any]) -> None:
    distance = snapshot["distance"]
    settings = distance["settings"]
    calibration = distance["calibration"]
    _require(20 <= settings["sample_count"] <= 500, "TOF sample_count is outside the firmware contract")
    _require(500 <= settings["reference_distance_mm"] <= 1500, "TOF reference distance is outside the firmware contract")
    _require(settings["sample_interval_ms"] > 0, "TOF sample interval must be positive")
    _require(calibration["state"] in CALIBRATION_STATES, "TOF calibration state is unknown")
    _require(0 <= calibration["progress_percent"] <= 100, "TOF calibration progress is out of range")
    for index, sensor in enumerate(distance["sensors"]):
        if _component_current(sensor):
            _require(sensor["raw_distance_mm"] > 0 and sensor["filtered_distance_mm"] > 0, f"TOF {index + 1} current data requires positive distances")
            _require(0 < sensor["inlier_count"] <= sensor["window_sample_count"], f"TOF {index + 1} inlier window is incoherent")
            _require(sensor["frame_sequence"] > 0, f"TOF {index + 1} current data requires a frame sequence")
    if _component_current(distance):
        _require(all(_component_current(sensor) for sensor in distance["sensors"]), "distance result requires two current TOF sensors")
        filtered = [sensor["filtered_distance_mm"] for sensor in distance["sensors"]]
        _require(distance["sensor_spread_mm"] == abs(filtered[0] - filtered[1]), "distance sensor spread does not match the TOF pair")
        _require(0 < distance["vertical_distance_mm"] <= distance["slant_distance_mm"], "distance geometry is incoherent")
        _require(0 < distance["samples_used"] <= min(sensor["inlier_count"] for sensor in distance["sensors"]), "distance sample count exceeds TOF inliers")
        _require(0 <= distance["confidence_percent"] <= 100, "distance confidence is out of range")
        _require(distance["acquisition_timing_valid"], "current distance requires valid acquisition timing")


def _validate_memory(snapshot: Mapping[str, Any]) -> None:
    memory = snapshot["memory"]
    pairs = (
        ("heap_free_bytes", "heap_total_bytes"),
        ("internal_free_bytes", "internal_heap_total_bytes"),
        ("internal_largest_block_bytes", "internal_free_bytes"),
        ("psram_free_bytes", "psram_heap_total_bytes"),
        ("psram_largest_block_bytes", "psram_free_bytes"),
    )
    for value_name, limit_name in pairs:
        _require(0 <= memory[value_name] <= memory[limit_name], f"memory {value_name} exceeds {limit_name}")
    _require(memory["psram_used_bytes"] == memory["psram_heap_total_bytes"] - memory["psram_free_bytes"], "PSRAM used/free/total values disagree")
    _require(memory["internal_total_blocks"] == memory["internal_allocated_blocks"] + memory["internal_free_blocks"], "internal allocator block counts disagree")
    _require(memory["psram_total_blocks"] == memory["psram_allocated_blocks"] + memory["psram_free_blocks"], "PSRAM allocator block counts disagree")
    pressure = memory["internal_free_bytes"] < 32_768 or memory["internal_largest_block_bytes"] < 16_384
    _require(memory["internal_memory_pressure"] is pressure, "internal memory pressure flag disagrees with raw allocator thresholds")
    if memory["allocation_failure_count"] == 0:
        _require(memory["largest_failed_allocation_bytes"] == 0 and memory["failed_allocation_caps_seen_mask"] == 0, "zero allocation failures cannot retain failure aggregates")
    else:
        _require(memory["allocation_failure_monitor_active"], "allocation failures require an active boot monitor")
        _require(memory["largest_failed_allocation_bytes"] > 0 and memory["failed_allocation_caps_seen_mask"] > 0, "allocation failure aggregates are incomplete")


def _validate_power(snapshot: Mapping[str, Any]) -> None:
    power = snapshot["power"]
    ui = snapshot["ui"]
    _require(power["state"] in POWER_STATES, "power operating state is unknown")
    _require(power["reason"] in POWER_REASONS, "power transition reason is unknown")
    _require(power["display_idle_stage"] in DISPLAY_MODES, "power display idle stage is unknown")
    _require(ui["display_power_mode_state"] in DISPLAY_MODES, "UI display power mode is unknown")
    _require(power["display_idle_stage"] == ui["display_power_mode_state"], "power and UI display owners disagree")
    configured = ui["configured_brightness_percent"]
    applied = ui["brightness_percent"]
    _require(1 <= configured <= 100 and 0 <= applied <= 100, "display brightness is out of range")
    if power["display_idle_stage"] == "active":
        _require(applied == configured, "active display must apply configured brightness")
    elif power["display_idle_stage"] == "dimmed":
        _require(0 < applied < configured, "dimmed display requires a reduced non-zero brightness")
    elif power["display_idle_stage"] == "display-off":
        _require(applied == 0, "display-off stage requires zero applied brightness")
    if power["state"] == "active":
        _require(not power["audio_suspended"], "active device cannot claim suspended audio")
        if power["display_idle_stage"] == "display-off":
            _require(not power["command_arc_visible"] and not power["status_arc_visible"], "display-off cannot retain visible chrome")
    if power["state"] == "standby":
        _require(power["display_idle_stage"] == "display-off" and applied == 0, "standby requires a dark display")
        _require(power["audio_suspended"] and not power["fullscreen_active"], "standby requires suspended audio and no fullscreen lock")
        _require(not power["command_arc_visible"] and not power["status_arc_visible"], "standby cannot retain visible chrome")


def _validate_processor(snapshot: Mapping[str, Any]) -> None:
    processor = snapshot["processor"]
    _require(processor["core_count"] == 2 and processor["sampling_core"] in {0, 1}, "processor topology must match ESP32-S3")
    for index, core in enumerate(processor["cores"]):
        _require(isinstance(core["utilization_available"], bool), f"processor core {index} utilization availability must be boolean")
        if core["utilization_available"]:
            _require(core["available"] and 0 <= core["utilization_percent"] <= 100, f"processor core {index} utilization is invalid")
        else:
            _require(core["utilization_percent"] == 0, f"processor core {index} unavailable utilization must not contain an estimate")


def validate_snapshot(snapshot: Mapping[str, Any], *, now_us: int) -> None:
    """Validate the complete raw snapshot and all cross-component invariants."""

    _require(_is_int(now_us) and now_us >= 0, "now_us must be a non-negative integer")
    _validate_shape(snapshot, _base_snapshot(max(1, now_us)))
    _require(_is_int(snapshot["sequence"]) and snapshot["sequence"] > 0, "snapshot sequence must be positive")
    attempted = snapshot["last_update_attempt_at_us"]
    _require(_is_int(attempted) and 0 < attempted <= now_us, "snapshot attempt timestamp is outside the virtual clock")
    _require(snapshot["last_update_error"] in ERROR_VALUES, "snapshot update error is unknown")
    _require(isinstance(snapshot["last_update_complete"], bool), "snapshot completion flag must be boolean")
    _require(snapshot["last_update_complete"] == (snapshot["last_update_error"] == "ESP_OK"), "snapshot completion and update error disagree")
    _require(isinstance(snapshot["reason"], str) and 0 < len(snapshot["reason"]) <= 31, "snapshot reason must fit the firmware field")
    for path in COMPONENT_PATHS:
        _validate_component(_resolve(snapshot, path), path, now_us)
    _validate_battery(snapshot)
    _validate_gps(snapshot)
    _validate_wifi(snapshot)
    _validate_storage(snapshot)
    _validate_distance(snapshot)
    _validate_memory(snapshot)
    _validate_power(snapshot)
    _validate_processor(snapshot)


def _freeze(value: Any) -> Any:
    if isinstance(value, dict):
        return MappingProxyType({key: _freeze(child) for key, child in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(child) for child in value)
    return value


def _thaw(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {key: _thaw(child) for key, child in value.items()}
    if isinstance(value, tuple):
        return [_thaw(child) for child in value]
    return copy.deepcopy(value)


@dataclass(frozen=True)
class TimelineEvent:
    at_ms: int
    reason: str
    patch: Mapping[str, Any]
    touch_components: tuple[str, ...] = ()


@dataclass(frozen=True)
class ScenarioDefinition:
    name: str
    title: str
    description: str
    source: Path
    initial_time_us: int
    initial_patch: Mapping[str, Any]
    timeline: tuple[TimelineEvent, ...]


@dataclass(frozen=True)
class SnapshotGeneration:
    sequence: int
    at_us: int
    snapshot: Mapping[str, Any]
    simulator: Mapping[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {"simulator": _thaw(self.simulator), "snapshot": _thaw(self.snapshot)}


class VirtualClock:
    """A monotonic clock advanced only by explicit simulator operations."""

    def __init__(self, now_us: int = 0) -> None:
        _require(_is_int(now_us) and now_us >= 0, "virtual clock origin must be a non-negative integer")
        self._now_us = now_us

    @property
    def now_us(self) -> int:
        return self._now_us

    def advance_us(self, delta_us: int) -> int:
        _require(_is_int(delta_us) and delta_us >= 0, "virtual clock delta must be non-negative")
        self._now_us += delta_us
        return self._now_us

    def advance_ms(self, delta_ms: int) -> int:
        _require(_is_int(delta_ms) and delta_ms >= 0, "virtual clock delta must be non-negative")
        return self.advance_us(delta_ms * 1000)

    def advance_to_us(self, target_us: int) -> int:
        _require(_is_int(target_us) and target_us >= self._now_us, "virtual clock cannot move backwards")
        self._now_us = target_us
        return self._now_us

    def advance_to_ms(self, target_ms: int) -> int:
        _require(_is_int(target_ms) and target_ms >= 0, "virtual clock target must be non-negative")
        return self.advance_to_us(target_ms * 1000)


def _reject_duplicate_keys(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ScenarioValidationError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _scenario_directory() -> Path:
    return Path(__file__).resolve().with_name("scenarios")


def _built_in_paths() -> dict[str, Path]:
    directory = _scenario_directory()
    paths: dict[str, Path] = {}
    if not directory.is_dir():
        raise UnknownScenarioError(f"built-in scenario directory is missing: {directory}")
    for path in sorted(directory.glob("*.json")):
        if path.stem in paths:
            raise ScenarioValidationError(f"duplicate built-in scenario stem {path.stem!r}")
        paths[path.stem] = path
    return paths


def scenario_names() -> tuple[str, ...]:
    """Return validated built-in scenario names in deterministic order."""

    definitions = [load_scenario(path) for path in _built_in_paths().values()]
    names = tuple(sorted(definition.name for definition in definitions))
    _require(len(names) == len(set(names)), "built-in scenario names are not unique")
    return names


def _parse_patch(value: Any, context: str) -> Mapping[str, Any]:
    _require(isinstance(value, dict), f"{context} must be an object of dotted snapshot paths")
    for path in value:
        _parts(path)
    return _freeze(value)


def load_scenario(name_or_path: str | Path) -> ScenarioDefinition:
    """Load a built-in name or an explicit v1 JSON fixture."""

    candidate = Path(name_or_path)
    built_in = False
    if candidate.is_file():
        path = candidate.resolve()
    else:
        name = str(name_or_path)
        paths = _built_in_paths()
        if name not in paths:
            raise UnknownScenarioError(f"unknown scenario {name!r}; available: {', '.join(sorted(paths))}")
        path = paths[name]
        built_in = True
    try:
        raw = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioValidationError(f"cannot load scenario {path}: {error}") from error
    _require(isinstance(raw, dict), f"scenario {path} must contain one JSON object")
    expected = {"schema", "name", "title", "description", "initial_time_us", "initial", "timeline"}
    _require(set(raw) == expected, f"scenario {path.name} fields must be exactly: {', '.join(sorted(expected))}")
    _require(raw["schema"] == SCENARIO_SCHEMA, f"scenario {path.name} uses unsupported schema {raw['schema']!r}")
    _require(isinstance(raw["name"], str) and raw["name"], f"scenario {path.name} requires a name")
    _require(isinstance(raw["title"], str) and raw["title"], f"scenario {path.name} requires a title")
    _require(isinstance(raw["description"], str) and raw["description"], f"scenario {path.name} requires a description")
    _require(_is_int(raw["initial_time_us"]) and raw["initial_time_us"] > 0, f"scenario {path.name} initial_time_us must be positive")
    if built_in:
        _require(path.stem == raw["name"], f"built-in scenario file {path.name} must match its declared name")
    initial_patch = _parse_patch(raw["initial"], f"scenario {raw['name']} initial patch")
    _require(isinstance(raw["timeline"], list), f"scenario {raw['name']} timeline must be an array")
    events: list[TimelineEvent] = []
    previous_at_ms = -1
    for index, item in enumerate(raw["timeline"]):
        context = f"scenario {raw['name']} timeline[{index}]"
        _require(isinstance(item, dict), f"{context} must be an object")
        expected_event = {"at_ms", "reason", "patch", "touch_components"}
        _require(set(item) == expected_event, f"{context} fields must be exactly: {', '.join(sorted(expected_event))}")
        _require(_is_int(item["at_ms"]) and item["at_ms"] > previous_at_ms and item["at_ms"] > 0, f"{context}.at_ms must be positive and strictly increasing")
        _require(isinstance(item["reason"], str) and 0 < len(item["reason"]) <= 31, f"{context}.reason must fit the firmware field")
        _require(isinstance(item["touch_components"], list), f"{context}.touch_components must be an array")
        touches = tuple(item["touch_components"])
        _require(all(isinstance(value, str) and value in _COMPONENT_PATH_SET for value in touches), f"{context} contains an unknown component path")
        _require(len(touches) == len(set(touches)), f"{context} repeats a component path")
        events.append(TimelineEvent(item["at_ms"], item["reason"], _parse_patch(item["patch"], f"{context}.patch"), touches))
        previous_at_ms = item["at_ms"]
    definition = ScenarioDefinition(
        raw["name"],
        raw["title"],
        raw["description"],
        path,
        raw["initial_time_us"],
        initial_patch,
        tuple(events),
    )
    _validate_definition(definition)
    return definition


def _apply_event_state(state: dict[str, Any], event: TimelineEvent, at_us: int) -> None:
    _apply_flat_patch(state, event.patch)
    state["sequence"] += 1
    state["last_update_attempt_at_us"] = at_us
    state["reason"] = event.reason
    inferred = {component for path in event.patch for component in [_component_for_patch(path)] if component}
    for path in sorted(inferred | set(event.touch_components)):
        _touch_component(state, path, at_us)


def _validate_definition(definition: ScenarioDefinition) -> None:
    state = _base_snapshot(definition.initial_time_us)
    _apply_flat_patch(state, definition.initial_patch)
    validate_snapshot(state, now_us=definition.initial_time_us)
    for event in definition.timeline:
        at_us = definition.initial_time_us + event.at_ms * 1000
        _apply_event_state(state, event, at_us)
        validate_snapshot(state, now_us=at_us)


class ScenarioEngine:
    """Own a deterministic scenario timeline and immutable snapshot generations."""

    def __init__(self, scenario: ScenarioDefinition, clock: VirtualClock | None = None) -> None:
        self.scenario = scenario
        self.clock = clock or VirtualClock(scenario.initial_time_us)
        _require(self.clock.now_us == scenario.initial_time_us, "scenario clock must begin at initial_time_us")
        self._state = _base_snapshot(scenario.initial_time_us)
        _apply_flat_patch(self._state, scenario.initial_patch)
        validate_snapshot(self._state, now_us=self.clock.now_us)
        self._event_index = 0
        self._generation_count = 0
        self._generation = self._publish()

    @classmethod
    def from_scenario(cls, name_or_path: str | Path, *, start_at_ms: int = 0) -> "ScenarioEngine":
        _require(_is_int(start_at_ms) and start_at_ms >= 0, "start_at_ms must be non-negative")
        engine = cls(load_scenario(name_or_path))
        if start_at_ms:
            engine.advance_to_ms(start_at_ms)
        return engine

    @property
    def generation(self) -> SnapshotGeneration:
        return self._generation

    @property
    def state(self) -> SnapshotGeneration:
        return self._generation

    @property
    def elapsed_ms(self) -> int:
        return (self.clock.now_us - self.scenario.initial_time_us) // 1000

    def _publish(self) -> SnapshotGeneration:
        validate_snapshot(self._state, now_us=self.clock.now_us)
        self._generation_count += 1
        simulator = {
            "synthetic": True,
            "model_version": SIMULATOR_MODEL_VERSION,
            "scenario": self.scenario.name,
            "title": self.scenario.title,
            "clock_us": self.clock.now_us,
            "elapsed_ms": self.elapsed_ms,
            "generation": self._generation_count,
        }
        return SnapshotGeneration(
            sequence=self._state["sequence"],
            at_us=self.clock.now_us,
            snapshot=_freeze(copy.deepcopy(self._state)),
            simulator=_freeze(simulator),
        )

    def snapshot(self) -> dict[str, Any]:
        return _thaw(self._generation.snapshot)

    def advance_ms(self, delta_ms: int) -> SnapshotGeneration:
        _require(_is_int(delta_ms) and delta_ms >= 0, "advance delta must be non-negative")
        return self.advance_to_ms(self.elapsed_ms + delta_ms)

    def advance_to_ms(self, target_ms: int) -> SnapshotGeneration:
        _require(_is_int(target_ms) and target_ms >= self.elapsed_ms, "scenario timeline cannot move backwards")
        target_us = self.scenario.initial_time_us + target_ms * 1000
        published = False
        while self._event_index < len(self.scenario.timeline):
            event = self.scenario.timeline[self._event_index]
            event_us = self.scenario.initial_time_us + event.at_ms * 1000
            if event_us > target_us:
                break
            self.clock.advance_to_us(event_us)
            _apply_event_state(self._state, event, event_us)
            validate_snapshot(self._state, now_us=event_us)
            self._event_index += 1
            self._generation = self._publish()
            published = True
        self.clock.advance_to_us(target_us)
        if not published or self._generation.at_us != target_us:
            self._generation = self._publish()
        return self._generation

    def _commit_action(
        self,
        patch: Mapping[str, Any],
        *,
        reason: str,
        touch_components: Iterable[str] = (),
    ) -> SnapshotGeneration:
        event = TimelineEvent(self.elapsed_ms, reason, _freeze(dict(patch)), tuple(touch_components))
        trial = copy.deepcopy(self._state)
        _apply_event_state(trial, event, self.clock.now_us)
        validate_snapshot(trial, now_us=self.clock.now_us)
        self._state = trial
        self._generation = self._publish()
        return self._generation

    def action(self, name: str, **params: Any) -> SnapshotGeneration:
        """Apply a semantic user/device action with strict preconditions."""

        actions = {
            "press:pwr": self._action_power,
            "press:boot": self._action_boot,
            "touch": self._action_touch,
            "confirm:format-sd": self._action_format_sd,
            "remove:sd": self._action_remove_sd,
            "insert:sd": self._action_insert_sd,
            "start:wireless": self._action_start_wireless,
            "stop:wireless": self._action_stop_wireless,
            "toggle:wireless": self._action_toggle_wireless,
            "calibrate:tof": self._action_calibrate_tof,
        }
        if name not in actions:
            raise SemanticActionError(f"unknown semantic action {name!r}; available: {', '.join(sorted(actions))}")
        return actions[name](params)

    @staticmethod
    def _no_params(name: str, params: Mapping[str, Any]) -> None:
        if params:
            raise SemanticActionError(f"{name} does not accept parameters: {', '.join(sorted(params))}")

    def _action_power(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("press:pwr", params)
        state = self._state["power"]["state"]
        if state not in {"active", "standby"}:
            raise SemanticActionError(f"press:pwr rejected during {state}")
        wake = state == "standby"
        count = self._state["input"]["power_short_press_count"] + 1
        transition = self._state["power"]["transition_sequence"] + 1
        patch: dict[str, Any] = {
            "input.power_short_press_count": count,
            "power.state": "active" if wake else "standby",
            "power.reason": "power-button",
            "power.audio_suspended": not wake,
            "power.fullscreen_active": False,
            "power.command_arc_visible": wake,
            "power.status_arc_visible": wake,
            "power.display_idle_stage": "active" if wake else "display-off",
            "power.transition_sequence": transition,
            "power.last_activity_at_us": self.clock.now_us,
            "power.changed_at_us": self.clock.now_us,
            "ui.display_power_mode_state": "active" if wake else "display-off",
            "ui.brightness_percent": self._state["ui"]["configured_brightness_percent"] if wake else 0,
        }
        return self._commit_action(patch, reason="synthetic-pwr", touch_components=("input.power_button", "power", "ui.display_power_mode", "ui.brightness"))

    def _action_boot(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("press:boot", params)
        if self._state["power"]["state"] == "standby":
            return self._generation
        if self._state["power"]["state"] != "active":
            raise SemanticActionError(f"press:boot rejected during {self._state['power']['state']}")
        fullscreen = not self._state["power"]["fullscreen_active"]
        patch = {
            "input.boot_press_count": self._state["input"]["boot_press_count"] + 1,
            "power.reason": "boot-button",
            "power.fullscreen_active": fullscreen,
            "power.command_arc_visible": not fullscreen,
            "power.status_arc_visible": not fullscreen,
            "power.last_activity_at_us": self.clock.now_us,
        }
        return self._commit_action(patch, reason="synthetic-boot", touch_components=("input.boot_button", "power"))

    def _action_touch(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("touch", params)
        if self._state["power"]["state"] == "standby":
            return self._generation
        if self._state["power"]["state"] != "active":
            raise SemanticActionError(f"touch rejected during {self._state['power']['state']}")
        fullscreen = self._state["power"]["fullscreen_active"]
        patch = {
            "ui.touch_activity_sequence": self._state["ui"]["touch_activity_sequence"] + 1,
            "ui.touch_last_activity_at_us": self.clock.now_us,
            "ui.display_power_mode_state": "active",
            "ui.brightness_percent": self._state["ui"]["configured_brightness_percent"],
            "power.reason": "touch",
            "power.display_idle_stage": "active",
            "power.command_arc_visible": not fullscreen,
            "power.status_arc_visible": not fullscreen,
            "power.last_activity_at_us": self.clock.now_us,
        }
        return self._commit_action(patch, reason="synthetic-touch", touch_components=("ui.touch", "ui.display_power_mode", "ui.brightness", "power"))

    def _action_format_sd(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("confirm:format-sd", params)
        storage = self._state["storage"]
        if not _storage_format_required(storage):
            raise SemanticActionError("confirm:format-sd requires a present, verified format-required card")
        if storage["authorization_consumed"]:
            raise SemanticActionError("confirm:format-sd authorization was already consumed")
        patch = {
            "storage.health": "ready",
            "storage.last_error": "ESP_OK",
            "storage.data_valid": True,
            "storage.data_stale": False,
            "storage.mount_outcome": "mounted",
            "storage.mount_stage": "post-format-mount",
            "storage.cleanup_stage": "none",
            "storage.mounted": True,
            "storage.format_required": False,
            "storage.authorization_consumed": True,
            "storage.authorization_identity_matched": True,
            "storage.format_attempted": True,
            "storage.partition_written": True,
            "storage.formatted": True,
            "storage.mount_attempt_count": storage["mount_attempt_count"] + 1,
            "storage.capacity_bytes": 8_589_934_592,
            "storage.filesystem_total_bytes": 8_573_157_376,
            "storage.free_bytes": 8_560_574_464,
            "storage.logger_mirror.health": "ready",
            "storage.logger_mirror.last_error": "ESP_OK",
            "storage.logger_mirror.data_valid": True,
            "storage.logger_mirror.data_stale": False,
            "storage.logger_mirror_enabled": True,
            "storage.logger_mirror_errno": 0,
            "storage.logger_mirror_changed_at_us": self.clock.now_us,
        }
        return self._commit_action(patch, reason="synthetic-sd-format", touch_components=("storage", "storage.logger_mirror"))

    def _action_remove_sd(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("remove:sd", params)
        storage = self._state["storage"]
        if not storage["card_response_verified"]:
            raise SemanticActionError("remove:sd requires a present card")
        stale = storage["last_success_at_us"] > 0
        mirror_stale = storage["logger_mirror"]["last_success_at_us"] > 0
        patch = {
            "storage.health": "missing",
            "storage.last_error": "ESP_ERR_NOT_FOUND",
            "storage.data_valid": False,
            "storage.data_stale": stale,
            "storage.mount_outcome": "card-or-transport-failed",
            "storage.mount_stage": "runtime-status",
            "storage.mounted": False,
            "storage.card_response_verified": False,
            "storage.format_required": False,
            "storage.formatted": False,
            "storage.capacity_bytes": 0,
            "storage.filesystem_total_bytes": 0,
            "storage.free_bytes": 0,
            "storage.logger_mirror.health": "degraded",
            "storage.logger_mirror.last_error": "ESP_ERR_NOT_FOUND",
            "storage.logger_mirror.data_valid": False,
            "storage.logger_mirror.data_stale": mirror_stale,
            "storage.logger_mirror_enabled": False,
            "storage.logger_mirror_errno": 19,
            "storage.logger_mirror_changed_at_us": self.clock.now_us,
            "storage.logger_mirror_failure_count": storage["logger_mirror_failure_count"] + 1,
        }
        return self._commit_action(patch, reason="synthetic-sd-removed", touch_components=("storage", "storage.logger_mirror"))

    def _action_insert_sd(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("insert:sd", params)
        storage = self._state["storage"]
        if storage["card_response_verified"]:
            raise SemanticActionError("insert:sd requires an absent card")
        patch = {
            "storage.health": "degraded",
            "storage.last_error": "ESP_ERR_INVALID_RESPONSE",
            "storage.data_valid": False,
            "storage.data_stale": False,
            "storage.last_success_at_us": 0,
            "storage.mount_outcome": "format-required",
            "storage.mount_stage": "fat-mount",
            "storage.mounted": False,
            "storage.card_response_verified": True,
            "storage.format_required": True,
            "storage.authorization_consumed": False,
            "storage.authorization_identity_matched": False,
            "storage.format_attempted": False,
            "storage.partition_written": False,
            "storage.formatted": False,
            "storage.mount_attempt_count": storage["mount_attempt_count"] + 1,
            "storage.logger_mirror.health": "degraded",
            "storage.logger_mirror.last_error": "ESP_ERR_INVALID_STATE",
            "storage.logger_mirror.data_valid": False,
            "storage.logger_mirror.data_stale": False,
            "storage.logger_mirror.last_success_at_us": 0,
            "storage.logger_mirror_enabled": False,
        }
        return self._commit_action(patch, reason="synthetic-sd-insert", touch_components=("storage", "storage.logger_mirror"))

    def _action_start_wireless(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        allowed = {"connect"}
        if set(params) - allowed:
            raise SemanticActionError(f"start:wireless accepts only connect, got: {', '.join(sorted(set(params) - allowed))}")
        connect = params.get("connect", True)
        if not isinstance(connect, bool):
            raise SemanticActionError("start:wireless connect must be boolean")
        wifi = self._state["radios"]["wifi"]
        ble = self._state["radios"]["ble"]
        if wifi["started"] or _component_current(ble):
            raise SemanticActionError("start:wireless requires both radios to be stopped")
        if connect and not wifi["credentials_configured"]:
            raise SemanticActionError("start:wireless cannot connect without configured credentials")
        patch = {
            "radios.wifi.health": "ready",
            "radios.wifi.last_error": "ESP_OK",
            "radios.wifi.data_valid": True,
            "radios.wifi.data_stale": False,
            "radios.wifi.started": True,
            "radios.wifi.connect_requested": connect,
            "radios.wifi.connected": connect,
            "radios.wifi.ip_acquired": connect,
            "radios.wifi.reconnect_pending": False,
            "radios.wifi.rssi": -47 if connect else 0,
            "radios.wifi.channel": 6 if connect else 0,
            "radios.wifi.event_sequence": wifi["event_sequence"] + 1,
            "radios.wifi.connect_attempt_count": wifi["connect_attempt_count"] + (1 if connect else 0),
            "radios.wifi.ssid": "LUNASHELL-SYNTHETIC" if connect else "",
            "radios.wifi.ip_address": "192.0.2.42" if connect else "",
            "radios.wifi.mac_address": "02:00:00:00:00:42" if connect else "02:00:00:00:00:42",
            "radios.ble.health": "ready",
            "radios.ble.last_error": "ESP_OK",
            "radios.ble.data_valid": True,
            "radios.ble.data_stale": False,
            "radios.ble.provisioning": False,
            "radios.ble.advertising": False,
            "radios.ble.connected": False,
        }
        return self._commit_action(patch, reason="synthetic-radio-start", touch_components=("radios.wifi", "radios.ble"))

    def _action_stop_wireless(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("stop:wireless", params)
        wifi = self._state["radios"]["wifi"]
        ble = self._state["radios"]["ble"]
        if not wifi["started"] and not _component_current(ble):
            raise SemanticActionError("stop:wireless requires a running radio subsystem")
        patch = {
            "radios.wifi.health": "unknown",
            "radios.wifi.last_error": "ESP_ERR_INVALID_STATE",
            "radios.wifi.data_valid": False,
            "radios.wifi.data_stale": wifi["last_success_at_us"] > 0,
            "radios.wifi.started": False,
            "radios.wifi.connect_requested": False,
            "radios.wifi.connected": False,
            "radios.wifi.ip_acquired": False,
            "radios.wifi.reconnect_pending": False,
            "radios.wifi.rssi": 0,
            "radios.wifi.channel": 0,
            "radios.wifi.event_sequence": wifi["event_sequence"] + 1,
            "radios.wifi.ssid": "",
            "radios.wifi.ip_address": "",
            "radios.wifi.mac_address": "",
            "radios.ble.health": "unknown",
            "radios.ble.last_error": "ESP_ERR_INVALID_STATE",
            "radios.ble.data_valid": False,
            "radios.ble.data_stale": ble["last_success_at_us"] > 0,
            "radios.ble.provisioning": False,
            "radios.ble.advertising": False,
            "radios.ble.connected": False,
            "radios.ble.connection_handle": 65535,
        }
        return self._commit_action(patch, reason="synthetic-radio-stop", touch_components=("radios.wifi", "radios.ble"))

    def _action_toggle_wireless(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        if self._state["radios"]["wifi"]["started"]:
            return self._action_stop_wireless(params)
        return self._action_start_wireless(params)

    def _action_calibrate_tof(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        allowed = {"succeed"}
        if set(params) - allowed:
            raise SemanticActionError(f"calibrate:tof accepts only succeed, got: {', '.join(sorted(set(params) - allowed))}")
        succeed = params.get("succeed", True)
        if not isinstance(succeed, bool):
            raise SemanticActionError("calibrate:tof succeed must be boolean")
        distance = self._state["distance"]
        if not all(_component_current(sensor) for sensor in distance["sensors"]):
            raise SemanticActionError("calibrate:tof requires two current TOF sensors")
        if distance["calibration"]["state"] == "running":
            raise SemanticActionError("calibrate:tof cannot start while calibration is running")
        sample_count = distance["settings"]["sample_count"]
        patch = {
            "distance.calibration.health": "ready" if succeed else "degraded",
            "distance.calibration.last_error": "ESP_OK" if succeed else "ESP_FAIL",
            "distance.calibration.data_valid": succeed,
            "distance.calibration.data_stale": False,
            "distance.calibration.state": "succeeded" if succeed else "failed",
            "distance.calibration.record_valid": succeed,
            "distance.calibration.requested_samples": sample_count,
            "distance.calibration.accepted_samples.0": sample_count if succeed else 0,
            "distance.calibration.accepted_samples.1": sample_count if succeed else 0,
            "distance.calibration.rejected_samples.0": 0 if succeed else sample_count,
            "distance.calibration.rejected_samples.1": 0 if succeed else sample_count,
            "distance.calibration.reference_distance_mm": distance["settings"]["reference_distance_mm"],
            "distance.calibration.progress_percent": 100,
            "distance.calibration.started_at_us": self.clock.now_us,
            "distance.calibration.completed_at_us": self.clock.now_us,
        }
        return self._commit_action(patch, reason="synthetic-tof-cal", touch_components=("distance.calibration",))

    def _simulator_metadata(self) -> dict[str, Any]:
        return _thaw(self._generation.simulator)

    def api_payload(self, path: str) -> dict[str, Any]:
        """Adapt the active generation to one documented read-only API v1 route."""

        builders = {
            "/api/v1/health": self._api_health,
            "/api/v1/capabilities": self._api_capabilities,
            "/api/v1/info": lambda: self._api_info(False),
            "/api/v1/diagnostics": lambda: self._api_info(True),
            "/api/v1/rtc": self._api_rtc,
            "/api/v1/gps": self._api_gps,
            "/api/v1/status": self._api_status,
        }
        if path not in builders:
            raise KeyError(f"unknown API v1 path {path!r}")
        payload = builders[path]()
        payload["simulator"] = self._simulator_metadata()
        return payload

    def _envelope(self, schema: str, data: Any) -> dict[str, Any]:
        snapshot = self._state
        age_ms = max(0, self.clock.now_us - snapshot["last_update_attempt_at_us"]) // 1000
        return {
            "schema": schema,
            "api_version": API_VERSION,
            "sequence": snapshot["sequence"],
            "last_update_attempt_at_us": snapshot["last_update_attempt_at_us"],
            "last_update_complete": snapshot["last_update_complete"],
            "last_update_error": snapshot["last_update_error"],
            "age_ms": age_ms,
            "data": data,
        }

    @staticmethod
    def _component_payload(component: Mapping[str, Any]) -> dict[str, Any]:
        return {key: copy.deepcopy(component[key]) for key in ("health", "last_error", "data_valid", "data_stale", "last_attempt_at_us", "last_success_at_us")}

    def _battery_payload(self) -> dict[str, Any]:
        battery = copy.deepcopy(self._state["battery"])
        current = _component_current(battery)
        level_valid = current and battery["level_percent_valid"] and 0 <= battery["level_percent"] <= 100
        voltage_valid = current and battery["battery_mv_valid"] and battery["battery_mv"] > 0
        battery["level_percent_valid"] = level_valid
        battery["level_percent"] = battery["level_percent"] if level_valid else None
        battery["battery_mv_valid"] = voltage_valid
        battery["battery_mv"] = battery["battery_mv"] if voltage_valid else None
        return battery

    def _gps_payload(self, include_sensitive: bool) -> dict[str, Any]:
        gps = copy.deepcopy(self._state["gps"])
        fix_valid = gps["has_fix"] and _component_current(gps)
        if not fix_valid:
            for field in ("latitude_degrees", "longitude_degrees", "altitude_m", "speed_mps", "course_degrees", "hdop", "utc_time", "utc_date"):
                gps[field] = None
        if not include_sensitive:
            gps.pop("first_sentence")
        return gps

    def _wifi_payload(self, include_sensitive: bool) -> dict[str, Any]:
        wifi = copy.deepcopy(self._state["radios"]["wifi"])
        if not include_sensitive:
            for field in ("ssid", "ip_address", "mac_address"):
                wifi.pop(field)
        return wifi

    def _distance_payload(self) -> dict[str, Any]:
        distance = copy.deepcopy(self._state["distance"])
        for sensor in distance["sensors"]:
            if not _component_current(sensor):
                for field in ("raw_distance_mm", "filtered_distance_mm", "window_sample_count", "inlier_count", "median_absolute_deviation_mm"):
                    sensor[field] = None
        if not _component_current(distance):
            for field in ("slant_distance_mm", "vertical_distance_mm", "sensor_spread_mm", "samples_used", "tilt_degrees", "confidence_percent"):
                distance[field] = None
        return distance

    def _info_data(self, include_sensitive: bool) -> dict[str, Any]:
        snapshot = self._state
        data = {
            "reason": snapshot["reason"],
            "battery": self._battery_payload(),
            "imu": copy.deepcopy(snapshot["imu"]),
            "sensor_fusion": copy.deepcopy(snapshot["sensor_fusion"]),
        }
        if include_sensitive:
            data["distance"] = self._distance_payload()
        data.update({
            "rtc": copy.deepcopy(snapshot["rtc"]),
            "gps": self._gps_payload(include_sensitive),
            "audio": copy.deepcopy(snapshot["audio"]),
            "radios": {
                "wifi": self._wifi_payload(include_sensitive),
                "ble": copy.deepcopy(snapshot["radios"]["ble"]),
            },
            "storage": copy.deepcopy(snapshot["storage"]),
        })
        if include_sensitive:
            data["diagnostics"] = copy.deepcopy(snapshot["diagnostics"])
        data.update({
            "ui": copy.deepcopy(snapshot["ui"]),
            "input": copy.deepcopy(snapshot["input"]),
            "power": copy.deepcopy(snapshot["power"]),
            "memory": copy.deepcopy(snapshot["memory"]),
            "processor": copy.deepcopy(snapshot["processor"]),
        })
        return data

    def _api_info(self, include_sensitive: bool) -> dict[str, Any]:
        schema = "LUNASHELL.device.diagnostics.v1" if include_sensitive else "LUNASHELL.device.info.v1"
        return self._envelope(schema, self._info_data(include_sensitive))

    def _api_rtc(self) -> dict[str, Any]:
        return self._envelope("LUNASHELL.device.rtc.v1", copy.deepcopy(self._state["rtc"]))

    def _api_gps(self) -> dict[str, Any]:
        return self._envelope("LUNASHELL.device.gps.v1", self._gps_payload(False))

    def _api_capabilities(self) -> dict[str, Any]:
        full_snapshot_period = self._state["diagnostics"]["full_snapshot_period"]
        refresh_interval_ms = (
            self._state["diagnostics"]["full_snapshot_period_ms"]
            if full_snapshot_period["health"] == "ready"
            and full_snapshot_period["last_error"] == "ESP_OK"
            and full_snapshot_period["data_valid"]
            else 0
        )
        return {
            "schema": "LUNASHELL.device.capabilities.v1",
            "api_version": API_VERSION,
            "data": {
                "read_only": True,
                "snapshot_refresh_interval_ms": refresh_interval_ms,
                "features": ["battery", "rtc", "imu", "gps", "audio", "wifi", "ble", "storage", "display", "touch"],
                "endpoints": ["/api/v1/health", "/api/v1/capabilities", "/api/v1/info", "/api/v1/rtc", "/api/v1/gps", "/api/v1/status", "/api/v1/diagnostics"],
            },
        }

    def _api_health(self) -> dict[str, Any]:
        snapshot = self._state
        wifi_connected = snapshot["radios"]["wifi"]["connected"] and snapshot["radios"]["wifi"]["ip_acquired"]
        return {
            "schema": "LUNASHELL.device.health.v1",
            "api_version": API_VERSION,
            "data": {
                "ok": True,
                "server_running": True,
                "server_cleanup_required": False,
                "server_handle_retained": True,
                "server_accepting_requests": True,
                "auth_required_for_data": True,
                "wifi_connected": wifi_connected,
                "snapshot": {
                    "sequence": snapshot["sequence"],
                    "last_update_attempt_at_us": snapshot["last_update_attempt_at_us"],
                    "last_update_complete": snapshot["last_update_complete"],
                    "age_ms": max(0, self.clock.now_us - snapshot["last_update_attempt_at_us"]) // 1000,
                },
                "liveness": {
                    "available": True,
                    "query_error": "ESP_OK",
                    "initialized": True,
                    "boot_reset_reason": 0,
                    "watchdog_timeout_ms": 5000,
                    "operation_sequence": snapshot["sequence"],
                    "active_mask": 0,
                    "last_error": "ESP_OK",
                    "last_progress": {"valid": True, "point": "snapshot-commit", "age_ms": 0},
                    "previous_timeout": {"valid": False, "operation_sequence": 0, "active_mask": 0, "last_progress_point": "unavailable"},
                },
            },
        }

    def _api_status(self) -> dict[str, Any]:
        named_paths = {
            "battery": "battery",
            "rtc": "rtc",
            "imu": "imu",
            "sensor_fusion": "sensor_fusion",
            "gps": "gps",
            "speaker": "audio.speaker",
            "microphone": "audio.microphone",
            "spectrum": "audio.spectrum",
            "wifi": "radios.wifi",
            "ble": "radios.ble",
            "storage": "storage",
            "display": "ui.display",
            "touch": "ui.touch",
            "io_expander": "input.io_expander",
            "boot_button": "input.boot_button",
            "power_button": "input.power_button",
            "power_management": "power",
        }
        components = {name: self._component_payload(_resolve(self._state, path)) for name, path in named_paths.items()}
        overall_paths = (
            "battery",
            "imu",
            "sensor_fusion",
            "rtc",
            "gps",
            "audio.speaker",
            "audio.microphone",
            "radios.wifi",
            "radios.ble",
            "storage",
            "ui.display",
            "ui.touch",
            "input.io_expander",
            "input.boot_button",
            "input.power_button",
            "power",
        )
        healths = {_resolve(self._state, path)["health"] for path in overall_paths}
        overall = "missing" if "missing" in healths else "degraded" if "degraded" in healths else "unknown" if "unknown" in healths else "ready"
        data = {
            "overall": overall,
            "server_running": True,
            "server_cleanup_required": False,
            "server_handle_retained": True,
            "server_accepting_requests": True,
            "components": components,
            "gps": {"has_fix": self._state["gps"]["has_fix"], "satellites": self._state["gps"]["satellites"]},
            "wifi": {"connected": self._state["radios"]["wifi"]["connected"], "ip_acquired": self._state["radios"]["wifi"]["ip_acquired"]},
            "storage": {"card_response_verified": self._state["storage"]["card_response_verified"], "formatted": self._state["storage"]["formatted"]},
            "metrics": {
                "request_count": 0,
                "authorized_count": 0,
                "unauthorized_count": 0,
                "response_failure_count": 0,
                "start_count": 1,
                "stop_count": 0,
                "start_failure_count": 0,
                "stop_failure_count": 0,
                "event_lifecycle_failure_count": 0,
                "last_lifecycle_error": 0,
                "auth_failures_in_window": 0,
            },
        }
        return self._envelope("LUNASHELL.device.status.v1", data)

    def dump_snapshot(self, *, indent: int = 2) -> str:
        return json.dumps(self._generation.to_dict(), indent=indent, sort_keys=True, ensure_ascii=False)

    def dump_api(self, path: str, *, indent: int = 2) -> str:
        return json.dumps(self.api_payload(path), indent=indent, sort_keys=True, ensure_ascii=False)


__all__ = [
    "API_VERSION",
    "SCENARIO_SCHEMA",
    "SIMULATOR_MODEL_VERSION",
    "ScenarioDefinition",
    "ScenarioEngine",
    "ScenarioValidationError",
    "SemanticActionError",
    "SnapshotGeneration",
    "TimelineEvent",
    "UnknownScenarioError",
    "VirtualClock",
    "load_scenario",
    "scenario_names",
    "validate_snapshot",
]
