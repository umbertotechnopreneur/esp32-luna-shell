"""Focused native inspection of Home, static link states and typed question variants."""

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

OUTPUT = ROOT / "artifacts/simulator/studio/inspection-checks"
INSPECTION_CASES = {
    "H01": ("cody.home.muse", "runtime-full"),
    "H03": ("cody.home.screensaver", "presenter-isolated"),
    "W10": ("cody.dashboard.about", "runtime-full"),
    "S24": ("cody.codex.pair-pc", "runtime-full"),
    "S25": ("cody.codex.committed", "runtime-full"),
    "S26": ("cody.codex.discovering", "runtime-full"),
    "S27": ("cody.codex.connected", "presenter-isolated"),
}


class InspectionContractTests(unittest.TestCase):
    """Variant selection supplies its own complete typed fixture and semantic parents."""

    # self: Seven explicit inspection contracts; no native library or device is loaded.
    # Raises AssertionError for a changed effective target or overstated rendering mode.
    def test_inspection_modes_and_static_inputs_are_explicit(self):
        for identity, (target, mode) in INSPECTION_CASES.items():
            with self.subTest(identity=identity):
                entry = entry_for(identity)
                value = validate_scenario(default_scenario(identity))
                self.assertEqual((entry["native_target"], entry["rendering_mode"]), (target, mode))
                self.assertEqual(value["time"], {"mode": "controlled-static", "initial_ms": 0,
                                                 "advances_ms": [], "capture_at_ms": [0]})

    # self: Original unavailable C06 fixture and separately selected populated fixture.
    # Raises AssertionError if selecting a variant mutates the default or borrows an incompatible schema.
    def test_question_variants_keep_distinct_complete_fixtures(self):
        original = default_scenario("C06")
        retained = deepcopy(original)
        populated = default_scenario("C06", variant="populated")
        self.assertEqual(original["variant"], "default")
        self.assertNotIn("questions", original["services"]["domain"])
        self.assertEqual(populated["variant"], "populated")
        self.assertEqual(populated["services"]["domain"]["questions_outcome"], "supported")
        self.assertEqual(len(populated["services"]["domain"]["questions"]), 3)
        self.assertEqual(default_scenario("C06"), retained)
        with self.assertRaises(ValueError):
            default_scenario("C06", variant="invented")
        incompatible = deepcopy(original)
        incompatible["variant"] = "populated"
        with self.assertRaises(ValueError):
            validate_scenario(incompatible)

    # self: Typed pending rows whose identities must remain unique and in the selected scope.
    # Raises AssertionError if Python admits an unlinked parent or duplicate question identity.
    def test_question_parent_scope_and_duplicate_ids_are_rejected(self):
        for field in ("chat_id", "turn_id"):
            invalid = default_scenario("C06", variant="populated")
            invalid["services"]["domain"]["questions"][0][field] = "0000000000000999"
            with self.subTest(parent=field), self.assertRaises(ValueError):
                validate_scenario(invalid)
        invalid = default_scenario("C06", variant="populated")
        questions = invalid["services"]["domain"]["questions"]
        questions[1]["id"] = questions[0]["id"]
        with self.assertRaises(ValueError):
            validate_scenario(invalid)


class InspectionNativeTests(unittest.TestCase):
    """One fresh process checks semantic receipts and the unchanged global clock."""

    # self: Process-owned settings, native runtime and retained artifact diagnostics.
    # Raises original deadline errors or AssertionError with the worker's precise original diagnostics.
    def test_native_inspection_and_question_retirement(self):
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
        self.assertTrue(lines, "Inspection worker returned no result")
        summary = json.loads(lines[-1])
        self.assertEqual(summary["inspected_cases"], 7)
        self.assertEqual(summary["global_monotonic_us"], 1000000)
        self.assertEqual(summary["question_rejections"], 3)
        self.assertEqual(summary["visible_question_count"], 3)
        self.assertTrue(summary["p04_restored"])
        self.assertIn("no standalone getter", summary["screensaver_preference_evidence"])
        print(lines[-1], flush=True)


# device: Initialized process-owned native device exposing the documented host monotonic read.
# Raises AssertionError if a local presenter sample changes the clock shared with workers/services.
def frozen_clock(device):
    read = device.api.cody_host_monotonic_us
    read.argtypes, read.restype = [], ctypes.c_int64
    value = read()
    if value != 1000000:
        raise AssertionError(f"Static inspection changed the global monotonic clock to {value} us")
    return value


# record: Native frame whose receipt retired atomically with its pixels.
# scenario: Explicit complete fixture submitted through the shared product runner.
# Raises AssertionError for a different destination, mode or scenario revision.
def assert_record(record, scenario):
    entry = entry_for(scenario["context"]["target"])
    metadata, receipt = record.metadata, record.metadata["frame_identity"]
    if (metadata["id"] != entry["id"] or metadata["rendering_mode"] != entry["rendering_mode"] or
            receipt["target"] != entry["native_target"] or receipt["rendering_mode"] != entry["rendering_mode"] or
            receipt["scenario_revision"] != scenario_revision(scenario) or receipt["abi"] != 3):
        raise AssertionError(f"Inspection receipt does not match {entry['id']}")


# device: Runtime displaying the prepared Pair Codex fixture, or a later unrelated case.
# available: Whether the current case explicitly owns the synthetic six-character code.
# Raises AssertionError for an unrelated code, substituted error or changed native frame.
# An unexported service symbol is reported as an explicit platform limitation.
def pairing_code(device, available):
    from cody_native_display import completed_studio_frame

    read = getattr(device.api, "LUNASHELL_codex_link_copy_pairing_code", None)
    if read is None:
        return "Service copy-code symbol is not exported; receipt/source fixture only, no direct API evidence"
    read.argtypes, read.restype = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint32], ctypes.c_int
    frame, identity = completed_studio_frame()
    output = ctypes.create_string_buffer(b"XXXXXX", 7)
    error = read(output, len(output), 1)
    expected = 0 if available else 0x106  # ESP_ERR_NOT_SUPPORTED from the host public error contract.
    if error != expected or output.value != (b"SIM123" if available else b""):
        raise AssertionError(f"Pairing-code source returned 0x{error:X}, value={output.value!r}")
    if available:
        error = read(output, len(output), 2)
        if error != 0x103 or output.value != b"":  # ESP_ERR_INVALID_STATE; never another attempt's code.
            raise AssertionError(f"Wrong pairing attempt returned 0x{error:X} or retained a code")
    after, after_identity = completed_studio_frame()
    if frame.tobytes() != after.tobytes() or identity != after_identity:
        raise AssertionError("Reading the service-owned pairing code changed the completed native frame")
    return "Existing service API: exact code/attempt checked and source retired on the next case"


# scenario: Valid populated C06 fixture; copied into the explicit ABI input, never owner memory.
# Raises no exceptions for a valid fixture; malformed semantic records are tested by the native boundary.
def raw_questions_request(scenario):
    from cody_native_device import StudioCase

    request = StudioCase()
    request.abi, request.size = 3, ctypes.sizeof(StudioCase)
    request.identity.abi = 3
    request.identity.target = b"cody.codex.questions"
    request.identity.scenario_revision = scenario_revision(scenario).encode("ascii")
    request.brightness_percent, request.playback_volume = 80, 90
    request.quiet_start_minute, request.quiet_end_minute = 1320, 420
    request.quiet_edit_start, request.quiet_draft_minute = 1, 1320
    domain = scenario["services"]["domain"]
    for name in ("source_id", "chat_id", "turn_id"):
        setattr(request.identity, name, int(domain[name], 16))
        setattr(request, "domain_" + name, int(domain[name], 16))
    request.domain_generation = domain["generation"]
    request.domain_chat_title = domain["chat_title"].encode("ascii")
    request.domain_execution = domain["execution"].encode("ascii")
    request.domain_questions_outcome = domain["questions_outcome"].encode("ascii")
    request.domain_question_count = len(domain["questions"])
    for index, question in enumerate(domain["questions"]):
        row = request.domain_questions[index]
        for name in ("id", "chat_id", "turn_id"):
            setattr(row, name, int(question[name], 16))
        row.prompt = question["prompt"].encode("ascii")
    return request


# device: Native runtime displaying a valid populated question capture.
# scenario: Valid fixture used to construct independent caller-owned ABI inputs.
# Raises AssertionError if invalid parent/identity data is admitted or changes the current frame.
def rejected_questions(device, scenario):
    from cody_native_device import StudioCase
    from cody_native_display import completed_studio_frame

    prepare = device.api.cody_host_studio_prepare_case
    prepare.argtypes, prepare.restype = [ctypes.POINTER(StudioCase)], ctypes.c_int
    frame, identity = completed_studio_frame()
    for field in ("chat_id", "turn_id", "duplicate"):
        request = raw_questions_request(scenario)
        if field == "duplicate":
            request.domain_questions[1].id = request.domain_questions[0].id
        else:
            setattr(request.domain_questions[0], field, 0)
        error = prepare(ctypes.byref(request))
        if error != 0x102:
            raise AssertionError(f"Invalid question {field} returned 0x{error:X}, expected ESP_ERR_INVALID_ARG")
        after, after_identity = completed_studio_frame()
        if after.tobytes() != frame.tobytes() or after_identity != identity:
            raise AssertionError(f"Rejected question {field} changed native pixels or their atomic identity")
    return 3


# record: Native receipt that must reflect exactly the provided pending-question rows.
# scenario: Default unavailable or explicit populated question fixture.
# Raises AssertionError for stale IDs/counts or a different selected source/chat/turn.
def assert_questions(record, scenario):
    assert_record(record, scenario)
    questions = scenario["services"]["domain"].get("questions", [])
    expected = [question["id"] for question in questions]
    receipt = record.metadata["frame_identity"]
    if receipt["question_count"] != len(expected) or receipt["context"]["question_ids"] != expected:
        raise AssertionError("Question pixels retired with missing, unrelated or stale question identities")


# directory: New retained artifact directory for one independent native inspection session.
# Raises native, provenance, identity and deadline failures without substituting another renderer.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Inspection worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-INSPECTION"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    frozen_clock(device)
    p04 = default_scenario("P04")
    runner.prepare(p04)
    p04_hash = runner.capture().metadata["pixel_sha256"]
    runner.prepare(default_scenario("P08"))
    behavior_hash = runner.capture().metadata["pixel_sha256"]
    evidence, code_evidence = [], None
    for inventory_id, (target, mode) in INSPECTION_CASES.items():
        scenario = default_scenario(inventory_id)
        runner.prepare(scenario)
        record = runner.capture()
        assert_record(record, scenario)
        if (record.metadata["frame_identity"]["target"], record.metadata["rendering_mode"]) != (target, mode):
            raise AssertionError(f"Wrong native inspection mode for {inventory_id}")
        frozen_clock(device)
        if inventory_id == "H03":
            dependencies = record.metadata["fixed_dependencies"]
            if not dependencies["screensaver_enabled"] or dependencies["screensaver_owner_sample"]["global_clock_advanced"]:
                raise AssertionError("Screensaver fixture does not describe its isolated local owner sample")
        if inventory_id == "S24":
            code_evidence = pairing_code(device, True)
        runner.prepare(p04)
        restored = runner.capture()
        if restored.metadata["pixel_sha256"] != p04_hash:
            raise AssertionError(f"{inventory_id} contaminated restored P04 pixels")
        frozen_clock(device)
        if inventory_id == "S24":
            retired = pairing_code(device, False)
            if retired != code_evidence:
                raise AssertionError("Pairing-code inspection capability changed within one loaded runtime")
        evidence.append({"inventory_id": inventory_id, "capture": str(record.metadata_path.relative_to(ROOT))})
    # P08 is the actual production preference presenter. Together with the
    # preparation bridge's settings write/readback this checks the restored Off
    # observation. There is deliberately no claim of an independent bool getter.
    runner.prepare(default_scenario("P08"))
    if runner.capture().metadata["pixel_sha256"] != behavior_hash:
        raise AssertionError("The real P08 preference presenter did not restore its initial Screensaver Off frame")
    unavailable = default_scenario("C06")
    runner.prepare(unavailable)
    unavailable_record = runner.capture()
    assert_questions(unavailable_record, unavailable)
    populated = default_scenario("C06", variant="populated")
    runner.prepare(populated)
    populated_record = runner.capture()
    assert_questions(populated_record, populated)
    edited = deepcopy(populated)
    edited["services"]["domain"]["questions"][0]["prompt"] = "Approve this synthetic change?"
    runner.prepare(edited)
    edited_record = runner.capture()
    assert_questions(edited_record, edited)
    if edited_record.metadata["pixel_sha256"] == populated_record.metadata["pixel_sha256"]:
        raise AssertionError("The actual pending-question renderer ignored an edited visible prompt")
    rejections = rejected_questions(device, populated)
    runner.prepare(unavailable)
    retired = runner.capture()
    assert_questions(retired, unavailable)
    if retired.metadata["pixel_sha256"] != unavailable_record.metadata["pixel_sha256"]:
        raise AssertionError("The default unavailable presenter retained a populated question fixture")
    runner.prepare(populated)
    assert_questions(runner.capture(), populated)
    runner.prepare(default_scenario("C04"))
    calls = runner.capture()
    if calls.metadata["frame_identity"]["question_count"] or calls.metadata["frame_identity"]["context"]["question_ids"]:
        raise AssertionError("Switching to C04 retained pending questions in its completed receipt")
    runner.prepare(p04)
    if runner.capture().metadata["pixel_sha256"] != p04_hash:
        raise AssertionError("Question inspection contaminated the final restored P04 frame")
    summary = {"inspected_cases": len(evidence), "global_monotonic_us": frozen_clock(device),
               "p04_restored": True, "question_rejections": rejections,
               "visible_question_count": populated_record.metadata["frame_identity"]["question_count"],
               "pairing_code_evidence": code_evidence,
               "screensaver_preference_evidence": "Native bridge write/readback and real P08 Off presentation; no standalone getter",
               "captures": evidence,
               "questions": {"default": str(unavailable_record.metadata_path.relative_to(ROOT)),
                             "populated": str(populated_record.metadata_path.relative_to(ROOT)),
                             "edited": str(edited_record.metadata_path.relative_to(ROOT))}}
    (directory / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary), flush=True)
    return 0


# base_class: Actual Tk/native fixture executed in a separately loaded native process.
# Returns only the explicit variant workflow, without duplicating inherited general UI suites.
def ui_suite(base_class):
    class InspectionUiTests(base_class):
        # self: Real variant combobox, generated typed question controls and native captured rows.
        # Raises AssertionError for lost common inputs, ignored variants, stale rows or changed baseline bytes.
        def test_question_variant_selection_preserves_common_inputs_and_baseline(self):
            window = self.window
            self.fill_scenario(default_scenario("C06"))
            original = self.apply_form()
            assert_questions(original, default_scenario("C06"))
            self.button("Preserve current as baseline").invoke()
            baseline = window.baseline
            retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
            window.inputs["brightness"].set("63")
            window.inputs["battery"].set("47")
            window.variant_control.set("populated")
            window.variant_control.event_generate("<<ComboboxSelected>>")
            self.root.update()
            self.assertEqual(self.errors, [])
            self.assertIs(window.current, original, "Selecting a variant silently replaced the current capture")
            form = window.form()
            self.assertEqual(form["variant"], "populated")
            self.assertEqual(form["common"]["settings"]["brightness_percent"], 63)
            self.assertEqual(form["common"]["observations"]["battery"]["level_percent"], 47)
            prompt_path = ("services", "domain", "questions", 0, "prompt")
            self.assertIn(prompt_path, window.case_fields)
            populated = self.apply_form()
            assert_questions(populated, form)
            self.assertIs(window.baseline, baseline)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
            window.case_fields[prompt_path][0].set("Approve this synthetic change?")
            edited = self.apply_form()
            self.assertNotEqual(edited.metadata["pixel_sha256"], populated.metadata["pixel_sha256"])
            self.reveal_widget(window.case_field_widgets[prompt_path], window.inspector_canvas)
            self.screenshot("studio-questions-populated.png")
            window.variant_control.set("default")
            window.variant_control.event_generate("<<ComboboxSelected>>")
            self.root.update()
            self.assertEqual(self.errors, [])
            self.assertNotIn(prompt_path, window.case_fields)
            restored_form = window.form()
            self.assertEqual(restored_form["common"], form["common"])
            restored = self.apply_form()
            assert_questions(restored, restored_form)
            self.assertEqual(restored.metadata["variant"], "default")
            self.assertIs(window.baseline, baseline)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
            self.screenshot("studio-questions-unavailable.png")

    return unittest.TestSuite([InspectionUiTests("test_question_variant_selection_preserves_common_inputs_and_baseline")])


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Inspection workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
