"""Exercise real wake reconnect and startup grace helpers with host boundaries."""
import argparse
import json
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.common.cody_native_checks import compile_command, resolve_compiler, run_stage
from scripts.checks.verify_power_policy import function_text


# path: Artifact-owned report destination.
# report: Complete current result, including any failed stage.
# Exceptions: Original filesystem and serialization failures propagate.
def write_report(path, report):
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


# No parameters: options are forwarded by the public device CLI.
# Exceptions: Extraction, compiler and fixture errors propagate unchanged.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/checks/wake-reconnect")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "artifacts"):
        raise ValueError("Wake reconnect output must remain under artifacts/")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    report = {"result": "running", "stage": "extract", "failed_stage": None,
              "scope": "real reconnect/startup/presentation helpers; fake RTOS and renderer boundaries; no radio, authentication, pixels or physical latency proof"}
    write_report(report_path, report)
    try:
        network = ROOT / "components/LUNASHELL_platform/src/network"
        groups = {
            "wifi": ("LUNASHELL_wifi.cpp", ("esp_err_t schedule_reconnect(",
                      "bool reconnect_deadline_pending_locked()",
                      "void reconnect_timer_callback(",
                      'extern "C" esp_err_t LUNASHELL_wifi_expedite_reconnect(void)')),
            "ccp": ("LUNASHELL_codex_link.cpp", ("bool wifiOnline(",
                     "bool networkReadyAdmittedLocked()", "void consumeNetworkReadyLocked()",
                     'extern "C" esp_err_t LUNASHELL_codex_link_notify_network_ready(void)')),
        }
        for name, (filename, signatures) in groups.items():
            source = (network / filename).read_text(encoding="utf-8")
            helpers = [function_text(source, signature, filename) for signature in signatures]
            # These event-driven entry points must never acquire their own polling loop.
            for helper in helpers:
                if any(token in helper for token in ("vTaskDelay(", "ulTaskNotifyTake(", "while (", "for (")):
                    raise AssertionError(f"Unexpected polling in {name} wake admission helper")
            (output / f"wake_{name}_under_test.inc").write_text("\n\n".join(helpers) + "\n", encoding="utf-8")
        onboarding_path = ROOT / "components/CODY_companion/src/scenes/CODY_onboarding.cpp"
        onboarding = onboarding_path.read_text(encoding="utf-8")
        declarations = []
        for name in ("kStartupWifiGraceUs", "s_startup_wifi_wait_started_us",
                     "s_startup_network_passive", "s_startup_network_presented"):
            declaration = re.search(rf"^(?:constexpr int64_t|int64_t|bool) {name}\s*=\s*[^;]+;", onboarding, re.MULTILINE)
            if declaration is None:
                raise AssertionError(f"Production startup declaration missing: {name}")
            declarations.append(declaration.group(0))
        signatures = ("void request_saved_setup_startup()", "bool saved_wifi_startup_grace(",
                      "esp_err_t present_network_wait(")
        startup_helpers = [function_text(onboarding, signature, str(onboarding_path))
                           for signature in signatures]
        # Locate the definition explicitly; this function also has a forward declaration.
        evaluate_signature = "esp_err_t evaluate_saved_setup(SceneFrame &scene, bool &changed)"
        startup_helpers.append(function_text(
            onboarding[onboarding.index(evaluate_signature + "\n{"):],
            evaluate_signature, str(onboarding_path)))
        # Compile the changed route branches verbatim; only lower renderer calls
        # are fake. Verify their real owner clears old action identities first.
        route_body = function_text(onboarding, "esp_err_t enter_route(", str(onboarding_path))
        passive_signature = "if (route == Route::NetworkWait && s_startup_network_passive)"
        recovery_signature = "if (route == Route::CodexChoice || route == Route::SetupMenu || route == Route::NetworkWait)"
        if not (route_body.index("wipe(&s_view, sizeof(s_view));") < route_body.index(passive_signature)
                < route_body.index(recovery_signature)):
            raise AssertionError("Startup passive route must follow cleared controls and precede recovery actions")
        route_branches = (function_text(route_body, passive_signature, str(onboarding_path)) + " else " +
                          function_text(route_body, recovery_signature, str(onboarding_path)))
        (output / "startup_route_controls_under_test.inc").write_text(route_branches + "\n", encoding="utf-8")
        reset_body = function_text(onboarding, "void reset_onboarding()", str(onboarding_path))
        for reset_statement in ("s_startup_wifi_wait_started_us = -1;", "s_startup_network_passive = false;",
                                "s_startup_network_presented = false;"):
            if reset_statement not in reset_body:
                raise AssertionError(f"Full onboarding reset lost grace reset: {reset_statement}")
        (output / "startup_network_under_test.inc").write_text(
            "\n\n".join(declarations + startup_helpers) + "\n", encoding="utf-8")
        report["stage"] = "compiler-discovery"
        compiler = resolve_compiler(args.compiler)
        report["compiler"] = compiler
        report["stage"] = "compile"
        write_report(report_path, report)
        executable = output / "wake-reconnect.exe"
        command = compile_command(compiler, [str(output), "scripts/tests/cody_native"],
                                  ["scripts/tests/wake_reconnect_contract.cpp"], executable)
        run_stage(command, output, "compile")
        report["stage"] = "admission-and-deadlines"
        write_report(report_path, report)
        run_stage([str(executable)], output, "contract")
        report["stage"] = "compile-startup-grace"
        write_report(report_path, report)
        startup_executable = output / "startup-network-grace.exe"
        command = compile_command(compiler, [str(output), "scripts/tests/cody_native"],
                                  ["scripts/tests/startup_network_grace_contract.cpp"], startup_executable)
        run_stage(command, output, "compile-startup-grace")
        report["stage"] = "startup-grace"
        write_report(report_path, report)
        run_stage([str(startup_executable)], output, "startup-grace")
        report.update(result="passed", stage="complete")
        write_report(report_path, report)
    except BaseException as error:
        report.update(result="failed", failed_stage=report["stage"], error=f"{type(error).__name__}: {error}")
        try:
            write_report(report_path, report)
        except Exception as report_error:
            print(f"Could not save failed wake reconnect report: {report_error}", file=sys.stderr)
        raise
    print(f"Wake reconnect host contracts passed; {report_path}")


if __name__ == "__main__":
    main()
