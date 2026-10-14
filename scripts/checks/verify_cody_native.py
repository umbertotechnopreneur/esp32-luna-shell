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

"""Compile the actual portable C++ raster/rig and compare its named geometry with Python."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import evaluate, load_contract
from scripts.common.cody_rig_types import FaceChannel, channel_name, face_channel
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# No parameters: inspect only versioned model-notice inputs and outputs, without audio playback or regeneration.
# Raises original read/format errors or AssertionError for stale provenance, payload divergence or unsafe audio.
def check_model_cost_audio() -> dict:
    assets = ROOT / "assets/cody/audio/model-cost"
    generated = assets / "generated"
    selection = json.loads((assets / "source-selection.json").read_text(encoding="utf-8"))
    manifest = json.loads((generated / "manifest.json").read_text(encoding="utf-8"))
    if manifest["source"] != selection:
        raise AssertionError("Model cost audio: generated source selection is stale.")
    if (selection["schema_version"] != 1 or selection["license"] != "CC0-1.0" or
            selection["sample_rate_hz"] != 16000 or selection["repeat_count"] != 5 or
            selection["repeat_gap_ms"] != 100 or selection["maximum_duration_ms"] != 220 or
            selection["target_peak_dbfs"] != -18.0):
        raise AssertionError("Model cost audio: source selection differs from the bounded authored notice.")
    source = (assets / selection["source_file"]).resolve(strict=True)
    if not source.is_relative_to((assets / "source").resolve(strict=True)):
        raise AssertionError("Model cost audio: selected source escapes its versioned owner.")
    if hashlib.sha256(source.read_bytes()).hexdigest() != selection["source_sha256"]:
        raise AssertionError("Model cost audio: selected source SHA-256 differs from provenance.")
    pcm = (generated / "model-cost.pcm16le").read_bytes()
    if not 2 <= len(pcm) <= 5600 * 2 or len(pcm) % 2:
        raise AssertionError("Model cost audio: PCM16 must be nonempty and no longer than 350 ms.")
    sample_count = len(pcm) // 2
    rate = 16000
    if (manifest["sample_count"] != sample_count or manifest["pcm_bytes"] != len(pcm) or
            manifest["sample_rate_hz"] != rate or
            manifest["pcm_sha256"] != hashlib.sha256(pcm).hexdigest() or
            sample_count * 1000 > selection["maximum_duration_ms"] * rate or
            not math.isclose(manifest["duration_ms"], sample_count * 1000 / rate, abs_tol=1e-9)):
        raise AssertionError("Model cost audio: PCM identity, clock or duration differs from its manifest.")
    samples = struct.unpack(f"<{sample_count}h", pcm)
    peak = max(abs(sample) for sample in samples)
    # Half of one PCM16 quantization step is the only allowed upward peak-rounding error.
    maximum_peak = 32768 * 10 ** (-18 / 20) + .5
    if samples[0] != 0 or samples[-1] != 0 or not 0 < peak <= maximum_peak:
        raise AssertionError("Model cost audio: nonzero boundary, silence or peak above -18 dBFS.")
    peak_dbfs = 20 * math.log10(peak / 32768)
    if not math.isclose(manifest["peak_dbfs"], peak_dbfs, abs_tol=1e-9):
        raise AssertionError("Model cost audio: measured PCM peak differs from its manifest.")
    with wave.open(str(generated / "model-cost.wav"), "rb") as preview:
        if (preview.getnchannels() != 1 or preview.getsampwidth() != 2 or
                preview.getframerate() != rate or preview.getcomptype() != "NONE" or
                preview.getnframes() != sample_count or preview.readframes(sample_count + 1) != pcm):
            raise AssertionError("Model cost audio: mono 16-kHz WAV does not contain the exact firmware PCM.")
    header = (generated / "model_cost_audio.hpp").read_text(encoding="utf-8")
    cpp = (generated / "model_cost_audio.cpp").read_text(encoding="utf-8")
    if (re.findall(r"\bsize_t\s+sample_count\s*=\s*(\d+)\s*;", header) != [str(sample_count)] or
            re.findall(r"\buint32_t\s+sample_rate_hz\s*=\s*(\d+)\s*;", header) != [str(rate)]):
        raise AssertionError("Model cost audio: generated C++ length or clock differs from the PCM.")
    arrays = re.findall(r"const\s+int16_t\s+samples\s*\[\s*sample_count\s*\]\s*=\s*\{([^}]*)\}\s*;",
                        cpp, re.DOTALL)
    if len(arrays) != 1 or re.fullmatch(r"\s*(?:-?\d+\s*,\s*)+", arrays[0]) is None:
        raise AssertionError("Model cost audio: missing, ambiguous or nonliteral C++ sample array.")
    cpp_samples = tuple(int(value) for value in re.findall(r"-?\d+", arrays[0]))
    if cpp_samples != samples:
        raise AssertionError("Model cost audio: linked C++ samples differ from the WAV/PCM payload.")
    sequence_samples = sample_count * 5 + 4 * (rate * 100 // 1000)
    if not math.isclose(manifest["preview_duration_ms"], sequence_samples * 1000 / rate, abs_tol=1e-9):
        raise AssertionError("Model cost audio: sequence metadata must represent five clips and four gaps.")
    # The portable native fixture exercises actual repeat/gap mapping. Its volatile
    # listening preview is deliberately not needed to validate a clean checkout.
    return {"sample_count": sample_count, "pcm_bytes": len(pcm), "sample_rate_hz": rate,
            "pcm_sha256": manifest["pcm_sha256"], "peak_dbfs": peak_dbfs,
            "repetitions": 5, "gap_ms": 100, "physical_playback_verified": False}


# expected: World coordinates produced from the same authored inputs by the host evaluator.
# observed: Stable names and coordinates emitted by the real C++ evaluator.
# label: Identifies the pose whose cross-language semantics must agree.
# Raises AssertionError for missing, malformed, non-finite or divergent coordinates.
def compare_named_world(expected: dict, observed: dict, label: str) -> None:
    if set(observed) != set(expected):
        raise AssertionError(f"{label}: native/Python point identities differ; generated tables may need regeneration.")
    for name, coordinates in expected.items():
        if len(observed[name]) != 2:
            raise AssertionError(f"{label}: invalid native coordinate pair for {name}")
        for native, host in zip(observed[name], coordinates):
            # Float32 generation/evaluation permits 0.002 pixels, well below one visible pixel.
            if not abs(native - host) <= 0.002:
                raise AssertionError(f"{label}: native/Python geometry differs for {name}: {native} vs {host}")


# fixture: Named ready scene plus bounded facial poses emitted by the real generated C++ evaluator.
# Raises AssertionError for incomplete identity/pose coverage, or source-load errors for invalid authoring.
def compare_geometry(fixture: dict) -> None:
    rig, _ = load_contract()
    transform = tuple(rig["default_transform"][name] for name in ("x", "y", "scale"))
    expected = evaluate(rig, rig["boot_channels"]["ready"], transform)["world"]
    compare_named_world(expected, fixture["world"], "ready")
    cases = fixture.get("face_cases")
    if not isinstance(cases, list) or len(cases) != 3:
        raise AssertionError("Native fixture requires closed/open/asymmetric extended facial poses.")
    for index, case in enumerate(cases):
        authored = case["channels"]
        if set(authored) != {channel_name(channel) for channel in FaceChannel}:
            raise AssertionError(f"Extended face pose {index} has incomplete or unknown channel identities.")
        values = [0] * len(FaceChannel)
        for name, value in authored.items():
            values[face_channel(name)] = value
        expected = evaluate(rig, values, transform)["world"]
        compare_named_world(expected, case["world"], f"extended face {index}")


# argv: Explicit CLI options forwarded through scripts/device.ps1 check -Check cody-native.
# Raises original compiler, execution, contract or filesystem errors after saving their status.
def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing host clang++, g++ or cl; nothing is installed")
    parser.add_argument("--simd-model", action="store_true",
                        help="Exercise firmware packing/dispatch with a host arithmetic model, not S3 assembly")
    args = parser.parse_args(argv)
    output = ROOT / ("artifacts/checks/cody-native-simd-model" if args.simd_model else "artifacts/checks/cody-native")
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "portable C++ and Python geometry; hardware unverified"}
    try:
        report["model_cost_audio"] = check_model_cost_audio()
        compiler = resolve_compiler(args.compiler)
        executable = output / "raster-contract.exe"
        include_dirs = ("scripts/tests/cody_native", "components/CODY2D/include",
                        "components/LUNASHELL_platform/include", "assets/cody/scenes/generated",
                        "components/CODY_companion/include", "assets/cody/rig/generated",
                        "assets/cody/appearance/jelly-scanline/generated",
                        "assets/cody/personality-icons/generated")
        include_dirs += ("assets/cody/personality/generated",)
        include_dirs += ("assets/cody/home-dashboard-fonts/generated",)
        sources = ("scripts/tests/cody_native/raster_contract.cpp", "scripts/tests/scene_standby_policy.cpp",
                   "components/CODY2D/src/CODY2D.cpp",
                   "scripts/tests/cody_native/host_settings.cpp",
                   "components/CODY2D/src/CODY2D_paths.cpp", "components/CODY2D/src/CODY2D_text.cpp",
                   "components/CODY2D/src/CODY2D_controls.cpp", "components/CODY_companion/src/model/CODY_rig.cpp",
                   "components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
                   "components/CODY_companion/src/rendering/CODY_personality.cpp",
                   "assets/cody/personality-icons/generated/personality_icons.cpp",
                   "components/CODY_companion/src/rendering/CODY_jelly.cpp",
                   "components/CODY_companion/src/runtime/CODY_lifecycle.cpp",
                   "components/CODY_companion/src/scenes/CODY_scene.cpp",
                   "components/CODY_companion/src/scenes/CODY_technical_panels.cpp",
                   "components/CODY_companion/src/scenes/CODY_home_dashboard.cpp",
                   "components/CODY_companion/src/scenes/CODY_home_identity.cpp",
                   "assets/cody/home-dashboard-fonts/generated/home_dashboard_fonts.cpp",
                   "scripts/tests/cody_native/home_dashboard_contract.cpp",
                   "components/LUNASHELL_platform/src/snapshot/LUNASHELL_snapshot_aging.cpp",
                   "components/CODY_companion/src/model/CODY_expression_catalog.cpp",
                   "components/CODY_companion/src/model/CODY_connection_personality.cpp",
                   "assets/cody/scenes/generated/scene_font.cpp",
                   "assets/cody/scenes/generated/scene_body_font.cpp",
                   "assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp")
        definitions = ("CODY_ENABLE_PEARL_JELLY=1",)
        if args.simd_model:
            sources += ("scripts/tests/cody_native/jelly_simd_model.cpp",
                        "components/CODY_companion/src/diagnostics/CODY_jelly_simd_validation.cpp")
            definitions += ("CODY_JELLY_SIMD_AVAILABLE=1",)
            report["scope"] = "portable renderer and SIMD packing/model; actual S3 instructions unverified"
        command = compile_command(compiler, include_dirs, sources, executable,
                                  definitions=definitions, cxx_standard=20)
        report["compiler"] = compiler
        run_stage(command, output, "compile")
        native = run_stage([str(executable)], output, "native")
        compare_geometry(json.loads(native.stdout))
        report["status"] = "passed"
        report["simd_backend"] = "host arithmetic model" if args.simd_model else "excluded"
        report["contracts"] = ["RGB565 alpha rounding", "stride guards", "invalid geometry",
                               "analytic AA rectangle and concave coverage", "AA opacity, cap gradients and tile seams",
                               "scene invalidation", "culling and strip parity", "native/Python ready and extended-face geometry",
                               "independent extended-face ownership, blink gates, tongue opacity and eye clipping",
                               "rounded eye/mouth tangent continuity through blink and asymmetric expressions",
                               "connection active-time boundaries, edge admission, stale cancellation and scene reactions",
                               "compact tile parity and sentinels", "dirty reconstruction and unchanged frames",
                               "overlay, selection, pause, opacity, visibility, transform and contour damage"]
        report["contracts"].extend(["premultiplied jelly texture and transparent/opaque/AA samples",
                                    "unprofiled/coarse/block pixel parity, tile clipping, 16-pixel tails and timer counts",
                                    "all authored look textures and frame-owned skin identity",
                                    "look read/write failure propagation, missing-only default and display-owned activation",
                                    "jelly compact-tile guards and invalid mapping rejection",
                                    "capsule gradient, bounded ASCII caption, touch gate and compact-tile parity",
                                    "continuous 3-second PWR hold, early release and failed-sample rearming",
                                    "startup checkpoint gates and 800-ms panel wake",
                                    "700-ms farewell Sleep hold and two-row/ordinary native strip parity (no DMA or PMU claim)"])
        report["contracts"].extend(["sequential title, body and footer teletype after entrance",
                                    "current installation heading, hidden-heading fixture and 60-second scene override",
                                    "scene standby entry, real activity, inheritance and uint32 boundary",
                                    "long-lived and replaced text clocks without replayed entrance",
                                    "immutable retired character and text-generation baselines"])
        report["contracts"].extend(["model-price notice source/PCM SHA-256 and WAV/C++ sample equivalence",
                                    "mono 16-kHz bounded plink, zero endpoints and -18 dBFS digital peak",
                                    "one notice per costly model selection and five-clip/four-gap native sequence"])
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Native Cody contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
