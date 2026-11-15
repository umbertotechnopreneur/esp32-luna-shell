"""Phase-G native coverage checks, selected through the Studio public command."""

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import time
import unittest
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from cody_studio_atlas import AtlasJob, load_atlas
from cody_studio_catalog import load_catalog, resolve_source
from cody_studio_runner import load_capture
from cody_studio_scenario import default_scenario, validate_scenario

OUTPUT = ROOT / "artifacts/simulator/studio/coverage-checks"


# job: Isolated native suite whose completion is observed rather than assumed.
# Raises the original job error or a bounded host deadline failure.
def completed_job(job):
    deadline = time.monotonic() + 300
    try:
        while not job.done and time.monotonic() < deadline:
            job.poll()
            if not job.done:
                time.sleep(.03)
        if not job.done:
            raise TimeoutError("Native coverage suite exceeded 300 seconds")
        if job.error or job.result is None:
            raise AssertionError(job.error or "Native suite returned no manifest")
        return load_atlas(job.result)
    finally:
        if not job.done:
            job.cancel()


class CoverageContractTests(unittest.TestCase):
    """Executable contracts and explicit absences come from one maintained catalog."""

    # self: Static catalog fixture; no native library or device is loaded.
    # Raises AssertionError for incomplete mappings, invalid fixtures or guessed sources.
    def test_extended_families_have_valid_scenarios_and_current_source_symbols(self):
        catalog = load_catalog()
        extension = [entry for entry in catalog if entry.get("suite") == "extension"]
        self.assertGreaterEqual(len(extension), 20)
        self.assertEqual(sum(entry.get("suite") == "pilot" for entry in catalog), 10)
        for entry in extension:
            with self.subTest(identity=entry["id"]):
                scenario = default_scenario(entry["id"], variant=entry.get("atlas_variant"))
                self.assertEqual(scenario["context"]["target"], entry["id"])
                self.assertEqual(scenario["time"]["mode"], "controlled-static")
                source, line = resolve_source(entry)
                self.assertTrue(source.is_file())
                self.assertGreater(line, 0)
                invalid = deepcopy(scenario)
                invalid["variant"] = "unsupported-coverage-variant"
                with self.assertRaises(ValueError):
                    validate_scenario(invalid)

    # self: Static catalog fixture, including retained and currently blocked entries.
    # Raises AssertionError if absences are silent or retired pages are advertised runnable.
    def test_absences_and_retired_families_are_explained(self):
        for entry in load_catalog():
            with self.subTest(identity=entry["id"]):
                if entry["availability"] in ("unsupported", "not-applicable"):
                    self.assertTrue(entry.get("reason"), "Every skipped case needs an explicit reason")
                if entry["source_state"].lower().startswith(("retired", "retained", "planned", "reference only")):
                    self.assertEqual(entry["availability"], "not-applicable")


class CoverageNativeTests(unittest.TestCase):
    """Exercise new destinations automatically through the existing shared native runner."""

    # self: Two separately loaded native runtimes and their immutable completed frames.
    # Raises AssertionError for failed cases, mismatched identities or changed static pixels.
    def test_supported_cases_repeat_in_two_fresh_processes(self):
        first = AtlasJob.start(suite="supported")
        manifest = completed_job(first)
        self.assertGreaterEqual(sum(case["acquisition"] == "captured" for case in manifest["cases"]), 35)
        self.assertNotIn("failed", {case["acquisition"] for case in manifest["cases"]})
        directory = OUTPUT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        # The second process reverses every case, comparing against the first
        # process. The same run then observes the declared touch transitions.
        command = [sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(directory), str(first.result)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=600)
        for name in ("stdout", "stderr"):
            (directory / f"worker.{name}.log").write_text(getattr(result, name), encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr[-6000:] + result.stdout[-2000:])
        print(result.stdout.splitlines()[-1], flush=True)


# directory: New retained artifact directory owned by this internal worker.
# baseline: Verified captures acquired by the first independent native process.
# Raises native/provenance/identity errors without substituting another renderer.
def run_worker(directory, baseline):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Coverage worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-COVERAGE"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    entries = [entry for entry in load_catalog() if entry.get("scenario") and runner.availability(entry)[0]]
    first = load_atlas(baseline)
    expected = {case["id"]: load_capture(ROOT / case["capture"]).metadata
                for case in first["cases"] if case["acquisition"] == "captured"}
    if set(expected) != {entry["id"] for entry in entries}:
        raise AssertionError("Independent native sessions disagree about available cases")
    for entry in reversed(entries):
        runner.prepare(default_scenario(entry["id"], variant=entry.get("atlas_variant")))
        record = runner.capture()
        if record.metadata["id"] != entry["id"]:
            raise AssertionError(f"Wrong completed frame for {entry['id']}")
        reference = expected[entry["id"]]
        if any(record.metadata[key] != reference[key] for key in ("pixel_sha256", "scenario_revision", "rendering_mode")):
            raise AssertionError(f"Independent reverse-order preparation changed the native frame for {entry['id']}")
    routes = {}
    for entry in entries:
        for route in entry.get("routes", []):
            if route.get("replay") is True:
                routes[route["route_id"]] = route
    observed = 0
    failures = {}
    for route_id, route in routes.items():
        origin = next((entry for entry in entries if entry["id"] == route["origin"]), None)
        destination = next((entry for entry in entries if entry["id"] == route["destination"]), None)
        if origin is None or destination is None:
            continue
        try:
            runner.prepare(default_scenario(origin["id"]))
            record = runner.replay_route(route_id)
            if (record.metadata["id"] != destination["id"] or
                    record.metadata["route"]["receipt"]["route_id"] != route_id):
                raise AssertionError("Different destination or route receipt")
            observed += 1
        except Exception as error:
            failures[route_id] = f"{type(error).__name__}: {error}"
    if failures:
        raise AssertionError(f"Native route failures ({observed} observed): {json.dumps(failures)}")
    if observed < 30:
        raise AssertionError(f"Expected at least 30 observed native routes, acquired {observed}")
    print(json.dumps({"repeated_cases": len(expected), "reverse_order": True,
                      "observed_routes": observed}), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] != "--worker":
        raise SystemExit("Coverage workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2], sys.argv[3]))
