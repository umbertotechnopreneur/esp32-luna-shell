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
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import evaluate, load_contract
from scripts.common.cody_rig_types import FaceChannel, channel_name, face_channel
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


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
        compiler = resolve_compiler(args.compiler)
        executable = output / "raster-contract.exe"
        include_dirs = ("scripts/tests/cody_native", "components/CODY2D/include",
                        "components/LUNASHELL_platform/include", "assets/cody/scenes/generated",
                        "components/CODY_companion/include", "assets/cody/rig/generated",
                        "assets/cody/appearance/jelly-scanline/generated",
                        "assets/cody/personality-icons/generated")
        include_dirs += ("assets/cody/personality/generated",)
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
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Native Cody contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
