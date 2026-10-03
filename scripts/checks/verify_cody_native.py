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
    args = parser.parse_args(argv)
    output = ROOT / "artifacts/checks/cody-native"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "portable C++ and Python geometry; hardware unverified"}
    try:
        compiler = resolve_compiler(args.compiler)
        executable = output / "raster-contract.exe"
        include_dirs = ("scripts/tests/cody_native", "components/CODY2D/include",
                        "components/CODY_companion/include", "assets/cody/rig/generated")
        sources = ("scripts/tests/cody_native/raster_contract.cpp", "components/CODY2D/CODY2D.cpp",
                   "components/CODY2D/CODY2D_paths.cpp", "components/CODY_companion/CODY_rig.cpp")
        command = compile_command(compiler, include_dirs, sources, executable)
        report["compiler"] = compiler
        run_stage(command, output, "compile")
        native = run_stage([str(executable)], output, "native")
        compare_geometry(json.loads(native.stdout))
        report["status"] = "passed"
        report["contracts"] = ["RGB565 alpha rounding", "stride guards", "invalid geometry",
                               "scene invalidation", "culling and strip parity", "native/Python ready geometry"]
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Native Cody contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
