"""Run the real weekly usage-alarm projection with isolated host snapshots."""

import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.common.cody_host_compiler import prepare_host_compiler
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# argv: Explicit options forwarded by scripts/device.ps1; no device is accessed.
# Exception: Propagates the original compiler, contract or filesystem failure.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing host clang++, g++ or initialized MSVC cl")
    args = parser.parse_args(argv)
    compiler = resolve_compiler(args.compiler) if args.compiler else prepare_host_compiler()
    parent = ROOT / "artifacts/checks"
    parent.mkdir(parents=True, exist_ok=True)
    # Each run keeps its own compiler output; never overwrite or remove prior artifacts.
    output = Path(tempfile.mkdtemp(prefix="cody-usage-alarms-", dir=parent))
    executable = output / "usage-alarm-contract.exe"
    includes = (
        "scripts/tests/cody_native",
        "cody-device/components/CODY_companion/include",
        "cody-device/components/LUNASHELL_platform/include",
    )
    sources = ("scripts/tests/cody_native/usage_alarm_contract.cpp",)
    command = compile_command(compiler, includes, sources, executable, cxx_standard=20)
    run_stage(command, output, "compile")
    result = run_stage([str(executable)], output, "usage-alarm")
    print(result.stdout, end="")
    print(f"Host check artifacts: {output}")


if __name__ == "__main__":
    main()
