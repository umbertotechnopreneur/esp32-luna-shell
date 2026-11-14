"""Focused Studio contract checks; optional native workers never contact devices.

Invoke through ``device.ps1 check -Check cody-studio`` after obtaining approval.
The default suite does not load a native library or build one. ``--native`` also
captures P04 twice in fresh processes with independent settings stores. ``--ui``
uses the real Tk/native product in an isolated session and exports host screenshots.
It needs a display, for example Xvfb on Linux. Generated
fixtures and captures remain in artifacts/ for inspection and manual cleanup.
``--pilot`` checks all ten inventory cases in fresh processes and an isolated
shared native session; it fails when a declared pilot case is unavailable.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest.mock import patch
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
SIMULATOR = ROOT / "cody-device/simulator/windows"
sys.path.insert(0, str(SIMULATOR))

from PIL import Image, ImageDraw, ImageFont

from cody_studio_catalog import entry_for, load_catalog, resolve_source
from cody_studio_comparison import (compare_captures, comparison_image,
                                    load_review, preserve_baseline, save_review)
from cody_studio_runner import load_capture
import cody_studio_runner as studio_runner
from cody_studio_scenario import (default_scenario, load_scenario, save_scenario,
                                  scenario_revision, validate_scenario)

OUTPUT = ROOT / "artifacts/simulator/studio-checks"
PILOT_CASES = {
    "S01": "ui-v1:branding", "S02": "ui-v1:birth", "S17": "ui-v1:guide-buttons",
    "P01": "atlas-v1:10", "P04": "ui-v1:display", "P12": "ui-v1:quiet-hours-edit",
    "W02": "widget-v1:Reset=1", "W04": "widget-v1:Limits=2",
    "S15": "atlas-v1:08", "C04": "next-v1:104",
}
INVENTORY_SHA256 = "4db789f7facf03efa79f6f0efb97ab7e57412b0b73435e50a6c24e8a803b9c77"


# No parameters: allocate a retained fixture directory under repository artifacts.
# Raises OSError if the explicitly owned directory cannot be created.
def fixture_directory():
    destination = OUTPUT / uuid4().hex
    destination.mkdir(parents=True, exist_ok=False)
    return destination


# command: Literal internal host-tool arguments, without a shell or public re-entry.
# directory: Unique artifact directory retaining the original process diagnostics.
# name: Fixed diagnostic filename stem owned by this suite.
# timeout: Real process deadline in seconds, independent of scenario time.
# Raises original launch/timeout errors or RuntimeError for a failing worker.
def run_host_worker(command, directory, name, timeout=120):
    try:
        process = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        for stream in ("stdout", "stderr"):
            value = getattr(error, stream) or ""
            if isinstance(value, bytes):
                value = value.decode("utf-8", errors="replace")
            (directory / f"{name}.{stream}.txt").write_text(value, encoding="utf-8")
        raise
    for stream in ("stdout", "stderr"):
        (directory / f"{name}.{stream}.txt").write_text(getattr(process, stream), encoding="utf-8")
    if process.returncode:
        raise RuntimeError(f"Native worker {name} failed ({process.returncode}):\n"
                           f"{process.stderr}\n{process.stdout}")
    return process


# inventory_id: Authoritative inventory case selected through the product CLI.
# directory: Unique retained process-diagnostic directory.
# name: Fixed filename stem identifying this independent process.
# Raises worker, metadata, image or provenance errors without preview fallback.
def capture_in_fresh_worker(inventory_id, directory, name):
    process = run_host_worker([sys.executable, "-B", str(SIMULATOR / "cody_simulator.py"),
                               "--studio-case", inventory_id, "--studio-capture"], directory, name)
    lines = [line.strip() for line in process.stdout.splitlines() if line.strip()]
    if not lines:
        raise AssertionError(f"Native {inventory_id} worker did not return capture metadata")
    return load_capture(lines[-1])


# record: Hash-checked saved native capture from a declared inventory case.
# inventory_id: Expected local inventory identity, independently mapped above.
# scenario: Optional explicitly submitted non-default scenario for scope/replay cases.
# Raises AssertionError for a different target, scenario, mode or completed frame.
def assert_pilot_record(record, inventory_id, scenario=None):
    entry = entry_for(inventory_id)
    scenario = validate_scenario(scenario if scenario is not None else default_scenario(inventory_id))
    metadata, receipt = record.metadata, record.metadata["frame_identity"]
    expected = {"id": PILOT_CASES[inventory_id], "inventory_id": inventory_id,
                "scenario_revision": scenario_revision(scenario), "variant": scenario["variant"],
                "at_ms": 0, "rendering_mode": entry["rendering_mode"], "source": entry["source"]}
    for name, value in expected.items():
        if metadata.get(name) != value:
            raise AssertionError(f"{inventory_id} capture {name}: {metadata.get(name)!r} != {value!r}")
    for name, value in {"target": entry["native_target"], "scenario_revision": expected["scenario_revision"],
                        "at_ms": 0, "rendering_mode": entry["rendering_mode"]}.items():
        if receipt.get(name) != value:
            raise AssertionError(f"{inventory_id} completed-frame {name} disagrees with its prepared case")
    if type(receipt.get("revision")) is not int or receipt["revision"] <= 0:
        raise AssertionError(f"{inventory_id} has no completed-frame revision")
    if metadata.get("inventory", {}).get("sha256") != INVENTORY_SHA256:
        raise AssertionError(f"{inventory_id} has no matching inventory document identity")
    if inventory_id in ("S01", "S02"):
        if receipt.get("source_animation_sample_ms") != scenario["page"]["sample_at_ms"]:
            raise AssertionError(f"{inventory_id} did not capture its declared native source-animation sample")
    if inventory_id == "S15":
        outcome = scenario["services"]["provisioning"]["outcome"]
        if receipt.get("service_outcome") != outcome:
            raise AssertionError("S15 did not retire the actual simulated provisioning outcome")
        error = receipt.get("service_error")
        if type(error) is not int or error == 0:
            raise AssertionError("S15 falsely represented a failed provisioning service as success")
        if outcome == "timeout" and error != 0x107:
            raise AssertionError("S15 did not preserve the original native timeout cause")
    if inventory_id == "C04":
        context, domain = receipt.get("context", {}), scenario["services"]["domain"]
        if type(receipt.get("abi")) is not int or receipt["abi"] < 2:
            raise AssertionError("C04 has no atomic native scope receipt")
        for name in ("source_id", "chat_id", "turn_id"):
            if context.get(name) != scenario["page"][name]:
                raise AssertionError(f"C04 rendered the wrong domain {name}")
        call_ids = context.get("call_ids")
        if (not isinstance(call_ids, list) or len(call_ids) != min(3, len(domain["calls"])) or
                len(set(call_ids)) != len(call_ids) or receipt.get("call_count") != len(call_ids)):
            raise AssertionError("C04 receipt does not identify the rendered native call rows")
        scoped_calls = {call["id"] for call in domain["calls"] if call["turn_id"] == scenario["page"]["turn_id"]}
        if not set(call_ids).issubset(scoped_calls):
            raise AssertionError("C04 displayed calls from another turn or unmodelled source")


# records: One saved native capture per authoritative pilot inventory ID.
# directory: Unique suite artifact directory receiving the derived host-only sheet.
# Raises capture/image/I/O errors or AssertionError if a pilot capture is missing.
def save_pilot_contact_sheet(records, directory):
    selected = {record.metadata["inventory_id"]: record for record in records}
    if set(selected) != set(PILOT_CASES):
        raise AssertionError("A pilot contact sheet requires all ten acquired native cases")
    side, margin, label_height, heading_height = 466, 18, 64, 82
    tile_width, tile_height = side + margin * 2, side + label_height + margin
    sheet = Image.new("RGB", (tile_width * 2, heading_height + tile_height * 5), "#151323")
    draw = ImageDraw.Draw(sheet)
    fonts = []
    for size in (22, 18, 13):
        try:
            fonts.append(ImageFont.truetype("DejaVuSans.ttf", size))
        except OSError:
            fonts.append(ImageFont.load_default())
    draw.text((margin, 12), "Cody Studio / native pilot captures", font=fonts[0], fill="#f3f0f6")
    draw.text((margin, 44), "Host labels surround unchanged native frames. SIMULATED; physical acceptance pending.",
              font=fonts[2], fill="#c0bacb")
    for index, inventory_id in enumerate(PILOT_CASES):
        record = load_capture(selected[inventory_id].metadata_path)
        entry = entry_for(inventory_id)
        left = (index % 2) * tile_width + margin
        top = heading_height + (index // 2) * tile_height
        draw.text((left, top), f"{inventory_id} / {entry['title']}", font=fonts[1], fill="#f3a55a")
        detail = f"{record.metadata['rendering_mode']} / capture t={record.metadata['at_ms']}"
        sample = record.metadata["scenario"]["page"].get("sample_at_ms")
        if sample is not None:
            detail += f" / source animation {sample} ms"
        draw.text((left, top + 28), detail, font=fonts[2], fill="#c0bacb")
        with Image.open(record.image_path) as original:
            frame = original.resize((side, side), Image.Resampling.NEAREST)
        sheet.paste(frame, (left, top + label_height))
    destination = directory / "pilot-contact-sheet.png"
    with destination.open("xb") as output:
        sheet.save(output, format="PNG")
    return destination


# image: Synthetic test pixels, explicitly unrelated to firmware layout evidence.
# overrides: Optional valid metadata changes for compatibility contract cases.
# scenario: Optional validated inputs used to compare different scenario revisions.
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
        "acquisition": "captured", "id": entry["id"], "inventory_id": entry["inventory_id"],
        "inventory": entry["inventory"],
        "fixed_dependencies": studio_runner.fixed_dependencies(entry, scenario),
        "scenario": scenario, "scenario_id": scenario["id"],
        "scenario_revision": revision, "variant": scenario["variant"],
        "geometry": [466, 466], "at_ms": 0, "rendering_mode": entry["rendering_mode"],
        "png": image_path.name, "png_sha256": hashlib.sha256(image_path.read_bytes()).hexdigest(),
        "pixel_sha256": hashlib.sha256(image.tobytes()).hexdigest(),
        "frame_identity": {"target": entry["native_target"], "scenario_revision": revision,
                           "at_ms": 0, "rendering_mode": entry["rendering_mode"], "revision": 1},
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
    # Raises AssertionError for ambiguous identities or incorrect inventory provenance.
    def test_p04_identity_is_explicit_and_inventory_revision_is_identified(self):
        entries = load_catalog()
        self.assertEqual(len({item["id"] for item in entries}), len(entries))
        display = entry_for("P04")
        self.assertEqual(display["id"], "ui-v1:display")
        self.assertEqual(entry_for("ui-v1:display"), display)
        self.assertEqual(display["inventory"]["reported_entry_count"], 100)
        inventory_path = Path(display["inventory"]["document"])
        self.assertFalse(inventory_path.is_absolute())
        self.assertEqual(inventory_path.name, "Cody-screen-inventory.pdf")
        self.assertEqual(display["inventory"]["sha256"], INVENTORY_SHA256)
        self.assertIn("2026-11-14", display["inventory"]["revision"])
        self.assertIn("e971b313", display["inventory"]["revision"])
        self.assertEqual(display["availability"], "runtime-full")
        source, line = resolve_source(display)
        self.assertTrue(source.is_relative_to(ROOT))
        self.assertGreater(line, 0)

    # self: Source-only inventory mapping; availability does not imply acquired frames.
    # Raises AssertionError for an invented ID meaning, missing owner or implicit mode.
    def test_pilot_mappings_match_the_identified_inventory(self):
        mapped = set()
        for inventory_id, identity in PILOT_CASES.items():
            with self.subTest(inventory_id=inventory_id):
                entry = entry_for(inventory_id)
                self.assertEqual(entry["id"], identity)
                self.assertEqual(entry["inventory_id"], inventory_id)
                self.assertEqual(entry["inventory"]["sha256"], INVENTORY_SHA256)
                self.assertIn(entry["rendering_mode"], ("runtime-full", "presenter-isolated"))
                self.assertTrue(entry["controller"])
                self.assertTrue(entry["dependencies"])
                self.assertTrue(entry["variants"])
                self.assertTrue(entry["routes"])
                source, line = resolve_source(entry)
                self.assertTrue(source.is_relative_to(ROOT))
                self.assertGreater(line, 0)
                scenario = default_scenario(inventory_id)
                self.assertEqual(scenario["context"]["target"], identity)
                self.assertIn(scenario["variant"], entry["variants"])
                self.assertEqual(scenario["time"]["capture_at_ms"], [0])
                mapped.add(identity)
        self.assertEqual(len(mapped), 10)
        self.assertTrue(entry_for("C04")["hard_to_reach"])
        self.assertEqual(entry_for("C04")["rendering_mode"], "presenter-isolated")
        self.assertTrue(entry_for("C04")["limitations"], "Isolated domain data needs an explicit evidence limit")
        self.assertTrue(entry_for("S15")["transient_error"])

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

    # self: Source-contract cases; rejected inputs must not create native state.
    # Raises AssertionError if any pilot accepts an unknown page/service/setting field.
    def test_every_pilot_rejects_unmodelled_page_service_and_setting_payloads(self):
        for inventory_id in PILOT_CASES:
            with self.subTest(inventory_id=inventory_id):
                scenario = default_scenario(inventory_id)
                self.assertEqual(validate_scenario(scenario), scenario)
                for field in ("page", "services"):
                    invalid = deepcopy(scenario)
                    invalid[field]["unsupported_field"] = True
                    with self.assertRaises(ValueError):
                        validate_scenario(invalid)
                invalid = deepcopy(scenario)
                invalid["common"]["settings"]["unowned_setting"] = 1
                with self.assertRaises(ValueError):
                    validate_scenario(invalid)
                invalid = deepcopy(scenario)
                invalid["context"]["target"] = inventory_id
                with self.assertRaisesRegex(ValueError, "qualified"):
                    validate_scenario(invalid)

    # self: Bound page/service inputs independent of any native runtime.
    # Raises AssertionError if scope collisions or impossible source states are admitted.
    def test_pilot_domain_scope_error_responses_and_editor_limits_are_strict(self):
        invalid_cases = []
        for inventory_id in ("S01", "S02"):
            scenario = default_scenario(inventory_id)
            scenario["page"]["sample_at_ms"] = -1
            invalid_cases.append((inventory_id, scenario))
        for field, value in (("minute", 1440), ("endpoint", "middle")):
            scenario = default_scenario("P12")
            scenario["page"][field] = value
            invalid_cases.append((f"P12 {field}", scenario))
        scenario = default_scenario("W02")
        scenario["page"]["weekly"]["remaining_basis_points"] = 10001
        invalid_cases.append(("W02 percentage", scenario))
        scenario = default_scenario("W04")
        scenario["page"]["account"]["short_window_minutes"] = 0
        invalid_cases.append(("W04 window", scenario))
        scenario = default_scenario("S15")
        scenario["services"]["provisioning"]["outcome"] = "live-network"
        invalid_cases.append(("S15 response", scenario))
        for field in ("source_id", "chat_id", "turn_id"):
            scenario = default_scenario("C04")
            scenario["page"][field] = "fffffffffffffffe"
            invalid_cases.append((f"C04 {field} scope", scenario))
        scenario = default_scenario("C04")
        scenario["services"]["domain"]["calls"][1]["id"] = scenario["services"]["domain"]["calls"][0]["id"]
        invalid_cases.append(("C04 duplicate call", scenario))
        scenario = default_scenario("C04")
        scenario["services"]["domain"]["calls"][0]["turn_id"] = "fffffffffffffffe"
        invalid_cases.append(("C04 foreign turn", scenario))
        scenario = default_scenario("C04")
        scenario["services"]["domain"]["calls"][0]["outcome"] = "succeeded"
        invalid_cases.append(("C04 running success", scenario))
        scenario = default_scenario("C04")
        scenario["services"]["domain"]["calls"][1]["tool_name"] = scenario["services"]["domain"]["calls"][0]["tool_name"]
        invalid_cases.append(("C04 conflicting tool definition", scenario))
        for name, invalid in invalid_cases:
            with self.subTest(case=name):
                with self.assertRaises(ValueError):
                    validate_scenario(invalid)


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
        historical = capture_fixture()
        metadata = deepcopy(historical.metadata)
        metadata["rendering_mode"] = "presenter-isolated"
        metadata["frame_identity"]["rendering_mode"] = "presenter-isolated"
        historical.metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
        historical = load_capture(historical.metadata_path)
        mode_result = compare_captures(baseline, historical)
        self.assertEqual(mode_result.status, "not-comparable")
        self.assertTrue(any("rendering mode" in reason for reason in mode_result.reasons))
        older = capture_fixture()
        metadata = deepcopy(older.metadata)
        del metadata["fixed_dependencies"]
        older.metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
        older = load_capture(older.metadata_path)
        input_result = compare_captures(baseline, older)
        self.assertEqual(input_result.status, "not-comparable")
        self.assertIn("Incompatible fixed native inputs", input_result.reasons)

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
            "cody-device/simulator/native/CODY_host_studio_domain.cpp",
            "cody-device/simulator/native/platform/cody_host_studio.h",
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


class PilotWorkerTests(unittest.TestCase):
    """Acquire the ten authoritative cases through the same native runner."""

    # self: Isolated native workers; neither the suite nor CLI builds libraries.
    # Raises AssertionError for a missing case, wrong receipt or nonrepeatable pixels.
    def test_all_ten_cases_repeat_in_independent_processes(self):
        directory = fixture_directory()
        acquired = set()
        selected = []
        for inventory_id in PILOT_CASES:
            with self.subTest(inventory_id=inventory_id):
                before = capture_in_fresh_worker(inventory_id, directory, f"{inventory_id}-first")
                after = capture_in_fresh_worker(inventory_id, directory, f"{inventory_id}-second")
                for record in (before, after):
                    assert_pilot_record(record, inventory_id)
                self.assertNotEqual(before.metadata["capture_id"], after.metadata["capture_id"])
                self.assertNotEqual(before.metadata_path, after.metadata_path)
                with Image.open(before.image_path) as left, Image.open(after.image_path) as right:
                    self.assertEqual(left.tobytes(), right.tobytes(),
                                     f"{inventory_id} is not repeatable across fresh native processes")
                self.assertEqual(compare_captures(before, after).status, "unchanged")
                acquired.add(inventory_id)
                selected.append(after)
                print(f"Pilot native capture: {inventory_id} · {after.metadata_path}", flush=True)
        self.assertEqual(acquired, set(PILOT_CASES), "The ten-case pilot cannot omit unsupported cases")
        contact_sheet = save_pilot_contact_sheet(selected, directory)
        print(f"Pilot contact sheet (derived host review): {contact_sheet}", flush=True)

    # self: One isolated worker reuses the actual display/domain owners across cases.
    # Raises AssertionError for state leakage, stale-build acceptance or altered baselines.
    def test_out_of_order_replay_and_error_recovery_preserve_case_identity(self):
        directory = fixture_directory()
        process = run_host_worker([sys.executable, "-B", str(Path(__file__).resolve()),
                                   "--pilot-worker"], directory, "pilot-replay", timeout=240)
        lines = [line.strip() for line in process.stdout.splitlines() if line.strip()]
        self.assertTrue(lines, "Pilot replay worker returned no evidence")
        summary = json.loads(lines[-1])
        self.assertTrue(summary["invalid_scenario_rejected"])
        self.assertTrue(summary["stale_build_rejected"])
        self.assertTrue(summary["baseline_unchanged"])
        self.assertTrue(summary["alternate_context_verified"])
        records = [load_capture(path) for path in summary["captures"]]
        seen = {record.metadata["inventory_id"] for record in records}
        self.assertEqual(seen, set(PILOT_CASES))
        order = [record.metadata["inventory_id"] for record in records]
        self.assertEqual(order[:4], ["P04", "S15", "P01", "S15"],
                         "Transient provisioning failure must survive recovery to another destination")
        self.assertGreaterEqual(order.count("C04"), 2)
        self.assertGreaterEqual(order.count("S15"), 3)
        for record in records:
            assert_pilot_record(record, record.metadata["inventory_id"], record.metadata["scenario"])
        contexts = {record.metadata["frame_identity"]["context"]["chat_id"] for record in records
                    if record.metadata["inventory_id"] == "C04"}
        self.assertEqual(contexts, {"0000000000000001", "0000000000000002"})


class NativeUiTests(unittest.TestCase):
    """Exercise the real Tk product and native owner, without manual dialogs.

    File dialogs select retained fixture paths and message boxes are observed
    without blocking the suite. Native preparation, capture, comparison, widget
    activation and Tk geometry remain the actual product implementation.
    """

    # self: One native/Tk workflow, isolated from the interactive settings/UI state.
    # Raises the original display/bootstrap error or an assertion on owner failures.
    def setUp(self):
        import tkinter as tk
        from cody_native_device import NativeDevice
        import cody_studio_window

        self.tk, self.ui_module = tk, cody_studio_window
        self.directory = fixture_directory()
        self.state_path = self.directory / "ui-state.json"
        self.preferences_patch = patch.object(cody_studio_window, "STATE_PATH", self.state_path)
        self.preferences_patch.start()
        self.addCleanup(self.preferences_patch.stop)
        self.errors, self.callback_errors = [], []
        self.message_patch = patch.object(cody_studio_window.messagebox, "showerror", side_effect=self.record_error)
        self.message_patch.start()
        self.addCleanup(self.message_patch.stop)
        self.device = NativeDevice({"reason": "SIMULATED-STUDIO-UI-CHECK"}, self.directory / "session.nvs",
                                   80, True, 1, deterministic_clock=True)
        self.device.start()
        self.window = None
        self.addCleanup(self.close_window)
        self.open_window(default_scenario())

    # args: Actual message-box title and error text from the UI error boundary.
    # kwargs: Tk parent and optional dialog settings; no dialog is displayed.
    def record_error(self, *args, **kwargs):
        self.errors.append(str(args[1] if len(args) > 1 else kwargs.get("message", "")))

    # error_type: Tk callback's original exception type.
    # error: Actual callback exception, retained for assertions.
    # traceback: Original callback traceback; no alternate UI behavior is supplied.
    def callback_error(self, error_type, error, traceback):
        self.callback_errors.append(f"{error_type.__name__}: {error}")

    # No parameters: close only this fixture window and drain its bounded UI executor.
    # Raises an executor/Tk failure instead of leaving it hidden after a failed test.
    def close_window(self):
        if self.window is not None:
            if not self.window.closed:
                self.window.close()
            self.window.executor.shutdown(wait=True, cancel_futures=True)
            self.window = None

    # scenario: Strict complete case used by the actual initial Apply callback.
    # Raises Tk/native errors; the existing NativeDevice remains process-owned.
    def open_window(self, scenario):
        self.root = self.tk.Tk()
        self.root.report_callback_exception = self.callback_error
        self.window = self.ui_module.StudioWindow(self.root, self.device, scenario)
        self.wait_for(lambda: bool(self.errors) or (self.window.current is not None and not self.window.busy),
                      "initial native UI capture", timeout=45)
        self.assertEqual(self.errors, [], "Initial UI preparation failed")

    # predicate: Observable completed state; no elapsed pause substitutes for completion.
    # description: Assertion context when the bounded real event-loop wait expires.
    # timeout: Real seconds, distinct from the controlled scenario clock.
    # Raises AssertionError for a timeout or uncaught Tk callback failure.
    def wait_for(self, predicate, description, timeout=15):
        completed = self.tk.BooleanVar(master=self.root, value=False)
        deadline, outcome = time.monotonic() + timeout, {"done": False}

        # No parameters: inspect actual Tk/owner state, then schedule another event check.
        def observe():
            if self.callback_errors:
                completed.set(True)
            elif predicate():
                outcome["done"] = True
                completed.set(True)
            elif time.monotonic() >= deadline:
                completed.set(True)
            else:
                self.root.after(10, observe)

        self.root.after_idle(observe)
        self.root.wait_variable(completed)
        self.assertEqual(self.callback_errors, [], "Uncaught Tk callback failure")
        self.assertTrue(outcome["done"], f"Timed out waiting for {description}; UI: {self.window.status.get()}")

    # name: Human-visible button caption, matched by its current UI text.
    # Raises AssertionError if the requested action is absent or ambiguous.
    def button(self, name):
        pending, matches = [self.root], []
        while pending:
            widget = pending.pop()
            pending.extend(widget.winfo_children())
            if widget.winfo_class() == "Button" and str(widget.cget("text")) == name:
                matches.append(widget)
        self.assertEqual(len(matches), 1, f"Expected one actual UI button: {name}")
        return matches[0]

    # No parameters: activate the real Apply button and wait for a new saved native frame.
    # Raises AssertionError if input is rejected, a worker fails or acquisition stalls.
    def apply_form(self):
        previous = self.window.current
        count = len(self.errors)
        self.window.apply_button.invoke()
        self.wait_for(lambda: len(self.errors) > count or (not self.window.busy and self.window.current is not previous),
                      "new native UI capture", timeout=45)
        self.assertEqual(len(self.errors), count, "Valid form failed native preparation")
        self.assertIsNot(self.window.current, previous)
        return self.window.current

    # inventory_id: Actual catalog destination activated by the product's button.
    # Raises AssertionError for failed direct entry or a capture from another case.
    def open_case(self, inventory_id):
        previous, failures = self.window.current, len(self.errors)
        identity = PILOT_CASES[inventory_id]
        self.window.tree.selection_set(identity)
        self.window.tree.see(identity)
        self.root.update()
        self.button("Open destination").invoke()
        self.wait_for(lambda: len(self.errors) > failures or
                      (not self.window.busy and self.window.current is not previous),
                      f"direct native {inventory_id} entry", timeout=45)
        self.assertEqual(len(self.errors), failures, f"Direct UI entry failed: {inventory_id}")
        record = self.window.current
        assert_pilot_record(record, inventory_id)
        self.assertIn(inventory_id, self.window.frame_title.get())
        self.assertEqual(self.window.form(), default_scenario(inventory_id))
        return record

    # scenario: Valid scenario to place into actual form controls before Apply.
    def fill_scenario(self, scenario):
        self.window.scenario = deepcopy(scenario)
        self.window.show_scenario()

    # filename: Fixture-owned PNG screenshot filename; the native image stays untouched.
    # Raises display/image errors; screenshots document host UI, not device acceptance.
    def screenshot(self, filename):
        from PIL import ImageGrab

        # Process queued Configure/scroll events before drawing and flushing X output.
        self.root.update()
        self.window.draw()
        self.root.update_idletasks()
        self.root.update()
        left, top = self.root.winfo_rootx(), self.root.winfo_rooty()
        bounds = (left, top, left + self.root.winfo_width(), top + self.root.winfo_height())
        destination = self.directory / filename
        ImageGrab.grab(bbox=bounds, xdisplay=os.environ.get("DISPLAY")).save(destination)
        print(f"Studio UI screenshot: {destination}", flush=True)

    # widget: Actual mapped control whose complete rectangle must be reachable.
    # viewport: Root window or scrolling canvas bounding the visible region.
    # Raises AssertionError when a control is hidden, clipped or given zero area.
    def assert_widget_visible(self, widget, viewport):
        self.root.update_idletasks()
        self.assertTrue(widget.winfo_ismapped(), f"Hidden widget: {widget}")
        left, top = widget.winfo_rootx(), widget.winfo_rooty()
        width, height = widget.winfo_width(), widget.winfo_height()
        view_left, view_top = viewport.winfo_rootx(), viewport.winfo_rooty()
        self.assertGreater(width, 1)
        self.assertGreater(height, 1)
        self.assertGreaterEqual(left, view_left)
        self.assertGreaterEqual(top, view_top)
        self.assertLessEqual(left + width, view_left + viewport.winfo_width())
        self.assertLessEqual(top + height, view_top + viewport.winfo_height())

    # No parameters: require the operational status text to survive every layout size.
    # Raises AssertionError if the reserved status area or its text is clipped.
    def assert_status_visible(self):
        label = self.window.status_label
        self.assert_widget_visible(label, self.root)
        self.assertGreaterEqual(label.winfo_width() + 2, label.winfo_reqwidth())
        self.assertGreaterEqual(label.winfo_height() + 2, label.winfo_reqheight())

    # widget: Actual descendant of the canvas's scrolled native Tk content.
    # viewport: Canvas whose real scroll commands must expose the complete control.
    # Raises AssertionError if scrolling cannot make the control fully reachable.
    def reveal_widget(self, widget, viewport):
        self.root.update()
        region = viewport.bbox("all")
        self.assertIsNotNone(region, "Scrollable host section has no content")
        top = widget.winfo_rooty() - viewport.winfo_rooty() + viewport.canvasy(0)
        region_height = max(1, region[3] - region[1])
        viewport.yview_moveto(max(0.0, (top - region[1] - 8) / region_height))
        self.root.update()
        self.assert_widget_visible(widget, viewport)

    # No parameters: reach every Atlas control through the actual scroll viewport.
    # Raises AssertionError if saved thumbnails are clipped or omitted from the grid.
    def show_atlas(self):
        window = self.window
        if not window.sections["Atlas"][2]:
            window.sections["Atlas"][0].invoke()
        self.root.update()
        viewport = window.bottom_canvas
        self.assert_widget_visible(viewport, self.root)
        thumbnails = window.thumbnails.winfo_children()
        self.assertTrue(thumbnails, "Atlas has no acquired thumbnail controls")
        self.assertEqual(len(thumbnails), len(window.records), "Atlas omitted acquired captures")
        for control in thumbnails:
            self.assertEqual(control.winfo_manager(), "grid")
            self.reveal_widget(control, viewport)
        self.reveal_widget(self.button("Open saved capture…"), viewport)

    # root: Existing Tcl interpreter, including after its Tk window is destroyed.
    # Returns product poll timers independently of its chosen Python attribute name.
    def pending_poll_callbacks(self, root):
        scheduled = root.tk.splitlist(root.tk.call("after", "info"))
        return {identity for identity in scheduled
                if "poll" in str(root.tk.call("after", "info", identity)).lower()}

    # expected_ratio: Native frame/comparison width-to-height ratio, before host scaling.
    # minimum_height: Smallest readable rendered frame height admitted in host pixels.
    # Raises AssertionError if the displayed image clips or changes native proportions.
    def assert_frame_geometry(self, expected_ratio, minimum_height=2):
        self.root.update_idletasks()
        images = [item for item in self.window.canvas.find_all() if self.window.canvas.type(item) == "image"]
        self.assertEqual(len(images), 1)
        left, top, right, bottom = self.window.canvas.bbox(images[0])
        width, height = self.window.canvas.winfo_width(), self.window.canvas.winfo_height()
        self.assertGreater(right - left, 1)
        self.assertGreaterEqual(bottom - top, minimum_height)
        self.assertGreaterEqual(left, 0)
        self.assertGreaterEqual(top, 0)
        self.assertLessEqual(right, width)
        self.assertLessEqual(bottom, height)
        self.assertLessEqual(abs((right - left) - expected_ratio * (bottom - top)), 2)

    # self: One real native owner reused through edits and a reopened Tk window.
    # Raises AssertionError for frame contamination, broken controls or lost review state.
    def test_native_ui_edit_compare_resize_focus_and_persistence(self):
        window = self.window
        original = default_scenario()
        first = window.current
        self.root.geometry("1440x960+0+0")
        self.wait_for(lambda: window.canvas.winfo_width() > 100, "wide canvas geometry")
        window.draw()
        self.assert_frame_geometry(1)
        self.show_atlas()
        self.assert_status_visible()
        self.screenshot("studio-wide.png")
        self.button("Preserve current as baseline").invoke()
        baseline = window.baseline
        retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
        window.mode.set("side-by-side")
        window.draw()
        self.assert_frame_geometry(2)
        self.show_atlas()
        self.assert_status_visible()
        self.screenshot("studio-compare.png")

        window.left_visible.set(False)
        window.right_visible.set(False)
        window.columns()
        for name in ("Animation", "Routes", "Atlas"):
            if window.sections[name][2]:
                window.sections[name][0].invoke()
        self.root.geometry("720x540+0+0")
        self.wait_for(lambda: self.root.winfo_width() == 720, "small window geometry")
        window.mode.set("current")
        window.draw()
        self.assert_frame_geometry(1, minimum_height=180)
        self.assertFalse(window.left.winfo_ismapped())
        self.assertFalse(window.right_host.winfo_ismapped())
        for name in ("Animation", "Routes", "Atlas"):
            self.assertFalse(window.sections[name][2])
            self.assert_widget_visible(window.sections[name][0], window.bottom_canvas)
        self.assert_status_visible()
        self.screenshot("studio-small.png")

        self.root.geometry("1440x960+0+0")
        window.left_visible.set(True)
        window.right_visible.set(True)
        window.columns()
        self.root.update()
        heading = window.sections["Scenario"][0]
        before = window.sections["Scenario"][2]
        heading.focus_force()
        self.root.update()
        self.assertEqual(self.root.focus_get(), heading)
        self.assertGreater(int(heading.cget("highlightthickness")), 0)
        self.assertEqual(heading.cget("highlightcolor"), self.ui_module.ORANGE)
        heading.event_generate("<KeyPress-space>")
        heading.event_generate("<KeyRelease-space>")
        self.wait_for(lambda: window.sections["Scenario"][2] != before, "keyboard expander activation")
        heading.event_generate("<KeyPress-Tab>")
        heading.event_generate("<KeyRelease-Tab>")
        self.root.update()
        self.assertIsNotNone(self.root.focus_get())
        self.assertNotEqual(self.root.focus_get(), heading)
        if not window.sections["Scenario"][2]:
            heading.invoke()

        changed = deepcopy(original)
        changed["common"]["settings"].update(brightness_percent=10, auto_rotate=True, orientation_degrees=180)
        changed["common"]["observations"]["battery"]["level_percent"] = 0
        changed["common"]["observations"]["network"] = {"connected": True, "ssid": "S" * 32, "rssi": -100}
        changed["common"]["observations"]["clock"]["utc_offset_minutes"] = -840
        self.fill_scenario(changed)
        second = self.apply_form()
        self.assertEqual(second.metadata["scenario_revision"], scenario_revision(changed))
        self.assertIn("Not comparable", window.compare_status.get())
        self.assertEqual(window.baseline.metadata_path, baseline.metadata_path)
        saved = self.directory / "edited-scenario.json"
        with patch.object(self.ui_module.filedialog, "asksaveasfilename", return_value=str(saved)):
            self.button("Save scenario…").invoke()
        self.assertEqual(load_scenario(saved), changed)

        upper = deepcopy(changed)
        upper["common"]["settings"].update(brightness_percent=100, auto_rotate=False, orientation_degrees=0)
        upper["common"]["observations"]["battery"]["level_percent"] = 100
        upper["common"]["observations"]["network"]["rssi"] = -1
        upper["common"]["observations"]["clock"]["utc_offset_minutes"] = 840
        self.fill_scenario(upper)
        self.assertEqual(self.apply_form().metadata["scenario_revision"], scenario_revision(upper))
        self.fill_scenario(original)
        restored = self.apply_form()
        self.assertEqual(restored.metadata["pixel_sha256"], first.metadata["pixel_sha256"],
                         "A→B→C→A changed a controlled static native frame")
        with patch.object(self.ui_module.filedialog, "askopenfilename", return_value=str(saved)):
            self.button("Open scenario…").invoke()
        self.assertEqual(window.form(), changed)
        self.assertEqual(window.current.metadata_path, restored.metadata_path,
                         "Loading a scenario must wait for explicit Apply")
        replayed = self.apply_form()
        self.assertEqual(replayed.metadata["pixel_sha256"], second.metadata["pixel_sha256"])

        for name in ("Atlas", "Review notes"):
            if not window.sections[name][2]:
                window.sections[name][0].invoke()
        self.show_atlas()
        window.note.insert("1.0", "Inspect this exact native capture.")
        window.review.set("approved")
        self.button("Save capture review").invoke()
        window.set_current(first)
        with patch.object(self.ui_module.filedialog, "askopenfilename", return_value=str(replayed.metadata_path)):
            self.button("Open saved capture…").invoke()
        self.assertEqual(window.review.get(), "approved")
        self.assertEqual(window.note.get("1.0", "end").strip(), "Inspect this exact native capture.")
        self.assertIn("1", window.sections["Review notes"][3])

        # Destination opening uses the product catalog and runner, not touch navigation.
        pilot_records = {}
        for inventory_id in PILOT_CASES:
            pilot_records[inventory_id] = self.open_case(inventory_id)
            self.assertEqual(window.baseline.metadata_path, baseline.metadata_path)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
        self.assertEqual({record.metadata["inventory_id"] for record in window.records}, set(PILOT_CASES))

        # Explicit history selection must inspect the selected capture's complete context.
        s15_thumbnail = self.button("S15 · acquired")
        self.reveal_widget(s15_thumbnail, window.bottom_canvas)
        s15_thumbnail.invoke()
        self.assertEqual(window.current.metadata_path, pilot_records["S15"].metadata_path)
        self.assertEqual(window.form(), pilot_records["S15"].metadata["scenario"])
        self.assertEqual(window.tree.selection(), (PILOT_CASES["S15"],))
        self.assertIn("S15", window.frame_title.get())
        self.assertIn("S15", window.scenario_title.get())
        self.assertIn(PILOT_CASES["S15"], window.route_summary.get())
        self.assertIn(("services", "provisioning", "outcome"), window.case_fields)
        self.assertNotIn(("page", "chat_id"), window.case_fields)

        c04_thumbnail = self.button("C04 · acquired")
        self.reveal_widget(c04_thumbnail, window.bottom_canvas)
        c04_thumbnail.invoke()
        self.assertEqual(window.current.metadata_path, pilot_records["C04"].metadata_path)
        self.assertEqual(window.form(), default_scenario("C04"))
        self.assertIn("C04", window.scenario_title.get())
        self.assertEqual(window.tree.selection(), (PILOT_CASES["C04"],))
        call_path = ("services", "domain", "calls", 0)
        for field, value in (("label", "UI changed command"), ("state", "returned"), ("outcome", "succeeded")):
            self.assertIn((*call_path, field), window.case_fields)
            variable, field_type = window.case_fields[(*call_path, field)]
            self.assertIs(field_type, str)
            variable.set(value)
        edited_calls = deepcopy(default_scenario("C04"))
        edited_calls["services"]["domain"]["calls"][0].update(
            label="UI changed command", state="returned", outcome="succeeded")
        changed_calls = self.apply_form()
        assert_pilot_record(changed_calls, "C04", edited_calls)
        self.assertNotEqual(changed_calls.metadata["pixel_sha256"], pilot_records["C04"].metadata["pixel_sha256"],
                            "C04 controls did not change the native tool-call presentation")
        restored_calls = self.open_case("C04")
        self.assertEqual(restored_calls.metadata["pixel_sha256"], pilot_records["C04"].metadata["pixel_sha256"],
                         "C04 did not restore its source-backed default after editing call state")
        with patch.object(self.ui_module.filedialog, "askopenfilename",
                          return_value=str(pilot_records["S15"].metadata_path)):
            self.button("Open saved capture…").invoke()
        self.assertEqual(window.form(), default_scenario("S15"))
        self.assertEqual(window.current.metadata_path, pilot_records["S15"].metadata_path)
        self.assertNotIn((*call_path, "label"), window.case_fields)
        self.open_case("C04")
        self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
        self.show_atlas()
        self.assert_status_visible()
        self.screenshot("studio-pilot.png")
        p04_again = self.open_case("P04")
        self.assertEqual(p04_again.metadata["pixel_sha256"], first.metadata["pixel_sha256"],
                         "Changing pilot destinations contaminated the original P04 frame")

        previous = window.current
        failures = len(self.errors)
        window.inputs["brightness"].set("9")
        window.apply_button.invoke()
        self.assertEqual(len(self.errors), failures + 1)
        self.assertFalse(window.busy)
        self.assertEqual(window.current.metadata_path, previous.metadata_path)
        window.inputs["brightness"].set("10")
        invalid = self.directory / "invalid-scenario.json"
        invalid.write_text('{"schema":"unsupported"}', encoding="utf-8")
        form_before = window.form()
        with patch.object(self.ui_module.filedialog, "askopenfilename", return_value=str(invalid)):
            self.button("Open scenario…").invoke()
        self.assertEqual(len(self.errors), failures + 2)
        self.assertEqual(window.form(), form_before)
        self.assertEqual(window.current.metadata_path, previous.metadata_path)
        self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)

        for name in ("Comparison", "Changes", "Review notes", "Animation", "Routes", "Atlas"):
            window.sections[name][0].invoke()
        expected = {name: values[2] for name, values in window.sections.items()}
        window.left_visible.set(False)
        window.columns()
        preferences = json.loads(self.state_path.read_text(encoding="utf-8"))
        self.assertEqual(preferences["expanders"], expected)
        old_root = self.root
        pending_poll = self.pending_poll_callbacks(old_root)
        self.assertTrue(pending_poll, "Live Studio has no scheduled result poll")
        self.close_window()
        self.assertFalse(pending_poll & self.pending_poll_callbacks(old_root),
                         "Closed Studio retained a Tcl poll callback")
        # Expected validation errors were observed; the reopened real window must succeed.
        self.errors.clear()
        self.open_window(original)
        self.assertEqual({name: values[2] for name, values in self.window.sections.items()}, expected)
        self.assertFalse(self.window.left_visible.get())
        self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)


# No parameters: exercise one process-owned native session and retained artifacts.
# Raises native, contract or AssertionError failures without replacing valid captures.
def run_pilot_worker():
    from cody_native_device import NativeDevice

    directory = fixture_directory()
    device = NativeDevice({"reason": "SIMULATED-STUDIO-PILOT-CHECK"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True)
    device.start()
    runner = studio_runner.StudioRunner(device)
    original_pixels, captures = {}, []
    baseline = None
    retained = None
    sequence = ["P04", "S15", "P01", "S15", "C04", "S01", "S02", "S17", "P12", "W02", "W04",
                *reversed(tuple(PILOT_CASES))]
    capture_root = directory / "captures"
    with patch.object(studio_runner, "CAPTURE_ROOT", capture_root):
        for inventory_id in sequence:
            runner.prepare(default_scenario(inventory_id))
            record = runner.capture()
            assert_pilot_record(record, inventory_id)
            with Image.open(record.image_path) as image:
                pixels = image.tobytes()
            if inventory_id in original_pixels and pixels != original_pixels[inventory_id]:
                raise AssertionError(f"{inventory_id} inherited state from a different pilot destination")
            original_pixels.setdefault(inventory_id, pixels)
            captures.append(record)
            if baseline is None:
                baseline = preserve_baseline(record)
                retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
        if set(original_pixels) != set(PILOT_CASES):
            raise AssertionError("Native replay did not acquire the complete ten-case pilot")

        # The isolated domain query must replace exact scopes and their visible rows.
        alternate = default_scenario("C04")
        alternate["id"] = "c04-alternate-scope"
        alternate["page"].update(source_id="0000000000000002", chat_id="0000000000000002",
                                 turn_id="0000000000000020")
        domain = alternate["services"]["domain"]
        domain.update(alternate["page"])
        domain["generation"] = 2
        domain["chat_title"] = "Alternate synthetic pilot chat"
        for index, call in enumerate(domain["calls"]):
            call["id"] = f"{0x201 + index:016x}"
            call["turn_id"] = alternate["page"]["turn_id"]
        domain["calls"][0]["label"] = "Alternate command"
        runner.prepare(alternate)
        changed = runner.capture()
        assert_pilot_record(changed, "C04", alternate)
        with Image.open(changed.image_path) as image:
            if image.tobytes() == original_pixels["C04"]:
                raise AssertionError("C04 ignored a changed native tool-call label")
        captures.append(changed)
        runner.prepare(default_scenario("C04"))
        restored = runner.capture()
        assert_pilot_record(restored, "C04")
        with Image.open(restored.image_path) as image:
            if image.tobytes() != original_pixels["C04"]:
                raise AssertionError("C04 retained data from the alternate domain scope")
        captures.append(restored)

        # A rejected page payload must not publish an artifact under a guessed ID.
        before = set(capture_root.glob("*/capture.json"))
        for boundary in ("page", "services"):
            invalid = default_scenario("P04")
            invalid[boundary]["unsupported_field"] = True
            try:
                runner.prepare(invalid)
            except ValueError:
                pass
            else:
                raise AssertionError(f"Native pilot accepted an invalid {boundary} payload")
            if set(capture_root.glob("*/capture.json")) != before:
                raise AssertionError("Rejected pilot preparation published capture metadata")

        # Reprepare through the owner after rejection; the prior valid artifact stays intact.
        runner.prepare(default_scenario("P04"))
        recovered = runner.capture()
        assert_pilot_record(recovered, "P04")
        with Image.open(recovered.image_path) as image:
            if image.tobytes() != original_pixels["P04"]:
                raise AssertionError("P04 did not recover its original pixels after invalid input")
        captures.append(recovered)
        before = set(capture_root.glob("*/capture.json"))
        changed_build = deepcopy(runner.build)
        changed_build["library_sha256"] = "0" * 64 if runner.build["library_sha256"] != "0" * 64 else "1" * 64
        with patch.object(studio_runner, "verify_current_build", return_value=changed_build):
            try:
                runner.capture()
            except ValueError as error:
                if "build" not in str(error).lower():
                    raise
            else:
                raise AssertionError("A changed native build produced an apparently current capture")
        if set(capture_root.glob("*/capture.json")) != before:
            raise AssertionError("Stale build rejection published capture metadata")

    if (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()) != retained:
        raise AssertionError("Pilot replay replaced its explicitly preserved baseline")
    print(json.dumps({"captures": [str(record.metadata_path) for record in captures],
                      "invalid_scenario_rejected": True, "stale_build_rejected": True,
                      "baseline_unchanged": True, "alternate_context_verified": True}), flush=True)
    return 0


# argv: Explicit suite flags after the public command dispatcher.
# Returns the unit-test exit code; raises argparse errors for unknown options.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", action="store_true",
                        help="Also capture P04 in two fresh prebuilt native processes")
    parser.add_argument("--ui", action="store_true",
                        help="Also exercise real Tk/native editing, comparisons, focus and resize; requires a display")
    parser.add_argument("--pilot", action="store_true",
                        help="Capture and repeat all ten pilot cases, including native cross-case replay")
    parser.add_argument("--pilot-worker", action="store_true", help=argparse.SUPPRESS)
    arguments = parser.parse_args(argv)
    if arguments.pilot_worker:
        if arguments.native or arguments.ui or arguments.pilot:
            parser.error("The internal pilot worker cannot be combined with suite flags")
        return run_pilot_worker()
    loader = unittest.TestLoader()
    suite = unittest.TestSuite([loader.loadTestsFromTestCase(CatalogContractTests),
                               loader.loadTestsFromTestCase(ScenarioContractTests),
                               loader.loadTestsFromTestCase(ComparisonContractTests),
                               loader.loadTestsFromTestCase(BuildProvenanceContractTests)])
    if arguments.native:
        suite.addTests(loader.loadTestsFromTestCase(NativeWorkerTests))
    if arguments.ui:
        suite.addTests(loader.loadTestsFromTestCase(NativeUiTests))
    if arguments.pilot:
        suite.addTests(loader.loadTestsFromTestCase(PilotWorkerTests))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print("Generated artifacts are retained; cleanup is manual.")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
