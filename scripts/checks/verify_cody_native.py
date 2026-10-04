"""Compile the actual portable C++ raster/rig and compare its named geometry with Python."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_rig_contract import evaluate, load_contract
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# fixture: Named world coordinates emitted by the real generated C++ ready scene.
# Raises AssertionError for identity/coordinate divergence, or source-load errors for invalid authoring data.
def compare_geometry(fixture: dict) -> None:
    rig, _ = load_contract()
    transform = tuple(rig["default_transform"][name] for name in ("x", "y", "scale"))
    expected = evaluate(rig, rig["boot_channels"]["ready"], transform)["world"]
    observed = fixture["world"]
    if set(observed) != set(expected):
        raise AssertionError("Native/Python point identities differ; generated tables may need regeneration.")
    for name, coordinates in expected.items():
        if len(observed[name]) != 2:
            raise AssertionError(f"Invalid native coordinate pair for {name}")
        for native, host in zip(observed[name], coordinates):
            # Float32 generation/evaluation permits 0.002 pixels, well below one visible pixel.
            if not abs(native - host) <= 0.002:
                raise AssertionError(f"Native/Python geometry differs for {name}: {native} vs {host}")


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
                        "assets/cody/appearance/jelly-scanline/generated")
        sources = ("scripts/tests/cody_native/raster_contract.cpp", "scripts/tests/scene_standby_policy.cpp",
                   "components/CODY2D/src/CODY2D.cpp",
                   "scripts/tests/cody_native/host_settings.cpp",
                   "components/CODY2D/src/CODY2D_paths.cpp", "components/CODY2D/src/CODY2D_text.cpp",
                   "components/CODY2D/src/CODY2D_controls.cpp", "components/CODY_companion/src/model/CODY_rig.cpp",
                   "components/CODY_companion/src/rendering/CODY_dirty_plan.cpp",
                   "components/CODY_companion/src/rendering/CODY_jelly.cpp",
                   "components/CODY_companion/src/runtime/CODY_lifecycle.cpp",
                   "components/CODY_companion/src/scenes/CODY_scene.cpp",
                   "components/CODY_companion/src/model/CODY_expression_catalog.cpp",
                   "assets/cody/scenes/generated/scene_font.cpp",
                   "assets/cody/scenes/generated/scene_body_font.cpp",
                   "assets/cody/appearance/jelly-scanline/generated/cody_jelly_data.cpp")
        definitions = ("CODY_ENABLE_PEARL_JELLY=1",)
        if args.simd_model:
            sources += ("scripts/tests/cody_native/jelly_simd_model.cpp",
                        "components/CODY_companion/src/diagnostics/CODY_jelly_simd_validation.cpp")
            definitions += ("CODY_JELLY_SIMD_AVAILABLE=1",)
            report["scope"] = "portable renderer and SIMD packing/model; actual S3 instructions unverified"
        command = compile_command(compiler, include_dirs, sources, executable, definitions=definitions)
        report["compiler"] = compiler
        run_stage(command, output, "compile")
        native = run_stage([str(executable)], output, "native")
        compare_geometry(json.loads(native.stdout))
        report["status"] = "passed"
        report["simd_backend"] = "host arithmetic model" if args.simd_model else "excluded"
        report["contracts"] = ["RGB565 alpha rounding", "stride guards", "invalid geometry",
                               "analytic AA rectangle and concave coverage", "AA opacity, cap gradients and tile seams",
                               "scene invalidation", "culling and strip parity", "native/Python ready geometry",
                               "compact tile parity and sentinels", "dirty reconstruction and unchanged frames",
                               "overlay, selection, pause, opacity, visibility, transform and contour damage"]
        report["contracts"].extend(["premultiplied jelly texture and transparent/opaque/AA samples",
                                    "unprofiled/coarse/block pixel parity, tile clipping, 16-pixel tails and timer counts",
                                    "all authored look textures and frame-owned skin identity",
                                    "look read/write failure propagation, missing-only default and display-owned activation",
                                    "jelly compact-tile guards and invalid mapping rejection",
                                    "capsule gradient, bounded ASCII caption, touch gate and compact-tile parity",
                                    "continuous 3-second PWR hold, early release and failed-sample rearming",
                                    "2/4/4-second boot timeline, startup checkpoint gates and 800-ms panel wake"])
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
