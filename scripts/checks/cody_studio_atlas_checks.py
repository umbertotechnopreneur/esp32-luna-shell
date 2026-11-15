"""Atlas publication contracts and optional real native suite checks.

Loaded by ``device.ps1 check -Check cody-studio -ToolArguments '--atlas'``.
Synthetic fixtures are contract evidence only; AtlasNativeTests use device C++.
"""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from uuid import uuid4

import cody_studio_atlas as atlas
from cody_native_runtime import ROOT
from cody_studio_catalog import entry_for
from cody_studio_runner import load_capture


# path: New fixture destination beneath artifacts, never a source checkout file.
# value: Retained JSON fixture representing an atlas request, manifest or pointer.
# Raises filesystem/serialization failures without removing any existing artifacts.
def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


# job: Actual isolated native atlas request owned by this test.
# Raises its original native/process error or a bounded test deadline failure.
def wait_job(job):
    events = []
    deadline = time.monotonic() + 240
    while not job.done and time.monotonic() < deadline:
        events.extend(job.poll())
        if not job.done:
            time.sleep(.02)
    if not job.done:
        job.cancel()
        raise TimeoutError("Atlas check exceeded 240 seconds")
    if job.error:
        raise AssertionError(job.error)
    return events


class AtlasContractTests(unittest.TestCase):
    """Reject invalid selections, partial results and stale build publications."""

    # self: Isolated retained fixture and publication root under repository artifacts.
    # Raises fixture setup failures; no native library is loaded.
    def setUp(self):
        self.directory = ROOT / "artifacts/simulator/studio-checks/atlas" / uuid4().hex
        self.directory.mkdir(parents=True)
        self.root_patch = patch.object(atlas, "ATLAS_ROOT", self.directory)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    # self: Synthetic fixture owner; this never claims device-rendering evidence.
    # Returns an internally consistent manifest and its complete native-shaped receipt.
    # Raises fixture validation/filesystem errors.
    def fixture(self):
        from verify_cody_studio import capture_fixture
        entry = entry_for("P04")
        record = capture_fixture(overrides={"symbol": entry["symbol"]})
        case = {key: deepcopy(record.metadata[key]) for key in
                ("id", "inventory_id", "variant", "scenario_revision", "source", "symbol", "rendering_mode")}
        case.update(title=entry["title"], acquisition="captured", reason=None,
                    capture=record.metadata_path.relative_to(ROOT).as_posix(),
                    metadata_sha256=hashlib.sha256(record.metadata_path.read_bytes()).hexdigest())
        identity = uuid4().hex
        manifest = {"schema": atlas.SCHEMA, "run_id": identity, "suite": "pilot", "status": "complete",
                    "build": record.metadata["build"], "inventory": entry["inventory"], "cases": [case],
                    "auto": False, "contract_inputs": atlas.contract_inputs([entry]), "selection": [entry["id"]]}
        path = self.directory / "runs" / identity / "manifest.json"
        write_json(path, manifest)
        write_json(path.parent / "request.json", {"schema": "cody.studio.atlas-request.v1", "suite": "pilot",
                   "identities": manifest["selection"], "build": manifest["build"], "auto": False,
                   "contract_inputs": manifest["contract_inputs"]})
        return path, manifest, record

    # self: Selection contracts reject ambiguity before process launch.
    # Raises AssertionError if a duplicate alias, unknown ID or empty suite is accepted.
    def test_invalid_and_duplicate_selections_are_rejected_before_spawn(self):
        for identities in ([], ["NO-SUCH-SCREEN"], ["P04", "ui-v1:display"]):
            with self.subTest(identities=identities), patch.object(atlas.subprocess, "Popen") as spawn:
                with self.assertRaises(ValueError):
                    atlas.AtlasJob.start(identities)
                spawn.assert_not_called()
        self.assertEqual(len(atlas.selected_entries(suite="pilot")), 10)
        self.assertGreaterEqual(len(atlas.selected_entries(suite="supported")), 15)
        with self.assertRaises(ValueError):
            atlas.selected_entries(suite="unknown")

    # self: Publication uses a verified complete manifest and preserves its prior bytes.
    # Raises AssertionError if a failed/stale replacement changes the last-valid pointer.
    def test_failed_and_stale_results_preserve_last_valid_publication(self):
        path, manifest, _ = self.fixture()
        with patch.object(atlas, "verify_current_build", return_value=manifest["build"]):
            atlas.publish_atlas(path, manifest["build"])
        pointer = (self.directory / "last-valid.json").read_bytes()
        failed = deepcopy(manifest)
        failed["status"] = "failed"
        failed_path = path.with_name("failed.json")
        write_json(failed_path, failed)
        with self.assertRaisesRegex(ValueError, "previous result preserved"):
            atlas.publish_atlas(failed_path, manifest["build"])
        changed = deepcopy(manifest["build"])
        changed["library_sha256"] = "1" * 64
        with patch.object(atlas, "verify_current_build", return_value=changed):
            with self.assertRaisesRegex(ValueError, "build changed"):
                atlas.publish_atlas(path, manifest["build"])
        self.assertEqual((self.directory / "last-valid.json").read_bytes(), pointer)
        self.assertEqual(atlas.latest_atlas_path(), path)

    # self: A successful-looking result must cover every requested identity exactly once.
    # Raises AssertionError if shortened selections or modified contracts can be published.
    def test_publication_is_bound_to_original_selection_and_contracts(self):
        path, manifest, _ = self.fixture()
        request_path = path.parent / "request.json"
        request = json.loads(request_path.read_text())
        request["identities"].append(entry_for("P01")["id"])
        write_json(request_path, request)
        with patch.object(atlas, "verify_current_build", return_value=manifest["build"]):
            with self.assertRaisesRegex(ValueError, "original request"):
                atlas.publish_atlas(path, manifest["build"])
        self.assertFalse((self.directory / "last-valid.json").exists())
        invalid = deepcopy(manifest)
        invalid["selection"].append(entry_for("P01")["id"])
        write_json(path, invalid)
        with self.assertRaisesRegex(ValueError, "declared selection"):
            atlas.load_atlas(path)
        with self.assertRaisesRegex(ValueError, "catalog/scenario changed"):
            atlas.verify_contract_inputs({"docs/cody/screen-catalog.md": "0" * 64})

    # self: Immutable capture receipts are authoritative for atlas case identity.
    # Raises AssertionError when a forged identity, metadata or absent-screen reason is accepted.
    def test_capture_hash_identity_and_absence_validation(self):
        path, manifest, _ = self.fixture()
        self.assertEqual(atlas.load_atlas(path)["status"], "complete")
        for field, value in (("metadata_sha256", "1" * 64), ("variant", "invented"),
                             ("id", "ui-v1:no-such-screen")):
            broken = deepcopy(manifest)
            broken["cases"][0][field] = value
            write_json(path, broken)
            with self.subTest(field=field), self.assertRaises(ValueError):
                atlas.load_atlas(path)
        broken = deepcopy(manifest)
        broken["cases"][0].update(acquisition="unsupported", capture=None, reason="")
        write_json(path, broken)
        with self.assertRaisesRegex(ValueError, "reason"):
            atlas.load_atlas(path)

    # self: Historical source evidence can be opened after a new build is selected.
    # Raises AssertionError if merely reading history asks for current source validity.
    def test_historical_result_does_not_claim_current_build(self):
        path, manifest, _ = self.fixture()
        with patch.object(atlas, "verify_current_build", side_effect=ValueError("stale source")) as verify:
            self.assertEqual(atlas.load_atlas(path)["build"], manifest["build"])
            verify.assert_not_called()

    # self: Build-watcher state is scoped to manifest/library files only.
    # Raises AssertionError if atlas outputs trigger work or incomplete builds are adopted.
    def test_build_watcher_ignores_outputs_and_rejects_invalid_rebuilds(self):
        library = self.directory / "native" / "library.so"
        library.parent.mkdir()
        library.write_bytes(b"one")
        (library.parent / "manifest.json").write_text("one")
        old, new = {"library_sha256": "old"}, {"library_sha256": "new"}
        with patch.object(atlas, "simulator_library_path", return_value=library), patch.object(
                atlas, "verify_current_build", return_value=old) as verify:
            watcher = atlas.BuildWatcher()
            self.assertIsNone(watcher.poll())
            (self.directory / "atlas-output.png").write_bytes(b"unrelated")
            self.assertIsNone(watcher.poll())
            self.assertEqual(verify.call_count, 1)
            library.write_bytes(b"rebuilt")
            verify.side_effect = ValueError("incomplete native build")
            self.assertIsNone(watcher.poll())
            self.assertIn("incomplete", watcher.error)
            verify.side_effect, verify.return_value = None, new
            self.assertEqual(watcher.poll(), new)
            self.assertIsNone(watcher.error)
            self.assertIsNone(watcher.poll())

    # self: A suite proceeds after a case error but cannot publish the failed result.
    # Raises AssertionError if later cases disappear or failed cases become valid captures.
    def test_worker_retains_per_case_failure_and_continues(self):
        _, fixture, record = self.fixture()
        directory = self.directory / "runs" / uuid4().hex
        directory.mkdir()
        write_json(directory / "request.json", {"schema": "cody.studio.atlas-request.v1", "suite": "pilot",
                   "identities": [entry_for("P01")["id"], entry_for("P04")["id"]],
                   "build": fixture["build"], "auto": False,
                   "contract_inputs": atlas.contract_inputs([entry_for("P01"), entry_for("P04")])})

        # scenario: Selected static request; the fake owner fails only P01.
        # Raises the precise injected error for the first case.
        def prepare(scenario):
            if scenario["context"]["target"] == entry_for("P01")["id"]:
                raise RuntimeError("injected owner failure")

        device = SimpleNamespace(start=lambda: None)
        runner = SimpleNamespace(availability=lambda entry: (True, "runtime-full"),
                                 prepare=prepare, capture=lambda: record)
        with patch.dict(sys.modules, {"cody_native_device": SimpleNamespace(NativeDevice=lambda *args, **kwargs: device)}), patch(
                "cody_studio_runner.StudioRunner", return_value=runner), patch.object(
                atlas, "verify_current_build", return_value=fixture["build"]):
            atlas.run_worker(directory)
        result = atlas.load_atlas(directory / "manifest.json")
        self.assertEqual(result["status"], "failed")
        self.assertEqual([case["acquisition"] for case in result["cases"]], ["failed", "captured"])
        self.assertIn("injected owner failure", result["cases"][0]["reason"])
        self.assertFalse((self.directory / "last-valid.json").exists())

    # self: A malformed progress stream is not recursively parsed during cancellation.
    # Raises AssertionError if the child remains running or the error loses its cause.
    def test_malformed_progress_reaps_process_without_recursive_poll(self):
        directory = self.directory / "runs" / uuid4().hex
        directory.mkdir(parents=True)
        (directory / "events.jsonl").write_text("{malformed}\n")
        process = SimpleNamespace(poll=lambda: 0, wait=lambda **kwargs: 0, returncode=0)
        job = atlas.AtlasJob(directory, process, {}, [])
        events = job.poll()
        self.assertTrue(job.done)
        self.assertIsNotNone(job.error)
        self.assertEqual(events[-1]["type"], "finished")


class AtlasNativeTests(unittest.TestCase):
    """Use fresh real C++ runtimes without disturbing an interactive session."""

    # self: Preserve the user's publication pointer across real test captures.
    # Raises state read/write errors; immutable test captures are retained.
    def setUp(self):
        pointer = atlas.ATLAS_ROOT / "last-valid.json"
        self.pointer_before = pointer.read_bytes() if pointer.exists() else None
        self.addCleanup(self.restore_pointer)

    # self: Restore only the previous pointer, never remove generated capture artifacts.
    # Raises filesystem errors if the publication state cannot be restored.
    def restore_pointer(self):
        pointer = atlas.ATLAS_ROOT / "last-valid.json"
        if self.pointer_before is None:
            if pointer.exists():
                pointer.unlink()
        else:
            temporary = pointer.with_name("restore-" + uuid4().hex + ".tmp")
            temporary.write_bytes(self.pointer_before)
            temporary.replace(pointer)

    # self: Real pilot and extended native suites in separately loaded processes.
    # Raises AssertionError/native errors for lost cases, nonrepeatable pixels or baseline writes.
    def test_full_pilot_and_supported_suites_preserve_baselines_and_repeat_pixels(self):
        baseline_root = ROOT / "artifacts/simulator/studio/baselines"
        before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in baseline_root.rglob("*") if path.is_file()}
        first = atlas.AtlasJob.start(suite="pilot")
        first_events = wait_job(first)
        pilot = atlas.load_atlas(first.result)
        self.assertEqual(len(pilot["cases"]), 10)
        self.assertTrue(all(case["acquisition"] == "captured" for case in pilot["cases"]))
        self.assertEqual(sum(event["type"] == "case-result" for event in first_events), 10)
        second = atlas.AtlasJob.start(suite="supported")
        wait_job(second)
        supported = atlas.load_atlas(second.result)
        self.assertGreaterEqual(len(supported["cases"]), 15)
        self.assertNotIn("failed", {case["acquisition"] for case in supported["cases"]})
        self.assertEqual(atlas.latest_atlas_path(), second.result)
        first_display = next(case for case in pilot["cases"] if case["inventory_id"] == "P04")
        second_display = next(case for case in supported["cases"] if case["inventory_id"] == "P04")
        left, right = [load_capture(ROOT / case["capture"]).metadata for case in (first_display, second_display)]
        self.assertEqual(left["pixel_sha256"], right["pixel_sha256"])
        self.assertEqual(left["scenario_revision"], right["scenario_revision"])
        after = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in baseline_root.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        self.assertNotEqual(first.directory, second.directory)
        self.assertNotEqual((first.directory / "session.nvs"), (second.directory / "session.nvs"))

    # self: Immediate cancellation is observed and only this suite child is reaped.
    # Raises AssertionError if cancellation changes publication or leaks the process.
    def test_cancelled_suite_reaps_its_child_and_preserves_publication(self):
        pointer = atlas.ATLAS_ROOT / "last-valid.json"
        before = pointer.read_bytes() if pointer.exists() else None
        job = atlas.AtlasJob.start(suite="supported")
        job.cancel()
        self.assertTrue(job.done)
        self.assertIsNotNone(job.process.poll())
        self.assertIsNone(job.result)
        self.assertIn("cancelled", job.error)
        self.assertEqual(pointer.read_bytes() if pointer.exists() else None, before)
