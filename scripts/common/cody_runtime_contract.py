"""Strict shared authoring boundary for firmware and host runtime settings."""
from pathlib import Path
import math
import yaml
from scripts.common.cody_rig_contract import RigLoader
from scripts.common.cody_codex_audio import BOOT_IDENTITY


# value: Authored mapping whose exact field set is part of this schema.
# names: Required keys, independent of their order in YAML.
# label: Human-readable configuration location used for rejection.
# Raises ValueError for missing, unknown or incorrectly typed sections.
def fields(value, names, label):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f"{label}: requires exactly {sorted(names)}")


# value: Authored scalar, never silently clamped.
# lower: Reviewed inclusive lower bound.
# upper: Reviewed inclusive upper bound.
# label: Stable configuration field name.
# Raises ValueError for nonnumeric, nonfinite or out-of-range values.
def number(value, lower, upper, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label}: finite number required")
    if not lower <= value <= upper:
        raise ValueError(f"{label}: rejected {value}; expected {lower}..{upper}")
    return value


# path: Repository-owned canonical runtime YAML.
# Raises ValueError or OSError for invalid authoring or unreadable assets.
def load_settings(path):
    settings = yaml.load(path.read_text(encoding="utf-8"), Loader=RigLoader)
    fields(settings, ("schema", "demo", "benchmark", "diagnostics", "audio"), "runtime")
    if settings["schema"] != "cody.runtime.v1":
        raise ValueError("Unsupported runtime schema")
    demo = settings["demo"]
    fields(demo, ("autostart", "steps"), "demo")
    if type(demo["autostart"]) is not bool or not isinstance(demo["steps"], list):
        raise ValueError("Demo requires a boolean autostart and a step list")
    identities = {"breathe", "contour_slow", "contour_fast", "sleepy", "awake"}
    seen = set()
    for step in demo["steps"]:
        fields(step, ("id", "duration_seconds", "face", "amplitude", "speed"), "demo.step")
        if step["id"] not in identities or step["id"] in seen:
            raise ValueError("Demo step identities must be unique and supported")
        seen.add(step["id"])
        number(step["duration_seconds"], .1, 60, "demo.duration_seconds")
        number(step["amplitude"], 0, 1, "demo.amplitude")
        number(step["speed"], 0, 2, "demo.speed")
    if seen != identities:
        raise ValueError("All five named demo phases are required")
    benchmark = settings["benchmark"]
    fields(benchmark, ("enabled", "profile_blocks", "kernel", "cloud_scale_percent"), "benchmark")
    for name in ("enabled", "profile_blocks"):
        if type(benchmark[name]) is not bool:
            raise ValueError(f"benchmark.{name}: boolean required")
    if benchmark["profile_blocks"] and not benchmark["enabled"]:
        raise ValueError("benchmark.profile_blocks requires benchmark.enabled")
    if benchmark["kernel"] not in ("scalar", "simd", "simd_verify"):
        raise ValueError("benchmark.kernel: expected scalar, simd or simd_verify")
    if benchmark["kernel"] != "scalar" and not benchmark["enabled"]:
        raise ValueError("benchmark.kernel: SIMD experiments require benchmark.enabled")
    if benchmark["kernel"] == "simd_verify" and benchmark["profile_blocks"]:
        raise ValueError("benchmark.kernel: verification and detailed timing are separate experiments")
    if type(benchmark["cloud_scale_percent"]) is not int:
        raise ValueError("benchmark.cloud_scale_percent: integer required")
    number(benchmark["cloud_scale_percent"], 50, 100, "benchmark.cloud_scale_percent")
    diagnostics = settings["diagnostics"]
    fields(diagnostics, ("execution_timings", "mirror_to_sd_when_available", "report_interval_ms", "frame_budget_us"), "diagnostics")
    for name in ("execution_timings", "mirror_to_sd_when_available"):
        if type(diagnostics[name]) is not bool:
            raise ValueError(f"diagnostics.{name}: boolean required")
    for name, lower, upper in (("report_interval_ms", 500, 60000), ("frame_budget_us", 1000, 1000000)):
        if type(diagnostics[name]) is not int:
            raise ValueError(f"diagnostics.{name}: integer required")
        number(diagnostics[name], lower, upper, name)
    if benchmark["enabled"] and not diagnostics["execution_timings"]:
        raise ValueError("benchmark.enabled requires diagnostics.execution_timings")
    if benchmark["enabled"] and diagnostics["frame_budget_us"] != 40000:
        raise ValueError("benchmark.enabled requires diagnostics.frame_budget_us=40000 (25 FPS)")
    audio = settings["audio"]
    fields(audio, ("sample_rate_hz", "ready_duration_seconds", "eyes_open_cue", "ready_volume_percent", "eyes_open_volume_percent"), "audio")
    if type(audio["sample_rate_hz"]) is not int or audio["sample_rate_hz"] != 22050:
        raise ValueError("Boot cues must match the shared 22050 Hz codec/diagnostic clock")
    number(audio["ready_duration_seconds"], .05, .5, "audio.ready_duration_seconds")
    if audio["eyes_open_cue"] != BOOT_IDENTITY:
        raise ValueError(f"audio.eyes_open_cue: unknown cue {audio['eyes_open_cue']!r}")
    for name in ("ready_volume_percent", "eyes_open_volume_percent"):
        if type(audio[name]) is not int:
            raise ValueError(f"audio.{name}: integer required")
        number(audio[name], 0, 100, name)
    return settings


# settings: Strict authored phase list with stable IDs and positive durations.
# rig: Selected skin providing named face presets and contour defaults.
# elapsed_ms: Active animation clock; standby is excluded by the caller.
# Raises ValueError for negative time or an unknown authored face preset.
def demo_targets(settings, rig, elapsed_ms):
    from scripts.common.cody_rig_contract import contour_defaults
    from scripts.common.cody_rig_types import ContourControl
    if not math.isfinite(elapsed_ms) or elapsed_ms < 0:
        raise ValueError("Demo requires nonnegative active time")
    steps = settings["demo"]["steps"]
    cycle_ms = sum(round(step["duration_seconds"] * 1000) for step in steps)
    phase_ms = int(elapsed_ms) % cycle_ms
    for step in steps:
        duration_ms = round(step["duration_seconds"] * 1000)
        if phase_ms < duration_ms:
            presets = rig["boot_channels"] | rig["expression_presets"]
            if step["face"] not in presets:
                raise ValueError(f"Unknown named demo face: {step['face']}")
            contour = contour_defaults(rig)
            contour[ContourControl.AMPLITUDE] = step["amplitude"]
            contour[ContourControl.SPEED] = step["speed"]
            return step["id"], list(presets[step["face"]]), contour
        phase_ms -= duration_ms
    raise ValueError("Demo phase durations do not cover the cycle")


