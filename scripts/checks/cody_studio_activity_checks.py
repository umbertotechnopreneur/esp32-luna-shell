"""Native notifications and current-turn ownership and frame contracts."""

import json
import ctypes
from pathlib import Path
import subprocess
import sys
import unittest
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from cody_studio_catalog import entry_for
from cody_studio_scenario import default_scenario, scenario_revision

OUTPUT = ROOT / "artifacts/simulator/studio/activity-checks"
CASES = {"N01": "cody.activity.events", "C07": "cody.activity.current-turn"}


class ActivityNativeTests(unittest.TestCase):
    """Exercise actual C++ presenters between independent P04 captures."""

    # self: Parent suite retaining the native worker's process diagnostics.
    # Raises AssertionError on a different receipt, changed pixels or leaked state.
    def test_activity_frames_repeat_and_restore_p04(self):
        directory = OUTPUT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        command = [sys.executable, "-B", str(Path(__file__).resolve()), "--worker", str(directory)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=240)
        for name in ("stdout", "stderr"):
            (directory / f"worker.{name}.log").write_text(getattr(result, name), encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr[-8000:] + result.stdout[-2000:])
        summary = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(summary["repeated_cases"], sorted(CASES))
        self.assertEqual(summary["p04_restorations"], 4)


# directory: New worker-owned directory underneath simulator artifacts.
# Raises the original native, provenance, frame or assertion failure.
def run_worker(directory):
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    directory = Path(directory).resolve(strict=True)
    if not directory.is_relative_to(OUTPUT.resolve()):
        raise ValueError("Activity worker directory escaped simulator artifacts")
    device = NativeDevice({"reason": "SIMULATED-STUDIO-ACTIVITY"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    subtitles = device.api.cody_host_studio_notification_subtitles
    subtitles.argtypes, subtitles.restype = [ctypes.POINTER(ctypes.c_char), ctypes.c_size_t], ctypes.c_int
    output = ctypes.create_string_buffer(192)
    if subtitles(output, 191) != 0x104 or subtitles(output, len(output)) != 0:
        raise AssertionError("Native notification presenter rejected its bounded contract")
    captions = [output.raw[index:index + 64].split(b"\0", 1)[0].decode("ascii")
                for index in range(0, 192, 64)]
    if captions != ["No notifications", "Local notices unavailable", "No notifications"]:
        raise AssertionError(f"Local notice error and recovery lost their distinct states: {captions!r}")
    runner = StudioRunner(device)
    display = default_scenario("P04")
    runner.prepare(display)
    original = runner.capture().metadata["pixel_sha256"]
    captures = {}
    restores = 0
    for identity, target in CASES.items():
        scenario = default_scenario(identity)
        images = []
        for _ in range(2):
            runner.prepare(scenario)
            metadata = runner.capture().metadata
            receipt = metadata["frame_identity"]
            if (metadata["id"] != entry_for(identity)["id"] or metadata["inventory_id"] != identity or
                    metadata["rendering_mode"] != "runtime-full" or receipt["target"] != target or
                    receipt["rendering_mode"] != "runtime-full" or
                    receipt["scenario_revision"] != scenario_revision(scenario) or
                    receipt["at_ms"] != 0 or receipt["abi"] != 3):
                raise AssertionError(f"Activity frame identity disagrees with {identity}")
            images.append(metadata["pixel_sha256"])
            runner.prepare(display)
            if runner.capture().metadata["pixel_sha256"] != original:
                raise AssertionError(f"{identity} contaminated the following P04 frame")
            restores += 1
        if images[0] != images[1]:
            raise AssertionError(f"{identity} produced different pixels after P04")
        captures[identity] = images[0]
    if captures["N01"] == captures["C07"]:
        raise AssertionError("Notifications and current turn rendered the same frame")
    print(json.dumps({"repeated_cases": sorted(CASES), "p04_restorations": restores}), flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--worker":
        raise SystemExit("Activity workers are internal; use device.ps1 check -Check cody-studio")
    raise SystemExit(run_worker(sys.argv[2]))
