"""Isolated native Codex-event inspection contracts and completed frames."""

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

OUTPUT = ROOT / "artifacts/simulator/studio/event-checks"
CASES = {
    "atlas-v1:21": ("cody.event.pr-pending", "N09", None),
    "atlas-v1:22": ("cody.event.checks-running", None, "N09"),
    "atlas-v1:23": ("cody.event.checks-failed", None, "N09"),
    "atlas-v1:24": ("cody.event.check-failures", "N10", None),
    "atlas-v1:26": ("cody.event.question", "N03", None),
    "atlas-v1:27": ("cody.event.question-choices", "N04", None),
    "ui-v1:review-reply": ("cody.event.review-reply", "N05", None),
    "atlas-v1:31": ("cody.event.reply-sent", "N07", None),
    "ui-v1:question-queue": ("cody.event.question-queue", None, "N03"),
    "ui-v1:sending-reply": ("cody.event.sending-reply", "N06", None),
    "ui-v1:notice": ("cody.event.notice", None, None),
    "ui-v1:approval-pc": ("cody.event.approval-pc", "N08", None),
    "ui-v1:event-unavailable": ("cody.event.unavailable", "N11", None),
}


class EventContractTests(unittest.TestCase):
    """Only source-owned identities and explicit synthetic inputs are admitted."""

    # self: PR and failed-check observations projected by the native fixture owner.
    # Raises AssertionError for contradictory progress or an unexpected PR caption.
    def test_pr_caption_and_failed_check_counts_are_consistent(self):
        fixture = entry_for("atlas-v1:21")["fixed_dependencies"]["codex_event"]
        self.assertEqual(fixture["title"], "PR pending")
        for identity in ("atlas-v1:23", "atlas-v1:24"):
            fixture = entry_for(identity)["fixed_dependencies"]["codex_event"]
            self.assertEqual(fixture["passed"], 1)
            self.assertEqual(len(fixture["failed_checks"]), 2)
            self.assertEqual(fixture["passed"] + len(fixture["failed_checks"]), fixture["total"])

    # self: Question notice, queue and delivery fixtures with distinct pending counts.
    # Raises AssertionError if a delivered or in-flight request is offered as a pending row.
    def test_question_delivery_retains_only_the_other_pending_row(self):
        first = "11111111111111111111111111111111"
        second = "22222222222222222222222222222222"
        for identity in ("atlas-v1:26", "atlas-v1:27", "ui-v1:question-queue", "ui-v1:review-reply"):
            fixture = entry_for(identity)["fixed_dependencies"]["codex_event"]
            self.assertEqual((fixture["pending_question_count"], fixture["pending_entry_count"]), (2, 2))
            self.assertEqual(fixture["pending_request_ids"], [first, second])
        for identity, count in (("ui-v1:sending-reply", 2), ("atlas-v1:31", 1)):
            fixture = entry_for(identity)["fixed_dependencies"]["codex_event"]
            self.assertEqual((fixture["pending_question_count"], fixture["pending_entry_count"]), (count, 1))
            self.assertEqual(fixture["pending_request_ids"], [second])
            self.assertEqual(fixture["request_id"], first)
            self.assertEqual(fixture["selected_id"], "approve")

    # self: Explicit read failure and local pending-choice review fixture contracts.
    # Raises AssertionError if the failure invents an event or the review admits a reply.
    def test_review_and_unavailable_inputs_are_distinct(self):
        review = entry_for("N05")["fixed_dependencies"]["codex_event"]
        self.assertEqual((review["state"], review["stage"], review["navigation_selected_id"]),
                         ("Pending", "Confirm", "approve"))
        self.assertNotIn("selected_id", review)
        unavailable = entry_for("ui-v1:event-unavailable")["fixed_dependencies"]["codex_event"]
        self.assertEqual(unavailable["observation_error"], "ESP_ERR_TIMEOUT")
        self.assertEqual(unavailable["event_observation"], "not published")
        self.assertNotIn("request_id", unavailable)

    # self: Catalog and scenario contracts, without loading a native library.
    # Raises AssertionError for a guessed inventory alias or incoherent fixture.
    def test_event_contracts_require_explicit_connected_synthetic_network(self):
        for identity, (target, inventory_id, family_id) in CASES.items():
            with self.subTest(identity=identity):
                entry = entry_for(identity)
                scenario = default_scenario(identity)
                self.assertEqual(entry["native_target"], target)
                self.assertEqual(entry["rendering_mode"], "presenter-isolated")
                self.assertEqual((entry.get("inventory_id"), entry.get("inventory_family_id")),
                                 (inventory_id, family_id))
                self.assertEqual(scenario["context"], {"target": identity, "selection": None,
                                                       "caller": None, "return_to": None})
                self.assertEqual((scenario["page"], scenario["services"]), ({}, {}))
                self.assertTrue(scenario["common"]["observations"]["network"]["connected"])
                self.assertEqual(scenario["time"], {"mode": "controlled-static", "initial_ms": 0,
                                                   "advances_ms": [], "capture_at_ms": [0]})
                self.assertEqual(validate_scenario(scenario), scenario)
                disconnected = deepcopy(scenario)
                disconnected["common"]["observations"]["network"] = {"connected": False, "ssid": "", "rssi": None}
                with self.assertRaises(ValueError):
                    validate_scenario(disconnected)
                extra = deepcopy(scenario)
                extra["page"]["invented_event"] = True
                with self.assertRaises(ValueError):
                    validate_scenario(extra)


class EventNativeTests(unittest.TestCase):
    """Validate static presentation and owner retirement in one isolated process."""

    # self: Suite parent retaining all worker diagnostics under simulator artifacts.
    # Raises the original worker or timeout failure; no renderer fallback is permitted.
    def test_event_frames_repeat_and_retire_before_p04(self):
        directory = OUTPUT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        command = [sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(directory)]
        try:
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=240)
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
        self.assertTrue(lines, "Event worker returned no result")
        summary = json.loads(lines[-1])
        self.assertEqual(summary["repeated_cases"], len(CASES))
        self.assertEqual(summary["p04_restorations"], len(CASES) * 2)
        self.assertTrue(summary["unavailable_event_recovery"])
        self.assertEqual(summary["global_monotonic_us"], 1000000)
        self.assertIn("not independently observed", summary["operation_evidence"])
        print(lines[-1], flush=True)


# device: Running isolated runtime exposing the documented simulated monotonic read.
# Raises AssertionError if a static presenter advanced the shared scenario clock.
def assert_clock(device):
    read = device.api.cody_host_monotonic_us
    read.argtypes, read.restype = [], ctypes.c_int64
    value = read()
    if value != 1000000:
        raise AssertionError(f"Event inspection changed the global clock to {value} us")
    return value


# directory: New process-owned session and diagnostics beneath simulator artifacts.
# Raises the original owner, build, frame or scenario failure without substitution.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Event worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-EVENTS"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    display = default_scenario("P04")
    runner.prepare(display)
    display_hash = runner.capture().metadata["pixel_sha256"]
    captures, restores, pixel_hashes = [], 0, {}
    for identity, (target, _, _) in CASES.items():
        scenario = default_scenario(identity)
        images = []
        for _ in range(2):
            runner.prepare(scenario)
            record = runner.capture()
            metadata, receipt = record.metadata, record.metadata["frame_identity"]
            if (metadata["id"] != identity or metadata["rendering_mode"] != "presenter-isolated" or
                    receipt["target"] != target or receipt["rendering_mode"] != "presenter-isolated" or
                    receipt["scenario_revision"] != scenario_revision(scenario) or receipt["at_ms"] != 0 or
                    receipt["abi"] != 3):
                raise AssertionError(f"Event frame identity differs from the prepared {identity}")
            images.append(metadata["pixel_sha256"])
            captures.append(str(record.metadata_path.relative_to(ROOT)))
            assert_clock(device)
            runner.prepare(display)
            if runner.capture().metadata["pixel_sha256"] != display_hash:
                raise AssertionError(f"{identity} contaminated the following P04 frame")
            restores += 1
            assert_clock(device)
        if images[0] != images[1]:
            raise AssertionError(f"{identity} did not reproduce its static native pixels")
        pixel_hashes[identity] = images[0]
    distinct = ("atlas-v1:21", "atlas-v1:23", "ui-v1:notice")
    if len({pixel_hashes[identity] for identity in distinct}) != len(distinct):
        raise AssertionError("PR pending, checks failed and notice rendered indistinguishable native frames")
    if pixel_hashes["atlas-v1:22"] == pixel_hashes["ui-v1:approval-pc"]:
        raise AssertionError("Checks running and approval on PC rendered indistinguishable native frames")
    if pixel_hashes["ui-v1:review-reply"] == pixel_hashes["atlas-v1:27"]:
        raise AssertionError("Reply review and choice list rendered indistinguishable native frames")
    # Exercise direct owner transitions too: a failed read must not retain the
    # previous event, and its injected failure must retire before the next event.
    for identity in ("atlas-v1:26", "ui-v1:event-unavailable", "atlas-v1:26", "ui-v1:event-unavailable"):
        runner.prepare(default_scenario(identity))
        if runner.capture().metadata["pixel_sha256"] != pixel_hashes[identity]:
            raise AssertionError(f"Direct event recovery changed {identity}'s completed frame")
        assert_clock(device)
    summary = {"repeated_cases": len(CASES), "p04_restorations": restores,
               "unavailable_event_recovery": True,
               "global_monotonic_us": assert_clock(device), "captures": captures,
               "operation_evidence": "Touch admission and external service calls are not independently observed; host guards are source-reviewed"}
    (directory / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Event workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
