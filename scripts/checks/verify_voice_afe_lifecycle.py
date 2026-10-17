"""Exercise the production AFE lifecycle gate with bounded host FreeRTOS fakes."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.checks.verify_power_policy import function_text


# path: Artifact-owned status path; each replacement contains complete JSON.
# report: Current lifecycle harness status, including its actual failing stage.
# Exceptions: Filesystem and serialization errors propagate unchanged.
def write_report(path, report):
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


# No parameters: the public device CLI forwards compiler/output options.
# Exceptions: Original compiler, fixture, interruption and filesystem errors propagate.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Existing host C++ compiler; nothing is installed")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/checks/voice-afe-lifecycle")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "artifacts"):
        raise ValueError("AFE lifecycle output must remain under artifacts/")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    report = {"result": "running", "stage": "compiler-discovery", "failed_stage": None,
              "scope": "production lifecycle gate with host primitives; no ESP-SR, codec, scheduling or hardware proof"}
    write_report(report_path, report)
    try:
        report["stage"] = "extract-production-helpers"
        write_report(report_path, report)
        source_path = ROOT / "components/LUNASHELL_platform/src/audio/LUNASHELL_voice_afe.cpp"
        source = source_path.read_text(encoding="utf-8")
        signatures = ("void signal_lifecycle_change()", "esp_err_t notify_workers(",
                      "void wait_lifecycle_change(", "void park_finished_worker()",
                      "bool wait_worker_start(", "esp_err_t cancel_feed_start(",
                      "void park_paused_worker(", "void publish_voice(",
                      "esp_err_t rollback_afe_resume()", "esp_err_t apply_afe_standby_locked(")
        extracted = "\n\n".join(function_text(source, signature, str(source_path)) for signature in signatures)
        (output / "afe_lifecycle_under_test.inc").write_text(extracted + "\n", encoding="utf-8")
        report["production_helpers"] = list(signatures)
        report["stage"] = "compiler-discovery"
        compiler = resolve_compiler(args.compiler)
        report["compiler"] = compiler
        report["stage"] = "compile"
        write_report(report_path, report)
        executable = output / "voice-afe-lifecycle.exe"
        command = compile_command(compiler,
            [str(output), "scripts/tests/cody_native",
             "components/LUNASHELL_platform/src/audio"],
            ["scripts/tests/voice_afe_lifecycle_contract.cpp"], executable)
        if Path(compiler).stem.lower() != "cl":
            command.append("-pthread")
        run_stage(command, output, "compile")
        report["stage"] = "lifecycle"
        write_report(report_path, report)
        run_stage([str(executable)], output, "lifecycle")
        report["stage"] = "complete"
        report["result"] = "passed"
        write_report(report_path, report)
    except BaseException as error:
        report["result"] = "failed"
        report["failed_stage"] = report["stage"]
        report["error"] = f"{type(error).__name__}: {error}"
        try:
            write_report(report_path, report)
        except Exception as report_error:
            print(f"Could not save failed AFE lifecycle report: {report_error}", file=sys.stderr)
        raise
    print(f"Production AFE lifecycle host checks passed; {report_path}")


if __name__ == "__main__":
    main()
