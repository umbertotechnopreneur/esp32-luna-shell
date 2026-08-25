#!/usr/bin/env python3
"""Check deterministic firmware size and build-graph resource budgets.

The checker consumes only versioned policy plus artifacts emitted by a normal
ESP-IDF build.  The per-change guard compares against the reviewed baseline in
that policy, so it needs neither Git history nor a network fetch of a base build.
It never invokes ESP-IDF itself.  CI must create the artifacts through
scripts/device.ps1 so the repository's build contract remains intact.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
from typing import Any, Mapping, Sequence


DEFAULT_CONFIG = Path(__file__).with_name("resource_budgets.json")
SUPPORTED_METRICS = {
    "app_bin_bytes": ("Application binary", "bytes"),
    "linked_image_bytes": ("Linked image", "bytes"),
    "diram_bytes": ("Static DIRAM", "bytes"),
    "translation_units": ("Build translation units", "count"),
    "managed_translation_units": ("Managed-component translation units", "count"),
}
REQUIRED_SIZE_LAYOUT = ("Flash Code", "Flash Data", "DIRAM")


class ResourceBudgetInputError(ValueError):
    """Raised when a build artifact or budget policy is missing or malformed."""


def _load_json(path: Path, label: str) -> Any:
    try:
        with path.open("r", encoding="utf-8-sig") as stream:
            return json.load(stream)
    except FileNotFoundError as error:
        raise ResourceBudgetInputError(f"{label} not found: {path}") from error
    except json.JSONDecodeError as error:
        raise ResourceBudgetInputError(
            f"{label} is not valid JSON at line {error.lineno}, "
            f"column {error.colno}: {path}"
        ) from error
    except OSError as error:
        raise ResourceBudgetInputError(f"cannot read {label} {path}: {error}") from error


def _mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ResourceBudgetInputError(f"{label} must be a JSON object")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ResourceBudgetInputError(f"{label} must be a non-empty string")
    return value


def _nonnegative_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ResourceBudgetInputError(f"{label} must be a non-negative integer")
    return value


def _resolve_artifact(build_dir: Path, value: Any, label: str) -> Path:
    raw_path = Path(_string(value, label))
    candidate = raw_path if raw_path.is_absolute() else build_dir / raw_path
    resolved = candidate.resolve(strict=False)
    try:
        resolved.relative_to(build_dir)
    except ValueError as error:
        raise ResourceBudgetInputError(
            f"{label} escapes the selected build directory: {resolved}"
        ) from error
    if not resolved.is_file():
        raise ResourceBudgetInputError(f"{label} not found: {resolved}")
    return resolved


def _resolve_source(entry: Mapping[str, Any], index: int, build_dir: Path) -> Path:
    raw_file = Path(_string(entry.get("file"), f"compile command {index}.file"))
    if raw_file.is_absolute():
        return raw_file.resolve(strict=False)

    raw_directory = entry.get("directory")
    if raw_directory is None:
        base = build_dir
    else:
        directory = Path(
            _string(raw_directory, f"compile command {index}.directory")
        )
        base = directory if directory.is_absolute() else build_dir / directory
    return (base / raw_file).resolve(strict=False)


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def load_policy(config_path: Path) -> Mapping[str, Any]:
    policy = _mapping(_load_json(config_path, "resource budget policy"), "policy")
    if policy.get("schema_version") != 1:
        raise ResourceBudgetInputError("policy.schema_version must be 1")

    _string(policy.get("profile"), "policy.profile")
    baseline_commit = _string(
        policy.get("baseline_commit"), "policy.baseline_commit"
    )
    if not (7 <= len(baseline_commit) <= 40) or any(
        character not in "0123456789abcdefABCDEF" for character in baseline_commit
    ):
        raise ResourceBudgetInputError(
            "policy.baseline_commit must be a 7-40 character hexadecimal Git id"
        )

    expected = _mapping(policy.get("expected"), "policy.expected")
    _string(expected.get("project_name"), "policy.expected.project_name")
    _string(expected.get("target"), "policy.expected.target")

    metrics = _mapping(policy.get("metrics"), "policy.metrics")
    missing = sorted(set(SUPPORTED_METRICS) - set(metrics))
    unknown = sorted(set(metrics) - set(SUPPORTED_METRICS))
    if missing:
        raise ResourceBudgetInputError(
            "policy.metrics is missing required metrics: " + ", ".join(missing)
        )
    if unknown:
        raise ResourceBudgetInputError(
            "policy.metrics contains unsupported metrics: " + ", ".join(unknown)
        )

    for metric_name in sorted(metrics):
        rule = _mapping(metrics[metric_name], f"policy.metrics.{metric_name}")
        baseline = _nonnegative_int(
            rule.get("baseline"), f"policy.metrics.{metric_name}.baseline"
        )
        absolute_max = _nonnegative_int(
            rule.get("absolute_max"),
            f"policy.metrics.{metric_name}.absolute_max",
        )
        warn_increase = _nonnegative_int(
            rule.get("warn_increase"),
            f"policy.metrics.{metric_name}.warn_increase",
        )
        max_increase = _nonnegative_int(
            rule.get("max_increase"),
            f"policy.metrics.{metric_name}.max_increase",
        )
        if baseline > absolute_max:
            raise ResourceBudgetInputError(
                f"policy.metrics.{metric_name}.baseline exceeds absolute_max"
            )
        if warn_increase > max_increase:
            raise ResourceBudgetInputError(
                f"policy.metrics.{metric_name}.warn_increase exceeds max_increase"
            )

    return policy


def collect_measurements(build_dir: Path, size_report_path: Path) -> dict[str, Any]:
    build_dir = build_dir.resolve(strict=False)
    if not build_dir.is_dir():
        raise ResourceBudgetInputError(f"build directory not found: {build_dir}")

    description_path = build_dir / "project_description.json"
    description = _mapping(
        _load_json(description_path, "ESP-IDF project description"),
        "project description",
    )
    project_name = _string(description.get("project_name"), "project_name")
    target = _string(description.get("target"), "target")
    if description.get("version") != "1.3":
        raise ResourceBudgetInputError(
            "project_description.json version must be 1.3 for ESP-IDF 6.0.2"
        )
    project_root = Path(
        _string(description.get("project_path"), "project_path")
    ).resolve(strict=False)
    if not project_root.is_dir():
        raise ResourceBudgetInputError(
            f"project_path from project description is not a directory: {project_root}"
        )
    app_bin = _resolve_artifact(build_dir, description.get("app_bin"), "app_bin")

    commands_path = build_dir / "compile_commands.json"
    commands = _load_json(commands_path, "compile command database")
    if not isinstance(commands, list) or not commands:
        raise ResourceBudgetInputError(
            "compile command database must be a non-empty JSON array"
        )

    managed_root = (project_root / "managed_components").resolve(strict=False)
    third_party_root = (project_root / "third_party").resolve(strict=False)
    project_components_root = (project_root / "components").resolve(strict=False)
    managed_count = 0
    third_party_count = 0
    project_component_count = 0
    for index, raw_entry in enumerate(commands):
        entry = _mapping(raw_entry, f"compile command {index}")
        if not isinstance(entry.get("command"), str) and not isinstance(
            entry.get("arguments"), list
        ):
            raise ResourceBudgetInputError(
                f"compile command {index} must contain command or arguments"
            )
        source = _resolve_source(entry, index, build_dir)
        if _is_relative_to(source, managed_root):
            managed_count += 1
        elif _is_relative_to(source, third_party_root):
            third_party_count += 1
        elif _is_relative_to(source, project_components_root):
            project_component_count += 1

    size_report = _mapping(
        _load_json(size_report_path, "ESP-IDF JSON size report"), "size report"
    )
    if size_report.get("version") != "1.2":
        raise ResourceBudgetInputError(
            "ESP-IDF JSON size report version must be 1.2 for ESP-IDF 6.0.2"
        )
    linked_image_size = _nonnegative_int(
        size_report.get("total_size"), "size report total_size"
    )
    layout = size_report.get("layout")
    if not isinstance(layout, list):
        raise ResourceBudgetInputError("size report layout must be a JSON array")

    layout_by_name: dict[str, Mapping[str, Any]] = {}
    for index, raw_entry in enumerate(layout):
        entry = _mapping(raw_entry, f"size report layout[{index}]")
        name = _string(entry.get("name"), f"size report layout[{index}].name")
        if name in layout_by_name:
            raise ResourceBudgetInputError(
                f"size report contains duplicate memory type: {name}"
            )
        layout_by_name[name] = entry

    missing_layout = [name for name in REQUIRED_SIZE_LAYOUT if name not in layout_by_name]
    if missing_layout:
        raise ResourceBudgetInputError(
            "size report is missing memory types: " + ", ".join(missing_layout)
        )

    memory_used = {
        name: _nonnegative_int(
            layout_by_name[name].get("used"), f"size report {name}.used"
        )
        for name in REQUIRED_SIZE_LAYOUT
    }
    return {
        "project_name": project_name,
        "target": target,
        "metrics": {
            "app_bin_bytes": app_bin.stat().st_size,
            "linked_image_bytes": linked_image_size,
            "diram_bytes": memory_used["DIRAM"],
            "translation_units": len(commands),
            "managed_translation_units": managed_count,
        },
        "diagnostics": {
            "flash_code_bytes": memory_used["Flash Code"],
            "flash_data_bytes": memory_used["Flash Data"],
            "third_party_translation_units": third_party_count,
            "project_component_translation_units": project_component_count,
        },
    }


def evaluate(policy: Mapping[str, Any], measurements: Mapping[str, Any]) -> dict[str, Any]:
    expected = _mapping(policy["expected"], "policy.expected")
    for field in ("project_name", "target"):
        expected_value = _string(expected[field], f"policy.expected.{field}")
        actual_value = _string(measurements.get(field), field)
        if actual_value != expected_value:
            raise ResourceBudgetInputError(
                f"build {field} is {actual_value!r}, expected {expected_value!r}"
            )

    measured_metrics = _mapping(measurements.get("metrics"), "measurements.metrics")
    policy_metrics = _mapping(policy["metrics"], "policy.metrics")
    metric_results: dict[str, Any] = {}
    errors: list[str] = []
    warnings: list[str] = []

    for metric_name in sorted(SUPPORTED_METRICS):
        label, unit = SUPPORTED_METRICS[metric_name]
        current = _nonnegative_int(measured_metrics.get(metric_name), metric_name)
        rule = _mapping(policy_metrics[metric_name], f"policy.metrics.{metric_name}")
        baseline = _nonnegative_int(rule["baseline"], f"{metric_name}.baseline")
        absolute_max = _nonnegative_int(
            rule["absolute_max"], f"{metric_name}.absolute_max"
        )
        warn_increase = _nonnegative_int(
            rule["warn_increase"], f"{metric_name}.warn_increase"
        )
        max_increase = _nonnegative_int(
            rule["max_increase"], f"{metric_name}.max_increase"
        )
        delta = current - baseline
        metric_errors: list[str] = []
        metric_warnings: list[str] = []

        if current > absolute_max:
            metric_errors.append(
                f"{label} is {_format_value(current, unit)}, above the absolute cap "
                f"of {_format_value(absolute_max, unit)}"
            )
        if delta > max_increase:
            metric_errors.append(
                f"{label} grew by {_format_value(delta, unit)} from baseline, above "
                f"the per-change guard of {_format_value(max_increase, unit)}"
            )
        elif delta > warn_increase:
            metric_warnings.append(
                f"{label} grew by {_format_value(delta, unit)} from baseline; review "
                f"growth above {_format_value(warn_increase, unit)}"
            )

        errors.extend(metric_errors)
        warnings.extend(metric_warnings)
        metric_results[metric_name] = {
            "label": label,
            "unit": unit,
            "current": current,
            "baseline": baseline,
            "delta": delta,
            "absolute_max": absolute_max,
            "warn_increase": warn_increase,
            "max_increase": max_increase,
            "status": "fail" if metric_errors else "warn" if metric_warnings else "pass",
        }

    return {
        "schema_version": 1,
        "status": "fail" if errors else "pass",
        "profile": policy["profile"],
        "baseline_commit": policy["baseline_commit"],
        "project_name": measurements["project_name"],
        "target": measurements["target"],
        "metrics": metric_results,
        "diagnostics": measurements["diagnostics"],
        "errors": errors,
        "warnings": warnings,
    }


def _format_value(value: int, unit: str) -> str:
    if unit == "bytes":
        return f"{value:,} B"
    return f"{value:,}"


def _format_delta(value: int, unit: str) -> str:
    prefix = "+" if value > 0 else ""
    return prefix + _format_value(value, unit)


def print_report(report: Mapping[str, Any]) -> None:
    status = str(report["status"]).upper()
    print(f"Firmware resource budgets: {status}")
    print(
        f"Profile: {report['profile']} | target: {report['target']} | "
        f"baseline: {report['baseline_commit']}"
    )
    metrics = _mapping(report["metrics"], "report.metrics")
    for metric_name in sorted(metrics):
        metric = _mapping(metrics[metric_name], f"report.metrics.{metric_name}")
        unit = str(metric["unit"])
        print(
            f"[{str(metric['status']).upper():4}] {metric['label']}: "
            f"{_format_value(int(metric['current']), unit)} "
            f"(baseline {_format_value(int(metric['baseline']), unit)}, "
            f"delta {_format_delta(int(metric['delta']), unit)}, "
            f"cap {_format_value(int(metric['absolute_max']), unit)}, "
            f"guard +{_format_value(int(metric['max_increase']), unit)})"
        )
    for warning in report["warnings"]:
        print(f"WARNING: {warning}")
    for error in report["errors"]:
        print(f"ERROR: {error}")


def write_json_report(path: Path, report: Mapping[str, Any]) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except OSError as error:
        raise ResourceBudgetInputError(
            f"cannot write JSON resource-budget report {path}: {error}"
        ) from error


def write_github_summary(path: Path, report: Mapping[str, Any]) -> None:
    rows = [
        "## Firmware resource budgets",
        "",
        f"Status: **{str(report['status']).upper()}**  ",
        f"Profile: `{report['profile']}` · Target: `{report['target']}` · "
        f"Baseline: `{report['baseline_commit']}`",
        "",
        "| Metric | Current | Baseline | Delta | Absolute cap | Change guard | Status |",
        "| --- | ---: | ---: | ---: | ---: | ---: | :---: |",
    ]
    metrics = _mapping(report["metrics"], "report.metrics")
    for metric_name in sorted(metrics):
        metric = _mapping(metrics[metric_name], f"report.metrics.{metric_name}")
        unit = str(metric["unit"])
        rows.append(
            f"| {metric['label']} | {_format_value(int(metric['current']), unit)} | "
            f"{_format_value(int(metric['baseline']), unit)} | "
            f"{_format_delta(int(metric['delta']), unit)} | "
            f"{_format_value(int(metric['absolute_max']), unit)} | "
            f"+{_format_value(int(metric['max_increase']), unit)} | "
            f"{str(metric['status']).upper()} |"
        )
    if report["warnings"]:
        rows.extend(("", "Warnings:", ""))
        rows.extend(f"- {warning}" for warning in report["warnings"])
    if report["errors"]:
        rows.extend(("", "Errors:", ""))
        rows.extend(f"- {error}" for error in report["errors"])
    try:
        with path.open("a", encoding="utf-8") as stream:
            stream.write("\n".join(rows) + "\n")
    except OSError as error:
        raise ResourceBudgetInputError(
            f"cannot write GitHub step summary {path}: {error}"
        ) from error


def _github_escape(message: str) -> str:
    return message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def emit_github_annotations(report: Mapping[str, Any]) -> None:
    if os.environ.get("GITHUB_ACTIONS", "").casefold() != "true":
        return
    for warning in report["warnings"]:
        print(
            "::warning title=Firmware resource budget::"
            + _github_escape(str(warning))
        )
    for error in report["errors"]:
        print(
            "::error title=Firmware resource budget::" + _github_escape(str(error))
        )


def run_check(
    build_dir: Path,
    size_report_path: Path,
    config_path: Path = DEFAULT_CONFIG,
    json_report_path: Path | None = None,
    github_summary_path: Path | None = None,
) -> dict[str, Any]:
    policy = load_policy(config_path.resolve(strict=False))
    measurements = collect_measurements(
        build_dir.resolve(strict=False), size_report_path.resolve(strict=False)
    )
    report = evaluate(policy, measurements)
    print_report(report)
    emit_github_annotations(report)
    if json_report_path is not None:
        write_json_report(json_report_path.resolve(strict=False), report)
    if github_summary_path is not None:
        write_github_summary(github_summary_path.resolve(strict=False), report)
    return report


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate firmware size and translation-unit budgets from existing "
            "ESP-IDF build artifacts."
        )
    )
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--size-report",
        type=Path,
        help=(
            "ESP-IDF json2 size output. Defaults to "
            "<build-dir>/esp-idf-size.json."
        ),
    )
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--json-report", type=Path)
    parser.add_argument("--github-summary", type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    size_report = args.size_report or args.build_dir / "esp-idf-size.json"
    try:
        report = run_check(
            args.build_dir,
            size_report,
            args.config,
            args.json_report,
            args.github_summary,
        )
    except ResourceBudgetInputError as error:
        print(f"Firmware resource budgets: INPUT ERROR\nERROR: {error}", file=sys.stderr)
        return 2
    return 1 if report["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
