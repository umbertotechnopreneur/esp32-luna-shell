"""Compile and exercise the real battery endurance service with bounded host fakes."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage


# path: Artifact-owned report replaced only after its complete JSON is written.
# report: Current run status, stage and completed scenarios; no hardware claims.
# Exceptions: Filesystem and serialization failures propagate to the caller.
def write_report(path, report):
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


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
    scenarios = ["usb", "unknown", "heartbeat", "wake", "shutdown", "write-error",
                 "clock-unavailable", "seed", "reboot", "crc", "type", "oversize",
                 "publication-coalescing", "publication-retry", "disabled-publication"]
    report_path = output / "report.json"
    report = {
        "implementation": "LUNASHELL_battery_endurance.cpp",
        "scenarios": scenarios, "completed_scenarios": [], "result": "running",
        "stage": "compiler-discovery", "failed_stage": None,
        "scope": "host fakes; no physical battery endurance claim",
    }
    # Replace an earlier success before any compiler or scenario can fail.
    write_report(report_path, report)
    try:
        compiler = resolve_compiler(args.compiler)
        report["compiler"] = compiler
        report["stage"] = "compile"
        write_report(report_path, report)
        executable = output / "battery_endurance.exe"
        run_stage(compile_command(compiler,
            ["scripts/tests/battery_endurance_stubs", "components/LUNASHELL_platform/include"],
            ["scripts/tests/battery_endurance_contract.cpp",
             "components/LUNASHELL_platform/src/power/LUNASHELL_battery_endurance.cpp"], executable),
            output, "compile")
        for scenario in scenarios:
            report["stage"] = scenario
            write_report(report_path, report)
            run_stage([str(executable), scenario, str(output / "journal.bin")], output, scenario)
            report["completed_scenarios"].append(scenario)
        report["stage"] = "complete"
        report["result"] = "passed"
        write_report(report_path, report)
    except BaseException as error:
        # Record interrupted runs too; report I/O must never replace the actual
        # compiler, assertion, timeout or interruption exception and exit cause.
        report["result"] = "failed"
        report["failed_stage"] = report["stage"]
        report["error"] = f"{type(error).__name__}: {error}"
        try:
            write_report(report_path, report)
        except Exception as report_error:
            print(f"Could not save failed endurance report: {report_error}", file=sys.stderr)
        raise
    print(f"Battery endurance real-service scenarios passed: {len(scenarios)}; {output}")


if __name__ == "__main__":
    main()
