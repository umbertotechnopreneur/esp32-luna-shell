"""Strict, named authoring boundary for the shared Cody response model."""

from enum import Enum
import math


class Axis(str, Enum):
    X = "x"
    Y = "y"
    Z = "z"


class StimulusId(str, Enum):
    MOTION_ACCELERATION = "motion.acceleration"
    POWER_CHARGE = "power.charge"
    MICROPHONE_LEVEL = "audio.microphone"
    SPEAKER_PLAYBACK = "audio.playback"


# These are reviewed authoring limits, not repairs of bad configuration.
RESPONSE_RANGES = {
    "touch": {
        "event_timeout_seconds": (.1, 10),
        "eye_hit_radius": (.05, .25),
        "eye_hold_seconds": (.1, 3),
        "close_seconds": (.01, .5),
        "reopen_seconds": (.05, 3),
        "annoyance_release_seconds": (.1, 10),
        "stress_gain": (0, 1),
        "brow_gain": (0, 1),
        "drag_gain": (0, 2),
        "drag_limit_px": (0, 70),
        "press_depth": (0, .15),
        "spring_hz": (.2, 10),
        "damping_ratio": (.2, 2),
    },
    "audio": {
        "sample_timeout_seconds": (.1, 10),
        "microphone_reference": (.001, 1),
        "playback_reference": (.001, 1),
        "attack_seconds": (.01, 5),
        "release_seconds": (.01, 5),
        "attention_brow": (0, 1),
        "playback_mouth": (0, 1),
    },
    "idle": {
        "after_seconds": (1, 600),
        "duration_seconds": (.5, 10),
        "cooldown_seconds": (1, 600),
        "activity_stress_threshold": (.001, 1),
        "audio_activity_threshold": (.001, 1),
        "mouth_open": (0, 1),
        "eye_closure": (0, 1),
    },
    "motion": {
        "sensitivity": (0, 4),
        "baseline_seconds": (.05, 5),
        "sample_timeout_seconds": (.05, 10),
        "translation_gain": (0, .1),
        "translation_limit": (0, .25),
        "depth_gain": (0, .03),
        "depth_limit": (0, .12),
        "spring_hz": (.2, 10),
        "damping_ratio": (.2, 2),
        "stress_reference_mps2": (.1, 100),
    },
    "power": {
        "sample_timeout_seconds": (.1, 60),
        "fatigue_curve": (.2, 4),
    },
    "state": {
        "stress_attack_seconds": (.01, 10),
        "stress_release_seconds": (.05, 60),
        "fatigue_attack_seconds": (.01, 60),
        "fatigue_release_seconds": (.01, 60),
    },
    "expression": {
        "stress_brow": (0, 1),
        "fatigue_eye_closure": (0, 1),
        "fatigue_cheek_fade": (0, 1),
        "stress_mouth_open": (0, 1),
        "stress_mouth_narrowing": (0, .5),
        "stress_wave_gain": (0, 1),
        "stress_wave_speed_gain": (0, 1),
        "fatigue_wave_slowdown": (0, 1),
        "fatigue_fullness": (0, .5),
        "base_breath_hz": (.08, 1),
        "stress_breath_gain": (0, 2),
        "fatigue_breath_slowdown": (0, .9),
        "rhythm_blend_seconds": (.01, 5),
    },
}


# value: YAML response section with explicit source identities and units.
# Raises ValueError for missing/extra fields, nonfinite values or ambiguous axis maps.
def validate_response_config(value):
    expected = {"schema", *RESPONSE_RANGES}
    if not isinstance(value, dict) or set(value) != expected:
        raise ValueError("Response requires exactly its named schema and model sections")
    if value["schema"] != "cody.response.v3":
        raise ValueError(f"Unsupported response schema: {value['schema']}")
    for group, ranges in RESPONSE_RANGES.items():
        section = value[group]
        metadata = {"source", "unit", "board_to_panel"} if group == "motion" else set()
        if group == "power":
            metadata = {"source", "unit"}
        if not isinstance(section, dict) or set(section) != set(ranges) | metadata:
            raise ValueError(f"response.{group}: all named fields are required; unknown fields are rejected")
        for name, (minimum, maximum) in ranges.items():
            number = section[name]
            if isinstance(number, bool) or not isinstance(number, (int, float)):
                raise ValueError(f"response.{group}.{name}: expected a number, got {number!r}")
            if not math.isfinite(number) or not minimum <= number <= maximum:
                raise ValueError(f"response.{group}.{name}: rejected {number!r}; range {minimum}..{maximum}")
    motion = value["motion"]
    power = value["power"]
    if motion["source"] != StimulusId.MOTION_ACCELERATION or motion["unit"] != "m/s2":
        raise ValueError("Motion source must be motion.acceleration in m/s2")
    if power["source"] != StimulusId.POWER_CHARGE or power["unit"] != "percent":
        raise ValueError("Power source must be power.charge in percent")
    mapping = motion["board_to_panel"]
    if not isinstance(mapping, dict) or set(mapping) != set(Axis):
        raise ValueError("board_to_panel requires named x, y and z mappings")
    used_axes = set()
    for panel_axis in Axis:
        entry = mapping[panel_axis]
        if not isinstance(entry, dict) or set(entry) != {"axis", "sign"}:
            raise ValueError(f"board_to_panel.{panel_axis.value}: expected axis and sign")
        if entry["axis"] not in set(Axis) or type(entry["sign"]) is not int or entry["sign"] not in (-1, 1):
            raise ValueError(f"board_to_panel.{panel_axis.value}: invalid axis/sign {entry!r}")
        used_axes.add(entry["axis"])
    if used_axes != set(Axis):
        raise ValueError("board_to_panel must be a signed permutation, with no repeated sensor axis")
