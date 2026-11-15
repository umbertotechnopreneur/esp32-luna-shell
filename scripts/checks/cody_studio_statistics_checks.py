"""Focused typed statistics contracts and one isolated native semantic-edit worker."""

from copy import deepcopy
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import unittest
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from cody_studio_catalog import entry_for
from cody_studio_scenario import default_scenario, scenario_revision, validate_scenario

OUTPUT = ROOT / "artifacts/simulator/studio/statistics-checks"
STATISTICS_CASES = {
    "context": "widget-v1:Context=8",
    "today": "widget-v1:Today=3",
    "seven-days": "widget-v1:LastSevenDays=4",
    "token-history": "widget-v1:TokenHistory=5",
    "credits": "widget-v1:Credits=6",
    "account": "widget-v1:Account=7",
}
CONTROLS = ("ui-v1:display", "widget-v1:Reset=1", "widget-v1:Limits=2")


# name: One declared statistics source identity, independent of local inventory aliases.
# Raises the original catalog or scenario error if the fixture is absent or malformed.
def statistics_scenario(name):
    return default_scenario(STATISTICS_CASES[name])


class StatisticsContractTests(unittest.TestCase):
    """Strict Python fields prevent coercion before the native service boundary."""

    # self: Six explicit static fixture contracts, without loading a native library.
    # Raises AssertionError for a missing target, service response or typed statistics input.
    def test_all_six_statistics_cases_have_explicit_ready_owner_inputs(self):
        for name, identity in STATISTICS_CASES.items():
            with self.subTest(case=name):
                value = validate_scenario(statistics_scenario(name))
                self.assertEqual(value["context"]["target"], identity)
                self.assertEqual(value["services"]["codex_link"], {"outcome": "ready"})
                self.assertIn("statistics", value["services"])
                self.assertEqual(entry_for(identity)["native_target"], f"cody.dashboard.{name}")

    # self: Each exact-capacity integer array, including values beyond its ABI capacity.
    # Raises AssertionError if a list can be truncated, padded or accepted with a different type.
    def test_integer_arrays_reject_wrong_lengths_and_container_types(self):
        for name, field, capacity in (("today", "daily_tokens", 8),
                                      ("token-history", "interval_tokens", 96),
                                      ("token-history", "interval_valid_mask", 3)):
            for length in (capacity - 1, capacity + 1):
                value = statistics_scenario(name)
                value["services"]["statistics"][field] = [0] * length
                with self.subTest(case=name, field=field, length=length), self.assertRaises(ValueError):
                    validate_scenario(value)
            value = statistics_scenario(name)
            value["services"]["statistics"][field] = tuple([0] * capacity)
            with self.subTest(field=field, container="tuple"), self.assertRaises(ValueError):
                validate_scenario(value)

    # self: Integer observations with bool, negative and overflowing proposed values.
    # Raises AssertionError if Python silently coerces an invalid numeric observation.
    def test_integer_arrays_and_scalars_reject_bool_negative_and_overflow(self):
        arrays = (("today", "daily_tokens", (True, -1, 1 << 64)),
                  ("token-history", "interval_tokens", (False, -1, 1 << 64)),
                  ("token-history", "interval_valid_mask", (True, -1, 1 << 32)))
        for name, field, proposals in arrays:
            for proposal in proposals:
                value = statistics_scenario(name)
                value["services"]["statistics"][field][-1] = proposal
                with self.subTest(field=field, value=proposal), self.assertRaises(ValueError):
                    validate_scenario(value)
        scalars = (("today", "daily_valid_mask", (True, -1, 256)),
                   ("today", "source_age_ms", (False, -1, 60001)),
                   ("credits", "credit_milliunits", (True, -1, 1 << 63)),
                   ("context", "context_used_tokens", (True, -1, 1 << 32)),
                   ("context", "context_capacity_tokens", (False, 0, 1 << 32)))
        for name, field, proposals in scalars:
            for proposal in proposals:
                value = statistics_scenario(name)
                value["services"]["statistics"][field] = proposal
                with self.subTest(field=field, value=proposal), self.assertRaises(ValueError):
                    validate_scenario(value)

    # self: Target-specific statistics groups and bounded device ASCII captions.
    # Raises AssertionError if unrelated fields or malformed text enter a prepared scenario.
    def test_unrelated_fields_and_invalid_captions_are_rejected(self):
        invalid = statistics_scenario("today")
        invalid["services"]["statistics"]["credit_milliunits"] = 1000
        with self.assertRaises(ValueError):
            validate_scenario(invalid)
        for name, field, value in (("account", "plan", ""), ("account", "plan_label", "x" * 25),
                                    ("account", "plan_price", "bad\ncaption"),
                                    ("context", "context_chat_title", "x" * 129),
                                    ("credits", "credits_kind", "invented")):
            invalid = statistics_scenario(name)
            invalid["services"]["statistics"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_scenario(invalid)


class StatisticsNativeTests(unittest.TestCase):
    """One isolated process exercises semantic edits, retirement and the explicit ABI."""

    # self: Fresh native process with its own NVS, captures and retained stdout/stderr.
    # Raises original launch/deadline errors or AssertionError with precise worker diagnostics.
    def test_native_edits_retirement_and_rejected_requests_preserve_completed_frames(self):
        directory = OUTPUT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        command = [sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(directory)]
        try:
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=180)
        except subprocess.TimeoutExpired as error:
            for stream in ("stdout", "stderr"):
                value = getattr(error, stream) or ""
                if isinstance(value, bytes):
                    value = value.decode("utf-8", errors="replace")
                (directory / f"worker.{stream}.log").write_text(value, encoding="utf-8")
            raise
        for stream in ("stdout", "stderr"):
            (directory / f"worker.{stream}.log").write_text(getattr(result, stream), encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr[-8000:] + result.stdout[-2000:])
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        self.assertTrue(lines, "Statistics worker returned no result")
        summary = json.loads(lines[-1])
        self.assertEqual(summary["edited_cases"], 6)
        self.assertEqual(summary["retirement_controls"], 6)
        self.assertGreaterEqual(summary["rejected_native_requests"], 10)
        print(lines[-1], flush=True)


# name: Declared statistics page with visible fixture data.
# value: Independent validated scenario copy, never the interactive Studio object.
# Raises KeyError for a missing fixture; the native renderer must expose the changed observation.
def edit_visible_value(name, value):
    statistics = value["services"]["statistics"]
    if name == "today":
        statistics["daily_tokens"][-1] += 5000
    elif name == "seven-days":
        statistics["daily_tokens"][-2] += 50000
    elif name == "token-history":
        statistics["interval_tokens"][-1] += 16000
    elif name == "credits":
        statistics["credit_milliunits"] += 10000
    elif name == "account":
        statistics.update(plan="business", plan_label="Business", plan_price="")
    elif name == "context":
        statistics["context_used_tokens"] += 16000
    else:
        raise ValueError(f"Unknown statistics semantic edit: {name}")


# target: Stable textual native destination, never a firmware enum ordinal.
# revision: Exact SHA-256 identity used for this ABI-boundary rejection exercise.
# Raises no exceptions; returns caller-owned input memory with valid base scalar settings.
def raw_request(target, revision):
    from cody_native_device import StudioCase

    request = StudioCase()
    request.abi, request.size = 2, ctypes.sizeof(StudioCase)
    request.identity.abi = 2
    request.identity.target = target.encode("ascii")
    request.identity.scenario_revision = revision.encode("ascii")
    request.brightness_percent, request.playback_volume = 80, 90
    request.quiet_start_minute, request.quiet_end_minute = 1320, 420
    request.quiet_edit_start, request.quiet_draft_minute = 1, 1320
    if target.startswith("cody.dashboard."):
        request.codex_link_outcome = b"ready"
    return request


# device: Fresh process-owned native runtime displaying a valid connected statistics control.
# Raises AssertionError for accepted invalid inputs, substituted errors or altered completed pixels/identity.
def rejected_requests(device):
    from cody_native_device import StudioCase
    from cody_native_display import completed_studio_frame

    valid = default_scenario("widget-v1:Limits=2")
    revision = scenario_revision(valid)
    prepare = device.api.cody_host_studio_prepare_case
    prepare.argtypes, prepare.restype = [ctypes.POINTER(StudioCase)], ctypes.c_int
    before_image, before_identity = completed_studio_frame()
    requests = []
    request = raw_request("cody.settings.display", revision)
    request.statistics.source_age_ms = 1
    requests.append(("non-statistics source age", request, 0x102))
    request = raw_request("cody.settings.display", revision)
    request.statistics.daily_tokens[0], request.statistics.daily_valid_mask = 1, 1
    requests.append(("non-statistics daily payload", request, 0x102))
    request = raw_request("cody.dashboard.today", revision)
    request.statistics.credits_kind, request.statistics.credit_milliunits = b"finite", 1000
    requests.append(("wrong page credits group", request, 0x102))
    request = raw_request("cody.dashboard.today", revision)
    request.statistics.daily_valid_mask = 256
    requests.append(("invalid daily mask", request, 0x102))
    request = raw_request("cody.dashboard.today", revision)
    request.statistics.source_age_ms = 60001
    requests.append(("invalid source age", request, 0x102))
    request = raw_request("cody.dashboard.credits", revision)
    request.statistics.credits_kind, request.statistics.credit_milliunits = b"finite", -1
    requests.append(("negative native credit observation", request, 0x102))
    request = raw_request("cody.dashboard.credits", revision)
    request.statistics.credits_kind = b"fabricated"
    requests.append(("unknown native credit kind", request, 0x102))
    request = raw_request("cody.dashboard.context", revision)
    request.statistics.context_chat_title = b"Synthetic context"
    requests.append(("zero context capacity", request, 0x102))
    request = raw_request("cody.dashboard.context", revision)
    request.statistics.context_chat_title = b"bad\x1ftext"
    request.statistics.context_capacity_tokens = 256000
    requests.append(("invalid context ASCII", request, 0x102))
    request = raw_request("cody.dashboard.account", revision)
    request.statistics.plan = b"A" * 25
    requests.append(("unterminated native plan", request, 0x102))
    request = raw_request("cody.dashboard.token-history", revision)
    request.statistics.plan = b"plus"
    requests.append(("wrong history account group", request, 0x102))
    request = raw_request("cody.settings.display", revision)
    request.abi = 1
    requests.append(("obsolete request ABI", request, 0x104))
    request = raw_request("cody.settings.display", revision)
    request.size -= 1
    requests.append(("wrong native input capacity", request, 0x104))
    for name, request, expected_error in requests:
        error = prepare(ctypes.byref(request))
        if error != expected_error:
            raise AssertionError(f"{name}: native returned 0x{error:X}, expected 0x{expected_error:X}")
        after_image, after_identity = completed_studio_frame()
        if after_identity != before_identity or after_image.tobytes() != before_image.tobytes():
            raise AssertionError(f"Rejected {name} changed the completed frame or its atomic identity")
    return len(requests)


# directory: New artifact-owned worker directory, never a native library or user data path.
# Raises native/provenance/identity failures without accepting a different renderer or stale frame.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Statistics worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-STATISTICS"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    controls = {}
    for identity in CONTROLS:
        runner.prepare(default_scenario(identity))
        controls[identity] = runner.capture().metadata["pixel_sha256"]
    evidence = []
    for index, (name, identity) in enumerate(STATISTICS_CASES.items()):
        original = statistics_scenario(name)
        runner.prepare(original)
        before = runner.capture()
        edited = deepcopy(original)
        edit_visible_value(name, edited)
        runner.prepare(edited)
        after = runner.capture()
        for record, scenario in ((before, original), (after, edited)):
            metadata = record.metadata
            if (metadata["id"] != identity or metadata["frame_identity"]["target"] != f"cody.dashboard.{name}" or
                    metadata["scenario_revision"] != scenario_revision(scenario) or
                    metadata["rendering_mode"] != "runtime-full"):
                raise AssertionError(f"Semantic {name} edit captured a different target, scenario or renderer")
        if before.metadata["pixel_sha256"] == after.metadata["pixel_sha256"]:
            raise AssertionError(f"The real {name} renderer ignored its changed visible observation")
        control = CONTROLS[index % len(CONTROLS)]
        runner.prepare(default_scenario(control))
        if runner.capture().metadata["pixel_sha256"] != controls[control]:
            raise AssertionError(f"Statistics {name} leaked into retained control {control}")
        runner.prepare(original)
        replay = runner.capture()
        if replay.metadata["pixel_sha256"] != before.metadata["pixel_sha256"]:
            raise AssertionError(f"Resetting {name} did not restore its explicit original observations")
        evidence.append({"id": identity, "before": str(before.metadata_path.relative_to(ROOT)),
                         "edited": str(after.metadata_path.relative_to(ROOT)),
                         "restored": str(replay.metadata_path.relative_to(ROOT)), "control": control})
    # A connected control lets invalid statistics groups reach their validator;
    # a disconnected base would reject earlier at the canonical Wi-Fi prerequisite.
    runner.prepare(default_scenario("widget-v1:Limits=2"))
    rejected = rejected_requests(device)
    summary = {"edited_cases": len(evidence), "retirement_controls": len(evidence),
               "rejected_native_requests": rejected, "captures": evidence}
    (directory / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary), flush=True)
    return 0


# base_class: Existing real Tk/native fixture, executed in its own dedicated process.
# Returns only this statistics workflow; inherited general UI tests are not duplicated.
def ui_suite(base_class):
    class StatisticsUiTests(base_class):
        # self: Real compact JSON-array controls, immutable baseline and family catalog selection.
        # Raises AssertionError for ignored edits, admitted malformed data or lost inventory identities.
        def test_array_editor_native_capture_and_family_identity(self):
            from cody_native_display import completed_studio_frame

            window = self.window
            self.fill_scenario(statistics_scenario("token-history"))
            first = self.apply_form()
            self.assertEqual(first.metadata["id"], STATISTICS_CASES["token-history"])
            self.assertEqual(first.metadata["frame_identity"]["target"], "cody.dashboard.token-history")
            self.button("Preserve current as baseline").invoke()
            baseline = window.baseline
            self.assertIsNotNone(baseline)
            baseline_bytes = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
            path = ("services", "statistics", "interval_tokens")
            variable, field_type = window.case_fields[path]
            self.assertIs(field_type, list)
            control = window.case_field_widgets[path]
            self.assertEqual(control.winfo_class(), "Entry")
            original = json.loads(variable.get())
            self.assertEqual(len(original), 96)
            edited = list(original)
            edited[-1] += 16000
            variable.set(json.dumps(edited, separators=(",", ":")))
            after = self.apply_form()
            self.assertEqual(after.metadata["id"], first.metadata["id"])
            self.assertEqual(after.metadata["frame_identity"]["target"], "cody.dashboard.token-history")
            self.assertEqual(after.metadata["scenario"]["services"]["statistics"]["interval_tokens"], edited)
            self.assertEqual(after.metadata["scenario_revision"], scenario_revision(window.form()))
            self.assertNotEqual(after.metadata["pixel_sha256"], first.metadata["pixel_sha256"])
            self.assertIs(window.baseline, baseline)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), baseline_bytes)

            # Both malformed JSON and a correctly sized array containing a bool
            # must stop at the actual form boundary, preserving the native frame.
            frame, receipt = completed_studio_frame()
            bool_array = list(edited)
            bool_array[-1] = True
            failures = len(self.errors)
            for text in ("[", json.dumps(bool_array)):
                variable.set(text)
                previous = window.current
                window.apply_button.invoke()
                self.root.update()
                failures += 1
                self.assertEqual(len(self.errors), failures)
                self.assertIn("interval_tokens", self.errors[-1])
                self.assertFalse(window.busy)
                self.assertIs(window.current, previous)
                rejected_frame, rejected_receipt = completed_studio_frame()
                self.assertEqual(rejected_frame.tobytes(), frame.tobytes())
                self.assertEqual(rejected_receipt, receipt)
                self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), baseline_bytes)
            variable.set(json.dumps(edited, separators=(",", ":")))

            window.left_visible.set(False)
            window.right_visible.set(True)
            window.columns()
            for name in ("Animation", "Routes", "Atlas"):
                if window.sections[name][2]:
                    window.sections[name][0].invoke()
            self.root.geometry("900x700+0+0")
            self.wait_for(lambda: self.root.winfo_width() == 900, "small statistics window geometry")
            self.reveal_widget(control, window.inspector_canvas)
            control.focus_force()
            self.root.update()
            self.assertEqual(self.root.focus_get(), control)
            self.assert_widget_visible(control, window.inspector_canvas)
            window.draw()
            self.assert_frame_geometry(1, minimum_height=180)
            self.assert_status_visible()
            self.screenshot("studio-statistics-array-small.png")

            self.root.geometry("1440x960+0+0")
            window.left_visible.set(True)
            window.columns()
            family_identity = "ui-v1:wifi-scan-unavailable"
            self.assertEqual(entry_for("S08")["id"], "ui-v1:wifi-networks")
            self.assertTrue(window.tree.exists(family_identity))
            window.tree.selection_set(family_identity)
            window.tree.see(family_identity)
            self.root.update()
            previous = window.current
            self.button("Open destination").invoke()
            self.wait_for(lambda: len(self.errors) > failures or
                          (not window.busy and window.current is not previous),
                          "qualified Wi-Fi unavailable family entry", timeout=45)
            self.assertEqual(len(self.errors), failures)
            self.assertEqual(window.current.metadata["id"], family_identity)
            self.assertIsNone(window.current.metadata["inventory_id"])
            self.assertEqual(window.current.metadata["inventory_family_id"], "S08")
            self.assertIn("S08 (family)", window.frame_title.get())
            self.assertIn("S08 (family)", window.scenario_title.get())
            self.assertEqual(entry_for("S08")["id"], "ui-v1:wifi-networks")
            self.assertIs(window.baseline, baseline)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), baseline_bytes)
            self.assert_frame_geometry(1)
            self.screenshot("studio-inventory-family.png")

    return unittest.TestSuite([StatisticsUiTests("test_array_editor_native_capture_and_family_identity")])


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Statistics workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
