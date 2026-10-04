"""Check finite benchmark sessions without Pearl, then the native renderer including pixel parity."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.checks import verify_cody_native


# argv: Options forwarded by device.ps1 check -Check cody-raster-benchmark.
# Raises original compiler, assertion, native-suite or filesystem errors after saving status.
def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing host clang++, g++ or cl; nothing is installed")
    args = parser.parse_args(argv)
    output = ROOT / "artifacts/checks/cody-raster-benchmark"
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "failed", "scope": "portable session and raster contracts; firmware/device unverified"}
    try:
        compiler = resolve_compiler(args.compiler)
        executable = output / "raster-benchmark-contract.exe"
        command = compile_command(
            compiler,
            ("scripts/tests/cody_native", "components/CODY2D/include",
             "components/CODY_companion/include", "assets/cody/rig/generated"),
            ("scripts/tests/cody_native/raster_benchmark_contract.cpp",
             "components/CODY_companion/src/diagnostics/CODY_raster_benchmark.cpp"),
            executable, definitions=("CODY_ENABLE_PEARL_JELLY=0",))
        report["compiler"] = compiler
        run_stage(command, output, "compile-session")
        run_stage([str(executable)], output, "session")
        verify_cody_native.main(["--compiler", compiler])
        report["contracts"] = ["non-Pearl shared profile compilation", "warmup and window boundaries",
                               "full-run p95 beyond 128 frames", "25 FPS missed budget",
                               "invalid/empty/interrupted runs", "fixed payload and capacity rejection",
                               "native renderer contracts including profiled/fused pixel parity"]
        report["status"] = "passed"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Raster benchmark contracts passed. Report: {output / 'report.json'}")


if __name__ == "__main__":
    main()
