"""Compile and exercise the real battery endurance service with bounded host fakes."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# No parameters: the public device CLI forwards optional compiler/output arguments.
# Exceptions: Compiler, fixture and filesystem failures propagate as failed checks.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/checks/battery-endurance")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "artifacts"):
        raise ValueError("Check output must remain under artifacts/")
    output.mkdir(parents=True, exist_ok=True)
    compiler = resolve_compiler(args.compiler)
    executable = output / "battery_endurance.exe"
    run_stage(compile_command(compiler,
        ["scripts/tests/battery_endurance_stubs", "components/LUNASHELL_platform/include"],
        ["scripts/tests/battery_endurance_contract.cpp",
         "components/LUNASHELL_platform/src/power/LUNASHELL_battery_endurance.cpp"], executable),
        output, "compile")
    scenarios = ["usb", "unknown", "heartbeat", "wake", "shutdown", "write-error",
                 "clock-unavailable", "seed", "reboot", "crc", "type", "oversize"]
    for scenario in scenarios:
        run_stage([str(executable), scenario, str(output / "journal.bin")], output, scenario)
    (output / "report.json").write_text(json.dumps({
        "implementation": "LUNASHELL_battery_endurance.cpp",
        "scenarios": scenarios, "result": "passed",
        "scope": "host fakes; no physical battery endurance claim",
    }, indent=2), encoding="utf-8")
    print(f"Battery endurance real-service scenarios passed: {len(scenarios)}; {output}")


if __name__ == "__main__":
    main()
