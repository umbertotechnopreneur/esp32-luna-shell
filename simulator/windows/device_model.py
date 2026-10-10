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
        "ESP_ERR_INVALID_SIZE",
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


# snapshot: Explicit input or aged host observation retaining historical PMIC fields.
# Raises ScenarioValidationError for malformed values; stale history does not become current.
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
        observed = current or (battery["data_stale"] and battery["last_success_at_us"] > 0)
        _require(observed and battery["battery_present"] and battery["usb_present"] and battery["vbus_good"], "charging requires a current or historical PMIC observation, cell, and good VBUS")
        _require(battery["charger_phase"] != "stopped", "charging requires an active charger phase")


# snapshot: Explicit input or aged observation whose original fix may remain historical.
# Raises ScenarioValidationError for fabricated fixes or invalid retained numeric fields.
def _validate_gps(snapshot: Mapping[str, Any]) -> None:
    gps = snapshot["gps"]
    if gps["has_fix"]:
        observed = _component_current(gps) or (gps["data_stale"] and gps["last_success_at_us"] > 0)
        _require(observed, "GPS fix requires a current or explicitly historical receiver sample")
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


# snapshot: Coherent explicit synthetic scenario observation.
# Raises ScenarioValidationError for invalid operating state or display/chrome invariants.
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
        # Wake speech may retain audio while every other standby invariant holds.
        _require(not power["fullscreen_active"], "standby cannot retain a fullscreen lock")
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

    # self: Explicit monotonic host clock, independent of firmware acquisition.
    # target_ms: Nondecreasing absolute virtual time in milliseconds.
    # Raises ScenarioValidationError for invalid or regressing time.
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

    # self: Synthetic acquisition owner; production aging acts on a separate host-only observation.
    # state: Candidate acquisition mapping, or the currently committed source.
    # now_us: Candidate virtual observation time; None uses the committed clock.
    # Raises native/schema errors without advancing publication ownership.
    def _publish(self, state=None, now_us=None) -> SnapshotGeneration:
        state = self._state if state is None else state
        now_us = self.clock.now_us if now_us is None else now_us
        validate_snapshot(state, now_us=now_us)
        from cody_native_snapshot import NativeSnapshot
        observed = NativeSnapshot(state, now_us)
        aged = observed.aged_mapping(state)
        next_generation = self._generation_count + 1
        simulator = {
            "synthetic": True,
            "model_version": SIMULATOR_MODEL_VERSION,
            "scenario": self.scenario.name,
            "title": self.scenario.title,
            "clock_us": now_us,
            "elapsed_ms": (now_us - self.scenario.initial_time_us) // 1000,
            "generation": next_generation,
        }
        standby = self.standby_diagnostics()
        if standby is not None:
            simulator["standby"] = standby
        generation = SnapshotGeneration(
            sequence=state["sequence"],
            at_us=now_us,
            snapshot=_freeze(aged),
            simulator=_freeze(simulator),
        )
        self._native_snapshot = observed
        self._generation_count = next_generation
        return generation

    def snapshot(self) -> dict[str, Any]:
        return _thaw(self._generation.snapshot)

    def advance_ms(self, delta_ms: int) -> SnapshotGeneration:
        _require(_is_int(delta_ms) and delta_ms >= 0, "advance delta must be non-negative")
        return self.advance_to_ms(self.elapsed_ms + delta_ms)

    # self: Deterministic host acquisition and publication owner.
    # target_ms: Nondecreasing authored timeline target.
    # Raises original native/schema errors before committing the failing event or virtual clock step.
    def advance_to_ms(self, target_ms: int) -> SnapshotGeneration:
        _require(_is_int(target_ms) and target_ms >= self.elapsed_ms, "scenario timeline cannot move backwards")
        target_us = self.scenario.initial_time_us + target_ms * 1000
        published = False
        while self._event_index < len(self.scenario.timeline):
            event = self.scenario.timeline[self._event_index]
            event_us = self.scenario.initial_time_us + event.at_ms * 1000
            if event_us > target_us:
                break
            trial = copy.deepcopy(self._state)
            _apply_event_state(trial, event, event_us)
            generation = self._publish(trial, event_us)
            self.clock.advance_to_us(event_us)
            self._state = trial
            self._event_index += 1
            self._generation = generation
            published = True
        if not published or self._generation.at_us != target_us:
            generation = self._publish(self._state, target_us)
            self.clock.advance_to_us(target_us)
            self._generation = generation
        return self._generation

    # self: Host owner committing a validated acquisition and native publication together.
    # patch: Exact original input fields for this semantic action.
    # reason: Explicit synthetic action identity.
    # touch_components: Only acquisition owners whose attempt occurred at the current virtual time.
    # Raises original native/schema errors before the candidate becomes committed state.
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
        generation = self._publish(trial)
        self._state = trial
        self._generation = generation
        return generation

    def action(self, name: str, **params: Any) -> SnapshotGeneration:
        """Apply a semantic user/device action with strict preconditions."""

        actions = {
            "press:pwr": self._action_power,
            "standby:profile": self._action_standby_profile,
            "standby:fault": self._action_standby_fault,
            "rtc-registers": self._action_rtc_registers,
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

    # self: Scenario owner; an absent synthetic baseline is borrowed until publication commits it.
    # Raises RuntimeError/OSError/ValueError when the required native implementation is unavailable.
    def _standby_owner(self):
        from cody_native_standby import NativeStandby
        if not hasattr(self, "_native_standby"):
            state = self._state["power"]["state"]
            if state not in {"active", "standby"}:
                raise SemanticActionError(f"Cannot initialize standby owner during {state}")
            return NativeStandby(standby=state == "standby")
        return self._native_standby

    # self: Scenario owner; only the publication thread may observe its private candidate.
    # Returns no synthetic power claims before the first native owner commits successfully.
    def standby_diagnostics(self) -> dict | None:
        publication = getattr(self, "_standby_publication", None)
        # API readers on other threads keep seeing the committed owner while
        # this thread builds candidate metadata under the server's existing action lock.
        owner = getattr(publication, "candidate", None)
        if owner is None:
            owner = getattr(self, "_native_standby", None)
        return owner.snapshot() if owner is not None else None

    # self: Serialized scenario owner; published generations and native resource ownership advance together.
    # candidate: Private host-only owner retaining proposed profile, faults or resource transitions.
    # patch: Proposed power projection, or None for a metadata-only profile/fault change.
    # Raises original publication errors after discarding the candidate; no hardware rollback is inferred.
    def _publish_standby_candidate(self, candidate, patch: Mapping[str, Any] | None = None) -> SnapshotGeneration:
        import threading
        if not hasattr(self, "_standby_publication"):
            self._standby_publication = threading.local()
        publication = self._standby_publication
        if hasattr(publication, "candidate"):
            candidate.release()
            raise SemanticActionError("Native standby publication cannot be reentered")
        publication.candidate = candidate
        try:
            if patch is None:
                generation = self._publish()
            else:
                generation = self._commit_action(patch, reason="synthetic-native-pwr",
                    touch_components=("input.power_button", "power", "ui.display_power_mode", "ui.brightness"))
        except BaseException:
            candidate.release()
            raise
        finally:
            del publication.candidate
        # Resource mutations occurred only in private host memory. The acquisition
        # owner prepared the generation before this candidate becomes its committed peer.
        self._native_standby = candidate
        self._generation = generation
        return generation

    # self: Synthetic scenario owner retaining immutable published generations.
    # params: No user parameters; native C++ decides ordering, errors and recovery.
    # Raises SemanticActionError after publishing a failed transaction's actual result.
    def _action_power(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        self._no_params("press:pwr", params)
        from cody_native_standby import error_name
        state = self._state["power"]["state"]
        if state not in {"active", "standby"}:
            raise SemanticActionError(f"press:pwr rejected during {state}")
        owner = self._standby_owner()
        before = owner.snapshot()
        if before["incomplete"] or before["standby"] != (state == "standby"):
            raise SemanticActionError("Scenario power state diverged from its native resource owner")
        target = state == "active"
        candidate = owner.prepare()
        result = candidate.transition(target)
        if result["incomplete"]:
            next_state = "entering-standby" if target else "waking"
        else:
            next_state = "standby" if result["standby"] else "active"
        panel_state = ("display-off" if result["display_off"] else "active") if result["display_consistent"] else "unknown"
        request_error = result["request_error_code"]
        panel_error = 0 if result["display_consistent"] else (
            result["display_rollback_error_code"] or request_error)
        sequence_increment = int(target or (next_state == "active" and state == "standby"))
        patch: dict[str, Any] = {
            "input.power_short_press_count": self._state["input"]["power_short_press_count"] + 1,
            "power.state": next_state,
            "power.reason": "power-button",
            "power.audio_suspended": result["audio_suspended"],
            "power.fullscreen_active": False,
            "power.command_arc_visible": next_state == "active",
            "power.status_arc_visible": next_state == "active",
            "power.display_idle_stage": panel_state,
            "power.transition_sequence": self._state["power"]["transition_sequence"] + sequence_increment,
            "power.changed_at_us": self.clock.now_us,
            "power.health": "degraded" if request_error else "ready",
            "power.last_error": error_name(request_error),
            "power.data_valid": request_error == 0,
            "power.data_stale": False,
            "ui.display_power_mode_state": panel_state,
        }
        for component in ("ui.display_power_mode", "ui.brightness"):
            patch[component + ".health"] = "degraded" if panel_error else "ready"
            patch[component + ".last_error"] = error_name(panel_error)
            patch[component + ".data_valid"] = panel_error == 0
            patch[component + ".data_stale"] = False
        if next_state == "active" and state == "standby":
            patch["power.last_activity_at_us"] = self.clock.now_us
        if result["display_consistent"]:
            patch["ui.brightness_percent"] = 0 if result["display_off"] else self._state["ui"]["configured_brightness_percent"]
        generation = self._publish_standby_candidate(candidate, patch)
        if request_error:
            raise SemanticActionError(f"Native standby request failed: {error_name(request_error)}; resulting state={next_state}")
        return generation

    # self: Synthetic scenario owner; C++ owns profile policy and admission.
    # params: Exactly four boolean service/profile requirements.
    # Raises SemanticActionError for invalid input or an actual native owner rejection.
    def _action_standby_profile(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        expected = {"deep_sleep_enabled", "retained_connection", "voice_listening", "touch_wake_available"}
        if set(params) != expected or any(type(value) is not bool for value in params.values()):
            raise SemanticActionError("standby:profile requires four explicit boolean service/profile fields")
        candidate = self._standby_owner().prepare()
        try:
            candidate.configure(**params)
        except (ValueError, RuntimeError) as error:
            candidate.release()
            raise SemanticActionError(str(error)) from error
        return self._publish_standby_candidate(candidate)

    # self: Synthetic scenario owner; faults never reach physical resource owners.
    # params: Resource identity, target and real error, with optional bounded count/skip.
    # Raises SemanticActionError for invalid fault identity or native admission failure.
    def _action_standby_fault(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        from cody_native_standby import RESOURCE_NAMES
        required = {"resource", "target", "error"}
        allowed = required | {"count", "skip"}
        if not required.issubset(params) or not set(params).issubset(allowed):
            raise SemanticActionError("standby:fault requires resource, target, error and optional count/skip")
        name = params["resource"]
        if not isinstance(name, str) or name not in RESOURCE_NAMES:
            raise SemanticActionError(f"Unknown native standby resource {name!r}")
        candidate = self._standby_owner().prepare()
        try:
            candidate.inject_fault(RESOURCE_NAMES[name], target=params["target"],
                error=params["error"], count=params.get("count", 1), skip=params.get("skip", 0))
        except (ValueError, RuntimeError) as error:
            candidate.release()
            raise SemanticActionError(str(error)) from error
        return self._publish_standby_candidate(candidate)

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

    # self: Explicit synthetic acquisition owner, separate from the firmware canonical service.
    # params: Exact registers input containing seven synthetic register bytes for the real decoder.
    # Raises input/schema errors; decoder failures are published with their real cause before raising.
    def _action_rtc_registers(self, params: Mapping[str, Any]) -> SnapshotGeneration:
        from cody_native_snapshot import NativeSnapshot
        if set(params) != {"registers"}:
            raise SemanticActionError("rtc-registers requires exactly the registers field")
        owner = NativeSnapshot(self._state, self.clock.now_us)
        error, data = owner.decode_rtc(params["registers"])
        # Register acquisition owns the RTC only; the independent system clock
        # must retain its original measurement and attempt timestamps.
        patch = {f"rtc.{name}": data[name] for name in self._state["rtc"] if name != "system_time"}
        generation = self._commit_action(patch, reason="synthetic-rtc-registers")
        if error:
            raise SemanticActionError(f"SIMULATED production RTC decoder failed: esp_err_t=0x{error & 0xffffffff:08x}")
        return generation

    # self: Host owner of the current aged native snapshot generation.
    # path: Exact public firmware API route; Python does not decide field validity or redaction.
    # Raises the original native serialization/route error without a Python response fallback.
    def api_payload(self, path: str) -> dict[str, Any]:
        payload = self._native_snapshot.api_payload(path)
        payload["simulator"] = self._simulator_metadata()
        payload["simulator"]["runtime"] = "production-cpp"
        return payload

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
