#!/usr/bin/env python3
"""Focused host-only tests for the firmware resource-budget checker and CI contract."""

from __future__ import annotations

import copy
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest

import check_resource_budgets as budgets


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = (
    REPOSITORY_ROOT / ".github" / "workflows" / "firmware-resource-budgets.yml"
)


class ResourceBudgetFixture:
    def __init__(self, root: Path) -> None:
        self.project = root / "project"
        self.build = self.project / "build"
        self.managed_source = (
            self.project / "managed_components" / "vendor__library" / "managed.c"
        )
        self.local_source = self.project / "components" / "local" / "local.cpp"
        self.third_party_source = self.project / "third_party" / "library" / "port.S"
        for source in (
            self.managed_source,
            self.local_source,
            self.third_party_source,
        ):
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("/* fixture */\n", encoding="utf-8")
        self.build.mkdir(parents=True)
        (self.build / "esp32_luna_shell.bin").write_bytes(bytes(120))
        self._write_json(
            self.build / "project_description.json",
            {
                "version": "1.3",
                "project_name": "esp32_luna_shell",
                "project_path": str(self.project),
                "app_bin": "esp32_luna_shell.bin",
                "target": "esp32s3",
            },
        )
        self._write_json(
            self.build / "compile_commands.json",
            [
                {
                    "directory": str(self.build),
                    "file": str(self.managed_source),
                    "command": "cc managed.c",
                },
                {
                    "directory": str(self.local_source.parent),
                    "file": "local.cpp",
                    "command": "c++ local.cpp",
                },
                {
                    "directory": str(self.build),
                    "file": str(self.third_party_source),
                    "command": "cc port.S",
                },
            ],
        )
        self.size_report = self.build / "esp-idf-size.json"
        self._write_json(
            self.size_report,
            {
                "version": "1.2",
                "total_size": 100,
                "layout": [
                    {"name": "Flash Code", "used": 60, "total": 0, "parts": {}},
                    {"name": "Flash Data", "used": 30, "total": 0, "parts": {}},
                    {"name": "DIRAM", "used": 20, "total": 100, "parts": {}},
                ],
            },
        )
        self.policy = {
            "schema_version": 1,
            "profile": "test",
            "baseline_commit": "0123456789abcdef",
            "expected": {
                "project_name": "esp32_luna_shell",
                "target": "esp32s3",
            },
            "metrics": {
                "app_bin_bytes": self._rule(120, 200, 10, 20),
                "linked_image_bytes": self._rule(100, 200, 10, 20),
                "diram_bytes": self._rule(20, 100, 5, 10),
                "translation_units": self._rule(3, 10, 1, 2),
                "managed_translation_units": self._rule(1, 5, 1, 2),
            },
        }

    @staticmethod
    def _rule(
        baseline: int, absolute_max: int, warn_increase: int, max_increase: int
    ) -> dict[str, int]:
        return {
            "baseline": baseline,
            "absolute_max": absolute_max,
            "warn_increase": warn_increase,
            "max_increase": max_increase,
        }

    @staticmethod
    def _write_json(path: Path, value: object) -> None:
        path.write_text(json.dumps(value, indent=2), encoding="utf-8")


class ResourceBudgetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = ResourceBudgetFixture(Path(self.temporary.name))

    def test_collects_size_and_build_graph_metrics(self) -> None:
        measurements = budgets.collect_measurements(
            self.fixture.build, self.fixture.size_report
        )
        self.assertEqual(
            measurements["metrics"],
            {
                "app_bin_bytes": 120,
                "linked_image_bytes": 100,
                "diram_bytes": 20,
                "translation_units": 3,
                "managed_translation_units": 1,
            },
        )
        self.assertEqual(measurements["diagnostics"]["flash_code_bytes"], 60)
        self.assertEqual(
            measurements["diagnostics"]["third_party_translation_units"], 1
        )

    def test_matching_baseline_passes(self) -> None:
        measurements = budgets.collect_measurements(
            self.fixture.build, self.fixture.size_report
        )
        report = budgets.evaluate(self.fixture.policy, measurements)
        self.assertEqual(report["status"], "pass")
        self.assertFalse(report["errors"])
        self.assertFalse(report["warnings"])

    def test_absolute_cap_fails_independently_of_growth_guard(self) -> None:
        policy = copy.deepcopy(self.fixture.policy)
        rule = policy["metrics"]["app_bin_bytes"]
        rule["absolute_max"] = 119
        rule["baseline"] = 119
        measurements = budgets.collect_measurements(
            self.fixture.build, self.fixture.size_report
        )
        report = budgets.evaluate(policy, measurements)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("absolute cap" in error for error in report["errors"]))

    def test_per_change_guard_fails_below_absolute_cap(self) -> None:
        policy = copy.deepcopy(self.fixture.policy)
        rule = policy["metrics"]["app_bin_bytes"]
        rule["baseline"] = 100
        rule["absolute_max"] = 1_000
        rule["warn_increase"] = 5
        rule["max_increase"] = 10
        measurements = budgets.collect_measurements(
            self.fixture.build, self.fixture.size_report
        )
        report = budgets.evaluate(policy, measurements)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(
            any("per-change guard" in error for error in report["errors"])
        )
        self.assertFalse(any("absolute cap" in error for error in report["errors"]))

    def test_warning_does_not_fail_the_check(self) -> None:
        policy = copy.deepcopy(self.fixture.policy)
        rule = policy["metrics"]["app_bin_bytes"]
        rule["baseline"] = 110
        rule["warn_increase"] = 5
        rule["max_increase"] = 20
        measurements = budgets.collect_measurements(
            self.fixture.build, self.fixture.size_report
        )
        report = budgets.evaluate(policy, measurements)
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["metrics"]["app_bin_bytes"]["status"], "warn")
        self.assertTrue(report["warnings"])

    def test_missing_memory_type_fails_closed(self) -> None:
        size_report = json.loads(self.fixture.size_report.read_text(encoding="utf-8"))
        size_report["layout"] = [
            entry for entry in size_report["layout"] if entry["name"] != "DIRAM"
        ]
        self.fixture.size_report.write_text(
            json.dumps(size_report), encoding="utf-8"
        )
        with self.assertRaisesRegex(
            budgets.ResourceBudgetInputError, "missing memory types: DIRAM"
        ):
            budgets.collect_measurements(
                self.fixture.build, self.fixture.size_report
            )

    def test_run_check_writes_deterministic_reports(self) -> None:
        policy_path = self.fixture.project / "resource_budgets.json"
        json_report = self.fixture.build / "resource-budget-report.json"
        summary = self.fixture.build / "summary.md"
        policy_path.write_text(
            json.dumps(self.fixture.policy, indent=2), encoding="utf-8"
        )
        with redirect_stdout(io.StringIO()):
            report = budgets.run_check(
                self.fixture.build,
                self.fixture.size_report,
                policy_path,
                json_report,
                summary,
            )
        self.assertEqual(json.loads(json_report.read_text(encoding="utf-8")), report)
        self.assertIn("Status: **PASS**", summary.read_text(encoding="utf-8"))
        self.assertNotIn("timestamp", json_report.read_text(encoding="utf-8").casefold())

    def test_repository_policy_is_valid(self) -> None:
        policy = budgets.load_policy(budgets.DEFAULT_CONFIG)
        self.assertEqual(policy["expected"]["target"], "esp32s3")
        self.assertEqual(policy["metrics"]["translation_units"]["baseline"], 1804)

    def test_workflow_routes_all_idf_operations_through_device_wrapper(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertNotIn("\t", workflow)
        self.assertNotIn("idf.py", workflow.casefold())
        for action in ("set-target", "build", "size"):
            self.assertIn(f"Invoke-DeviceAction {action}", workflow)
        self.assertIn('"scripts/device.ps1"', workflow)
        self.assertIn("-Jobs 8", workflow)
        self.assertIn("-SizeOutputFormat", workflow)
        self.assertIn("-SizeOutputFile", workflow)
        self.assertIn("json2", workflow)
        self.assertIn("scripts/checks/check_resource_budgets.py", workflow)
        self.assertIn("ref: v6.0.2", workflow)
        self.assertIn("contents: read", workflow)
        self.assertNotIn("shell: pwsh\n", workflow)
        self.assertEqual(workflow.count("shell: pwsh -NoProfile -File {0}"), 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
