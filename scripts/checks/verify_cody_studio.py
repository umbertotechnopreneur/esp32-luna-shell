"""Focused Studio contract checks; optional native workers never contact devices.

Invoke through ``device.ps1 check -Check cody-studio`` after obtaining approval.
The default suite does not load a native library or build one. ``--native`` also
captures P04 twice in fresh processes with independent settings stores. Generated
fixtures and captures remain in artifacts/ for inspection and manual cleanup.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
SIMULATOR = ROOT / "cody-device/simulator/windows"
sys.path.insert(0, str(SIMULATOR))

from PIL import Image

from cody_studio_catalog import entry_for, load_catalog, resolve_source
from cody_studio_comparison import (compare_captures, comparison_image,
                                    load_review, preserve_baseline, save_review)
from cody_studio_runner import load_capture
import cody_studio_runner as studio_runner
from cody_studio_scenario import (default_scenario, load_scenario, save_scenario,
                                  scenario_revision, validate_scenario)

OUTPUT = ROOT / "artifacts/simulator/studio-checks"


# No parameters: allocate a retained fixture directory under repository artifacts.
# Raises OSError if the explicitly owned directory cannot be created.
def fixture_directory():
    destination = OUTPUT / uuid4().hex
    destination.mkdir(parents=True, exist_ok=False)
    return destination


# image: Synthetic test pixels, explicitly unrelated to firmware layout evidence.
# overrides: Optional valid metadata changes for compatibility contract cases.
# scenario: Optional validated P04 inputs used to compare different scenario revisions.
# Raises OSError for retained fixture writes; no native runtime is loaded.
def capture_fixture(image=None, overrides=None, scenario=None):
    directory = fixture_directory()
    image = image if image is not None else Image.new("RGB", (466, 466), (7, 13, 23))
    scenario = validate_scenario(scenario if scenario is not None else default_scenario())
    entry = entry_for(scenario["context"]["target"])
    revision = scenario_revision(scenario)
    image_path = directory / "fixture.png"
    image.save(image_path, format="PNG")
    metadata = {
        "schema": "cody.studio.capture.v1", "capture_id": uuid4().hex,
        "acquisition": "captured", "id": "ui-v1:display", "inventory_id": "P04",
        "scenario": scenario, "scenario_id": scenario["id"],
        "scenario_revision": revision, "variant": "default",
        "geometry": [466, 466], "at_ms": 0, "rendering_mode": "runtime-full",
        "png": image_path.name, "png_sha256": hashlib.sha256(image_path.read_bytes()).hexdigest(),
        "pixel_sha256": hashlib.sha256(image.tobytes()).hexdigest(),
        "frame_identity": {"target": entry["native_target"], "scenario_revision": revision,
                           "at_ms": 0, "rendering_mode": "runtime-full", "revision": 1},
        "source": entry["source"], "source_sha256": "0" * 64,
        "build": {"manifest_sha256": "0" * 64, "library_sha256": "0" * 64,
                  "inputs": {entry["source"]: "0" * 64}},
        "evidence": "Synthetic contract-test image; not native rendering evidence",
    }
    metadata.update(overrides or {})
    metadata_path = directory / "capture.json"
    with metadata_path.open("x", encoding="utf-8") as output:
        json.dump(metadata, output, indent=2)
        output.write("\n")
    return load_capture(metadata_path)


class CatalogContractTests(unittest.TestCase):
    """Check public identities and current source references without rendering."""

    # self: Contract fixture with no native runtime or persistence owner.
    # Raises AssertionError for ambiguous identities or a guessed external revision.
    def test_p04_identity_is_explicit_and_inventory_revision_is_unresolved(self):
        entries = load_catalog()
        self.assertEqual(len({item["id"] for item in entries}), len(entries))
        display = entry_for("P04")
        self.assertEqual(display["id"], "ui-v1:display")
        self.assertEqual(entry_for("ui-v1:display"), display)
        self.assertEqual(display["inventory"]["reported_entry_count"], 100)
        self.assertIsNone(display["inventory"]["revision"])
        self.assertEqual(display["availability"], "runtime-full")
        source, line = resolve_source(display)
        self.assertTrue(source.is_relative_to(ROOT))
        self.assertGreater(line, 0)

    # self: Contract fixture; invalid identities must fail before native startup.
    # Raises AssertionError for implicit numeric, unknown or whitespace ID lookup.
    def test_unknown_and_unqualified_ids_are_rejected(self):
        for identity in ("P404", "4", "10", "display", " P04", "ui-v1:missing", None):
            with self.subTest(identity=identity):
                with self.assertRaisesRegex(ValueError, "identity"):
                    entry_for(identity)


class ScenarioContractTests(unittest.TestCase):
    """Reject ambiguous inputs and preserve portable scenario identity."""

    # self: Pure JSON fixture; no native library is loaded.
    # Raises AssertionError if schema boundaries silently admit unknown content.
    def test_invalid_schema_target_variant_context_and_services_are_rejected(self):
        changes = (
            (("schema",), "cody.studio.scenario.v0"),
            (("context", "target"), "P404"),
            (("variant",), "unknown"),
            (("context", "caller"), "ui-v1:unknown"),
            (("context", "return_to"), "ui-v1:display"),
            (("context", "selection"), 0),
            (("page",), {"fabricated_display": True}),
            (("services",), {"ccp": "live"}),
            (("common", "settings", "brightness_percent"), True),
            (("common", "settings", "brightness_percent"), 9),
            (("common", "settings", "brightness_percent"), 101),
            (("common", "settings", "orientation_degrees"), 45),
            (("common", "settings", "orientation_degrees"), 90),
            (("common", "settings", "auto_rotate"), 1),
            (("common", "observations", "battery", "level_percent"), -1),
            (("common", "observations", "network", "connected"), 1),
            (("common", "observations", "clock", "epoch_seconds"), True),
            (("seed",), 1),
        )
        for path, value in changes:
            with self.subTest(path=path, value=value):
                scenario = default_scenario()
                owner = scenario
                for key in path[:-1]:
                    owner = owner[key]
                owner[path[-1]] = value
                with self.assertRaises(ValueError) as rejected:
                    validate_scenario(scenario)
                self.assertTrue(str(rejected.exception))
        extra = default_scenario()
        extra["use_pc_clock"] = True
        with self.assertRaisesRegex(ValueError, "exactly"):
            validate_scenario(extra)
        missing = default_scenario()
        del missing["common"]["observations"]["clock"]
        with self.assertRaisesRegex(ValueError, "exactly"):
            validate_scenario(missing)

    # self: Controlled-static case, deliberately without animation replay support.
    # Raises AssertionError if unsupported time or numeric coercions are admitted.
    def test_timeline_rejects_reverse_seek_and_boolean_or_float_instants(self):
        changes = (
            ("initial_ms", -1), ("initial_ms", 1),
            ("initial_ms", False), ("initial_ms", 0.0),
            ("capture_at_ms", [False]), ("capture_at_ms", [0.0]),
            ("capture_at_ms", [0, 1]), ("advances_ms", [20, -20]),
            ("mode", "pc-clock"),
        )
        for name, value in changes:
            with self.subTest(name=name, value=value):
                scenario = default_scenario()
                scenario["time"][name] = value
                with self.assertRaises(ValueError):
                    validate_scenario(scenario)

    # self: Synthetic observations with explicit connected and disconnected states.
    # Raises AssertionError if contradictory network data or unsafe text is accepted.
    def test_synthetic_network_observations_are_coherent(self):
        for network in (
                {"connected": True, "ssid": "", "rssi": -55},
                {"connected": False, "ssid": "old-network", "rssi": None},
                {"connected": False, "ssid": "", "rssi": -55},
                {"connected": True, "ssid": "SIMULATED\x00wifi", "rssi": -55},
                {"connected": True, "ssid": "SIMULATED", "rssi": 0}):
            with self.subTest(network=network):
                scenario = default_scenario()
                scenario["common"]["observations"]["network"] = network
                with self.assertRaises(ValueError):
                    validate_scenario(scenario)

    # self: Retained JSON fixtures beneath artifacts; files are never cleaned here.
    # Raises AssertionError if formatting changes identity or saving overwrites data.
    def test_round_trip_revision_and_duplicate_fields(self):
        directory = fixture_directory()
        scenario = default_scenario()
        revision = scenario_revision(scenario)
        reordered = {key: scenario[key] for key in reversed(tuple(scenario))}
        self.assertEqual(scenario_revision(reordered), revision)
        changed = deepcopy(scenario)
        changed["common"]["settings"]["brightness_percent"] = 37
        self.assertNotEqual(scenario_revision(changed), revision)
        destination = save_scenario(scenario, directory / "scenario.json")
        original = destination.read_bytes()
        self.assertEqual(load_scenario(destination), scenario)
        with self.assertRaises(FileExistsError):
            save_scenario(changed, destination)
        self.assertEqual(destination.read_bytes(), original)
        duplicate = directory / "duplicate.json"
        duplicate.write_text('{"schema":"first","schema":"last"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            load_scenario(duplicate)
        with self.assertRaisesRegex(ValueError, "artifacts"):
            save_scenario(scenario, ROOT / "studio-invalid-output.json")


class ComparisonContractTests(unittest.TestCase):
    """Use synthetic pixels only to exercise evidence and baseline semantics."""

    # self: Fixture without native libraries, live endpoints or firmware page drawing.
    # Raises AssertionError when pixel changes imply approval or compare incorrectly.
    def test_exact_pixel_change_and_three_comparison_views(self):
        original = capture_fixture()
        identical = capture_fixture()
        unchanged = compare_captures(original, identical)
        self.assertTrue(unchanged.compatible)
        self.assertEqual((unchanged.status, unchanged.changed_pixels), ("unchanged", 0))
        with Image.open(original.image_path) as source:
            changed_image = source.copy()
        changed_image.putpixel((233, 233), (255, 13, 23))
        current = capture_fixture(changed_image)
        changed = compare_captures(original, current)
        self.assertEqual((changed.status, changed.changed_pixels), ("changed", 1))
        self.assertEqual(comparison_image(original, current).size, (932, 466))
        difference = comparison_image(original, current, "difference")
        self.assertEqual(difference.getbbox(), (233, 233, 234, 234))
        overlay_before = comparison_image(original, current, "overlay", 0)
        overlay_after = comparison_image(original, current, "overlay", 1)
        with Image.open(original.image_path) as before, Image.open(current.image_path) as after:
            self.assertEqual(overlay_before.tobytes(), before.tobytes())
            self.assertEqual(overlay_after.tobytes(), after.tobytes())
        with self.assertRaises(ValueError):
            comparison_image(original, current, "invalid")
        with self.assertRaises(ValueError):
            comparison_image(original, current, "overlay", 1.1)

    # self: Separate captures with one incompatible provenance dimension each.
    # Raises AssertionError if incompatible cases receive a visual difference result.
    def test_incompatible_cases_are_explicitly_not_comparable(self):
        baseline = capture_fixture()
        scenario = default_scenario()
        scenario["common"]["settings"]["brightness_percent"] = 37
        current = capture_fixture(scenario=scenario)
        result = compare_captures(baseline, current)
        self.assertFalse(result.compatible)
        self.assertEqual(result.status, "not-comparable")
        self.assertIsNone(result.changed_pixels)
        self.assertTrue(any("scenario revision" in reason for reason in result.reasons))
        with self.assertRaisesRegex(ValueError, "Incompatible"):
            comparison_image(baseline, current)

    # self: Explicitly chosen fixture; comparisons and reviews must preserve it.
    # Raises AssertionError if baseline bytes change or notes escape their capture.
    def test_baseline_is_copied_once_per_selection_and_never_replaced(self):
        current = capture_fixture()
        original = (current.image_path.read_bytes(), current.metadata_path.read_bytes())
        baseline = preserve_baseline(current)
        retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
        second_baseline = preserve_baseline(current)
        self.assertNotEqual(baseline.metadata_path, second_baseline.metadata_path)
        self.assertNotEqual(baseline.image_path, current.image_path)
        self.assertTrue(baseline.metadata["baseline"])
        self.assertEqual(baseline.metadata["baseline_of"], current.metadata["capture_id"])
        compare_captures(baseline, current)
        review_path = save_review(current, "Inspect the selected capture only.", "annotated")
        review = json.loads(review_path.read_text(encoding="utf-8"))
        self.assertEqual(review["capture_id"], current.metadata["capture_id"])
        self.assertEqual(review["pixel_sha256"], current.metadata["pixel_sha256"])
        self.assertEqual(review["status"], "annotated")
        restored = load_review(current)
        self.assertEqual((restored["status"], restored["note"], restored["count"]),
                         ("annotated", "Inspect the selected capture only.", 1))
        save_review(current, "", "approved")
        self.assertEqual((load_review(current)["status"], load_review(current)["count"]), ("approved", 2))
        unrelated = capture_fixture()
        self.assertEqual(load_review(unrelated), {"status": "unreviewed", "note": "", "count": 0})
        self.assertEqual((current.image_path.read_bytes(), current.metadata_path.read_bytes()), original)
        self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
        self.assertFalse(any(baseline.metadata_path.parent.glob("review-*.json")))

    # self: Retained fixture with deliberately corrupted metadata or PNG bytes.
    # Raises AssertionError if wrong geometry or altered pixels retain capture validity.
    def test_capture_geometry_and_integrity_fail_closed(self):
        record = capture_fixture()
        metadata = dict(record.metadata, geometry=[320, 320])
        record.metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "geometry"):
            load_capture(record.metadata_path)
        other = capture_fixture()
        with other.image_path.open("ab") as output:
            output.write(b"contract-test-corruption")
        with self.assertRaisesRegex(ValueError, "hash"):
            load_capture(other.metadata_path)

    # self: An edited external capture JSON must not redirect review writes.
    # Raises AssertionError if a non-UUID identity becomes an artifact directory.
    def test_capture_identity_cannot_escape_review_artifacts(self):
        record = capture_fixture()
        for identity in ("../../../../outside", "/tmp/outside", "", "not-a-uuid"):
            with self.subTest(identity=identity):
                metadata = dict(record.metadata, capture_id=identity)
                record.metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
                with self.assertRaises(ValueError):
                    load_capture(record.metadata_path)

    # self: Saved capture whose frame identity must agree with its scenario.
    # Raises AssertionError if a route or revision from another frame is admitted.
    def test_completed_frame_receipt_must_match_scenario_and_time(self):
        record = capture_fixture()
        for field, value in (("target", "cody.settings.other"),
                             ("scenario_revision", "1" * 64),
                             ("at_ms", 1), ("at_ms", False), ("revision", 0)):
            with self.subTest(field=field, value=value):
                metadata = deepcopy(record.metadata)
                metadata["frame_identity"][field] = value
                record.metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "receipt"):
                    load_capture(record.metadata_path)


class BuildProvenanceContractTests(unittest.TestCase):
    """Exercise stale dependency rejection using files that are never loaded."""

    # self: Isolated fake build files; the actual project/runtime is never modified.
    # Raises AssertionError if changed dependencies or a replaced library are accepted.
    def test_changed_source_and_library_are_rejected_before_capture(self):
        directory = fixture_directory()
        source_names = (
            "cody-device/simulator/native/CODY_host_display.cpp",
            "cody-device/components/CODY_companion/src/runtime/CODY_device_ui.cpp",
            "cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp",
        )
        inputs = {}
        for relative in source_names:
            path = directory / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// Isolated provenance fixture; not compiled.\n", encoding="utf-8")
            inputs[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        library = directory / "artifacts/build-cody-simulator-graphics-device/fixture-library.bin"
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_bytes(b"contract-only fixture; never load as executable")
        manifest = {"schema": "cody.host-graphics.v1", "inputs": inputs,
                    "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest()}
        manifest_path = library.parent / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with patch.object(studio_runner, "ROOT", directory), patch.object(
                studio_runner, "simulator_library_path", return_value=library):
            self.assertEqual(studio_runner.verify_current_build()["library_sha256"], manifest["library_sha256"])
            changed = directory / source_names[0]
            changed.write_text("// Changed source fixture.\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Stale"):
                studio_runner.verify_current_build()
            manifest["inputs"][source_names[0]] = hashlib.sha256(changed.read_bytes()).hexdigest()
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            library.write_bytes(b"replaced library fixture")
            with self.assertRaisesRegex(ValueError, "library"):
                studio_runner.verify_current_build()


class NativeWorkerTests(unittest.TestCase):
    """Run only when the owner explicitly approves the --native public suite."""

    # self: Native fixture using the current interpreter and prebuilt library only.
    # Raises process, native or validation errors without substituting preview images.
    def test_two_isolated_p04_workers_capture_identical_completed_frames(self):
        captures = []
        directory = fixture_directory()
        scenario = default_scenario()
        revision = scenario_revision(scenario)
        for index in range(2):
            command = [sys.executable, "-B", str(SIMULATOR / "cody_simulator.py"),
                       "--studio-case", "P04", "--studio-capture"]
            process = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
            (directory / f"worker-{index + 1}.stdout.txt").write_text(process.stdout, encoding="utf-8")
            (directory / f"worker-{index + 1}.stderr.txt").write_text(process.stderr, encoding="utf-8")
            self.assertEqual(process.returncode, 0,
                             f"P04 worker {index + 1} failed:\n{process.stderr}\n{process.stdout}")
            lines = [line.strip() for line in process.stdout.splitlines() if line.strip()]
            self.assertTrue(lines, "P04 worker did not return a capture metadata path")
            record = load_capture(lines[-1])
            metadata = record.metadata
            self.assertEqual(metadata["id"], "ui-v1:display")
            self.assertEqual(metadata["inventory_id"], "P04")
            self.assertEqual(metadata["scenario_revision"], revision)
            receipt = metadata["frame_identity"]
            self.assertEqual(receipt["target"], entry_for("P04")["native_target"])
            self.assertEqual(receipt["scenario_revision"], revision)
            self.assertEqual((receipt["at_ms"], receipt["rendering_mode"]), (0, "runtime-full"))
            self.assertEqual(metadata["at_ms"], receipt["at_ms"])
            self.assertTrue(metadata["build"]["library_sha256"])
            captures.append(record)
        self.assertNotEqual(captures[0].metadata["capture_id"], captures[1].metadata["capture_id"])
        self.assertNotEqual(captures[0].metadata_path, captures[1].metadata_path)
        with Image.open(captures[0].image_path) as first, Image.open(captures[1].image_path) as second:
            self.assertEqual(first.tobytes(), second.tobytes(), "P04 is not repeatable across fresh native processes")
        self.assertEqual(compare_captures(*captures).status, "unchanged")


# argv: Explicit suite flags after the public command dispatcher.
# Returns the unit-test exit code; raises argparse errors for unknown options.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", action="store_true",
                        help="Also capture P04 in two fresh prebuilt native processes")
    arguments = parser.parse_args(argv)
    loader = unittest.TestLoader()
    suite = unittest.TestSuite([loader.loadTestsFromTestCase(CatalogContractTests),
                               loader.loadTestsFromTestCase(ScenarioContractTests),
                               loader.loadTestsFromTestCase(ComparisonContractTests),
                               loader.loadTestsFromTestCase(BuildProvenanceContractTests)])
    if arguments.native:
        suite.addTests(loader.loadTestsFromTestCase(NativeWorkerTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print("Generated artifacts are retained; cleanup is manual.")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
