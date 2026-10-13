"""Inspect GCC stack-usage sidecars from an authorized Cody activity build.

This rejects a task whose own compiled frame exhausts its stack. It reports
individual helper frames; nested call chains and device watermarks remain separate.
"""
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = ROOT / "artifacts"
DEFAULT_BUILD = ARTIFACTS / "build-cody-companion-wake-word-activity-debug"


# value: Existing build or report location under the repository artifacts owner.
# Exception: Rejects paths outside artifacts, including resolved link escapes.
def artifact_path(value: Path) -> Path:
    resolved = (value if value.is_absolute() else ROOT / value).resolve()
    if not resolved.is_relative_to(ARTIFACTS.resolve()) or resolved == ARTIFACTS.resolve():
        raise ValueError(f"Stack report path must remain below artifacts: {value}")
    return resolved


# build: Authorized firmware build containing compiler-produced .su sidecars.
# Exception: Rejects absent, malformed or incomplete compiler records.
def collect(build: Path) -> list[dict]:
    rows = []
    for sidecar in sorted(build.rglob("*.su")):
        for line in sidecar.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            fields = line.rsplit("\t", 2)
            if len(fields) != 3 or not fields[1].isdigit():
                raise ValueError(f"Malformed compiler stack record in {sidecar}: {line}")
            rows.append({"function": fields[0], "bytes": int(fields[1]),
                         "classification": fields[2],
                         "sidecar": sidecar.relative_to(ROOT).as_posix()})
    if not rows:
        raise ValueError("No compiler stack sidecars; rebuild with the selected stack-usage flags")
    return rows


# rows: Individual compiler records already parsed from the selected build.
# sources: Translation units contributing the named subsystem's helper frames.
# signature: Optional function/source pattern that narrows a shared UI translation unit.
def helper_frames(rows: list[dict], sources: tuple[str, ...], signature: str = "") -> dict:
    selected = [row for row in rows if any(source in row["sidecar"] for source in sources)
                and (not signature or re.search(signature, row["function"], re.IGNORECASE))]
    ordered = sorted(selected, key=lambda row: row["bytes"], reverse=True)
    return {"compiled_record_count": len(ordered), "largest_frames": ordered[:12],
            "omitted_record_count": max(0, len(ordered) - 12)}


# argv: Explicit public CLI arguments; no compilation or device access occurs here.
# Exception: Preserves file/format failures and reports incomplete task coverage.
def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--output", type=Path,
                        default=ARTIFACTS / "checks/firmware-stack-frames/report.json")
    args = parser.parse_args(argv)
    build, output = artifact_path(args.build_dir), artifact_path(args.output)
    rows = collect(build)
    # The generated header identifies the compiled profile. Stale sidecars alone
    # must not cause an AFE requirement in a profile that excludes that source.
    configuration = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    wake_word_enabled = bool(re.search(
        r"^#define CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD 1\s*$", configuration, re.MULTILINE))
    voice_input_enabled = bool(re.search(
        r"^#define CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT 1\s*$", configuration, re.MULTILINE))
    tasks = []
    limits = [("power_task", "LUNASHELL_power.cpp", 8192),
              ("diagnostic_monitor_task", "LUNASHELL_hardware.cpp", 3072),
              ("command_task", "LUNASHELL_logger_usb.c", 4096),
              ("worker", "LUNASHELL_activity.cpp", 8192),
              ("companion_worker", "CODY_companion.cpp", 12288)]
    if wake_word_enabled:
        limits.extend((("voice_feed_task", "LUNASHELL_voice_afe.cpp", 4096),
                       ("voice_fetch_task", "LUNASHELL_voice_afe.cpp", 3072)))
    failures = []
    for name, source, budget in limits:
        # GCC also embeds the enclosing worker name in lambda helper records.
        # Match the task's complete signature, rather than those nested names.
        # C sidecars contain a bare function name; C++ includes the argument type.
        matches = [row for row in rows if source in row["sidecar"] and
                   re.search(r"\b" + re.escape(name) + r"(?:\(void\*\))?$", row["function"])]
        if len(matches) != 1:
            failures.append(f"Expected one compiled {name} frame, found {len(matches)}")
            continue
        frame = matches[0]
        tasks.append(dict(frame, stack_bytes=budget))
        if frame["bytes"] >= budget or frame["classification"] == "dynamic":
            failures.append(f"{name}: unbounded or exhausting task frame")
    selected = [row for row in rows if any(source in row["sidecar"] for source in
                ("LUNASHELL_power.cpp", "LUNASHELL_battery_endurance.cpp",
                 "LUNASHELL_settings.cpp", "LUNASHELL_activity.cpp", "LUNASHELL_logger.c"))]
    helpers = {}
    endurance_usb_helpers = helper_frames(
        rows, ("LUNASHELL_battery_endurance.cpp", "LUNASHELL_logger_usb.c"),
        r"read_endurance_usb|process_command|command_task")
    if wake_word_enabled:
        helpers["afe"] = helper_frames(rows, ("LUNASHELL_voice_afe.cpp",))
    if voice_input_enabled:
        helpers["voice_input"] = helper_frames(rows, ("LUNASHELL_voice_input.cpp",))
        helpers["cody_recording_ribbon"] = helper_frames(
            rows, ("CODY_companion.cpp",), r"voice|recording|ribbon")
    report = {"status": "failed" if failures else "passed", "tasks": tasks,
              "largest_selected_frames": sorted(selected, key=lambda row: row["bytes"], reverse=True)[:40],
              "voice_helper_frames": helpers,
              "endurance_usb_helper_frames": endurance_usb_helpers,
              "helper_scope": "Up to 12 largest individual records per enabled subsystem; inlined helpers may have no separate record",
              "compiled_record_count": len(rows), "failures": failures,
              "scope": "Individual compiled frames; nested call chains and device watermarks required separately"}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for task in tasks:
        print(f"Compiled task frame: {task['function']} {task['bytes']}/{task['stack_bytes']} bytes")
    for name, group in helpers.items():
        largest = group["largest_frames"]
        maximum = str(largest[0]["bytes"]) + " bytes" if largest else "no separate compiler record"
        print(f"Individual {name} frames: {group['compiled_record_count']}; largest {maximum}")
    for frame in endurance_usb_helpers["largest_frames"]:
        print(f"Individual endurance USB frame: {frame['function']} {frame['bytes']} bytes")
    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"Individual stack frame report: {report['status']}; {output}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
