"""Internal isolated process for deterministic native Studio worker replay.

This module is started by StudioRunner, not a separate public command. Each
request owns a fresh host runtime and settings store under simulator artifacts.
"""

from pathlib import Path
import json
import subprocess
import sys
from uuid import uuid4

from cody_native_runtime import ROOT
from cody_studio_runner import ARTIFACT_ROOT, load_capture
from cody_studio_scenario import unique_object, validate_scenario

_RESULT_PREFIX = "CODY_STUDIO_WORKER_CAPTURE="
_SESSION_ROOT = ARTIFACT_ROOT / "studio/worker-sessions"


# scenario: Validated immutable controlled-worker scenario.
# requested_at_ms: Explicit declared capture boundary to replay from zero.
# register_process: Runner-owned callback that registers and clears its child.
# Returns a hash-checked native capture made by a new process and settings owner.
# Raises source, subprocess, native, provenance and capture failures without fallback.
def run_isolated_worker(scenario, requested_at_ms, register_process):
    value = validate_scenario(scenario)
    if (value["time"]["mode"] != "controlled-worker" or type(requested_at_ms) is not int or
            requested_at_ms not in value["time"]["capture_at_ms"]):
        raise ValueError("Isolated worker requires a declared controlled-worker capture boundary")
    if getattr(sys, "frozen", False):
        raise ValueError("Controlled worker replay is unavailable in packaged simulator builds")
    directory = (_SESSION_ROOT / uuid4().hex).resolve()
    if not directory.is_relative_to(_SESSION_ROOT.resolve()):
        raise ValueError("Worker session escaped simulator artifacts")
    directory.mkdir(parents=True, exist_ok=False)
    request = json.dumps({"scenario": value, "requested_at_ms": requested_at_ms}, ensure_ascii=True)
    process = subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()), str(directory)],
                               cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    stdout = stderr = ""
    try:
        register_process(process)
        try:
            stdout, stderr = process.communicate(request, timeout=240)
        except subprocess.TimeoutExpired as error:
            process.kill()
            stdout, stderr = process.communicate()
            raise TimeoutError("Isolated native worker exceeded its 240-second deadline") from error
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        register_process(None)
        pending_error = sys.exc_info()[0] is not None
        try:
            (directory / "worker.stdout.log").write_text(stdout, encoding="utf-8")
            (directory / "worker.stderr.log").write_text(stderr, encoding="utf-8")
        except OSError as error:
            if not pending_error:
                raise
            print(f"Isolated worker log persistence failed: {error}", file=sys.stderr)
    if process.returncode:
        raise RuntimeError(f"Isolated native worker failed ({process.returncode}):\n"
                           f"{stderr[-4000:]}\n{stdout[-4000:]}")
    lines = [line[len(_RESULT_PREFIX):] for line in stdout.splitlines() if line.startswith(_RESULT_PREFIX)]
    if len(lines) != 1:
        raise ValueError("Isolated native worker did not return one capture identity")
    relative = json.loads(lines[0])
    if not isinstance(relative, str) or Path(relative).is_absolute():
        raise ValueError("Isolated worker returned an invalid capture path")
    return load_capture(ROOT / relative)


# argv: Internal session directory supplied by run_isolated_worker.
# Returns the worker exit status after a native completed frame has been saved.
# Raises validation, filesystem and native owner failures to the parent process.
def main(argv=None):
    arguments = sys.argv[1:] if argv is None else argv
    if len(arguments) != 1:
        raise ValueError("An isolated worker requires its unique artifact session directory")
    directory = Path(arguments[0]).resolve(strict=True)
    if not directory.is_dir() or not directory.is_relative_to(_SESSION_ROOT.resolve()):
        raise ValueError("Isolated worker session must be under simulator artifacts")
    request_text = sys.stdin.read(2 * 1024 * 1024 + 1)
    if len(request_text) > 2 * 1024 * 1024:
        raise ValueError("Isolated worker request is too large")
    request = json.loads(request_text, object_pairs_hook=unique_object)
    if not isinstance(request, dict) or set(request) != {"scenario", "requested_at_ms"}:
        raise ValueError("Isolated worker request requires exactly scenario and requested_at_ms")
    value = validate_scenario(request["scenario"])
    requested = request["requested_at_ms"]
    if (value["time"]["mode"] != "controlled-worker" or type(requested) is not int or
            requested not in value["time"]["capture_at_ms"]):
        raise ValueError("Isolated worker request does not select a declared worker capture")
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    device = NativeDevice({"reason": "SIMULATED-STUDIO-WORKER"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    runner.prepare_worker_local(value, requested)
    record = runner.capture()
    print(_RESULT_PREFIX + json.dumps(str(record.metadata_path.relative_to(ROOT))), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
