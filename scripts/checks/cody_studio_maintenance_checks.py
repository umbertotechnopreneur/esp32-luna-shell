"""Read-only maintenance presenter checks through the native Studio runner."""

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

OUTPUT = ROOT / "artifacts/simulator/studio/maintenance-checks"
CASES = {
    "ui-v1:restart-pending": ("cody.settings.restart-pending", "P19", None),
    "ui-v1:reset-pending": ("cody.settings.reset-pending", "P21", None),
    "ui-v1:reset-failed": ("cody.settings.reset-failed", None, "P21"),
    "atlas-v1:39": ("cody.provisioning.debug", None, None),
}
ERROR_DESCRIPTION = "Synthetic ESP_ERR_TIMEOUT (0x107) in the host owner; no reset is performed"


class MaintenanceContractTests(unittest.TestCase):
    """The catalog identifies read-only maintenance and Debug samples."""

    # self: Named catalog contracts and their bounded static inputs.
    # Raises AssertionError for guessed aliases, non-static inputs or a different failure dependency.
    def test_read_only_maintenance_contracts_are_explicit(self):
        for identity, (target, inventory_id, family_id) in CASES.items():
            with self.subTest(identity=identity):
                entry = entry_for(identity)
                scenario = validate_scenario(default_scenario(identity))
                self.assertEqual((entry["native_target"], entry["rendering_mode"]), (target, "presenter-isolated"))
                self.assertEqual((entry["inventory_id"], entry.get("inventory_family_id")), (inventory_id, family_id))
                self.assertEqual(scenario["page"], {})
                self.assertEqual(scenario["services"], {})
                self.assertEqual(scenario["context"], {"target": identity, "selection": None,
                                                       "caller": None, "return_to": None})
                self.assertEqual(scenario["time"], {"mode": "controlled-static", "initial_ms": 0,
                                                   "advances_ms": [], "capture_at_ms": [0]})
                if family_id:
                    self.assertEqual(entry["fixed_dependencies"]["maintenance_error"], ERROR_DESCRIPTION)
                if identity == "atlas-v1:39":
                    self.assertEqual(entry["availability"], "profile-conditional")
                    self.assertEqual(entry["required_capabilities"], ["debug-provisioning"])
                    self.assertTrue(scenario["common"]["observations"]["network"]["connected"])
                    from copy import deepcopy
                    disconnected = deepcopy(scenario)
                    disconnected["common"]["observations"]["network"] = {"connected": False, "ssid": "", "rssi": None}
                    with self.assertRaises(ValueError):
                        validate_scenario(disconnected)
                invalid = dict(scenario)
                invalid["page"] = {"invented_operation": True}
                with self.assertRaises(ValueError):
                    validate_scenario(invalid)


    # self: Debug-only entry against a profile reporting the feature absent.
    # Raises AssertionError if an unavailable Debug presenter is offered as capturable.
    def test_debug_provisioning_requires_native_capability(self):
        from cody_studio_runner import StudioRunner

        class ReleaseDevice:
            # name: Requested native capability; this fixture has no Debug presenter.
            # Returns False for every capability, without touching device state.
            def studio_has_capability(self, name):
                return False

        available, reason = StudioRunner(ReleaseDevice()).availability(entry_for("atlas-v1:39"))
        self.assertFalse(available)
        self.assertIn("debug-provisioning", reason)


class MaintenanceNativeTests(unittest.TestCase):
    """Inspect the frames in one separately loaded native runtime."""

    # self: Suite process retaining diagnostics from its isolated native worker.
    # Raises the original worker or deadline error, without retry or fallback rendering.
    def test_maintenance_frames_repeat_and_retire_before_p04(self):
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
        self.assertTrue(lines, "Maintenance worker returned no evidence")
        summary = json.loads(lines[-1])
        self.assertEqual(summary["repeated_cases"], len(CASES))
        self.assertEqual(summary["p04_restorations"], len(CASES) * 2)
        self.assertEqual(summary["global_monotonic_us"], 1000000)
        self.assertIn("not independently observed", summary["operation_evidence"])
        print(lines[-1], flush=True)


# device: Started isolated runtime exposing the documented simulated monotonic clock.
# Raises AssertionError if a static presenter advanced the shared scenario clock.
def assert_clock(device):
    read = device.api.cody_host_monotonic_us
    read.argtypes, read.restype = [], ctypes.c_int64
    value = read()
    if value != 1000000:
        raise AssertionError(f"Maintenance inspection changed the global clock to {value} us")
    return value


# directory: New process-owned session and diagnostics beneath simulator artifacts.
# Raises the original owner, provenance, image, identity or scenario error.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Maintenance worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-MAINTENANCE"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    if not device.studio_has_capability("debug-provisioning"):
        raise AssertionError("Host native Debug provisioning capability is unavailable")
    display = default_scenario("P04")
    runner.prepare(display)
    display_hash = runner.capture().metadata["pixel_sha256"]
    captures, restores = [], 0
    for identity, (target, _, _) in CASES.items():
        scenario = default_scenario(identity)
        images = []
        for _ in range(2):
            runner.prepare(scenario)
            record = runner.capture()
            metadata = record.metadata
            receipt = metadata["frame_identity"]
            if (metadata["id"] != identity or metadata["rendering_mode"] != "presenter-isolated" or
                    receipt["target"] != target or receipt["rendering_mode"] != "presenter-isolated" or
                    receipt["abi"] != 3 or receipt["scenario_revision"] != scenario_revision(scenario) or
                    receipt["at_ms"] != 0):
                raise AssertionError(f"Maintenance frame identity differs from the prepared {identity}")
            if identity == "ui-v1:reset-failed" and metadata["fixed_dependencies"].get("maintenance_error") != ERROR_DESCRIPTION:
                raise AssertionError("Reset failure lacks its declared synthetic timeout")
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
    summary = {"repeated_cases": len(CASES), "p04_restorations": restores,
               "global_monotonic_us": assert_clock(device), "captures": captures,
               "operation_evidence": "Touch admission and power calls are not independently observed; host guards are source-reviewed"}
    (directory / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Maintenance workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
