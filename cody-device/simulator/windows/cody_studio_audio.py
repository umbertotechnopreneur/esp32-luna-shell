"""Explicit authored-cue audition in an isolated native process, never route evidence."""

from copy import deepcopy
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from uuid import uuid4

from cody_native_runtime import ROOT
from cody_studio_runner import ARTIFACT_ROOT, verify_current_build
from cody_studio_scenario import default_scenario, scenario_revision, unique_object, validate_scenario

SESSION_ROOT = ARTIFACT_ROOT / "studio/audio-sessions"
CUE_KEYS = tuple("codex:" + name for name in (
    "working", "reading", "editing", "running", "response-ready", "paused", "interrupted",
    "session-ended", "connection-lost", "connection-restored", "question", "answer-received",
    "checks-passed", "checks-failed", "approval-required", "question-reminder", "bookmark-saved",
    "questions-open", "activity-open")) + tuple("pet:" + name for name in (
    "boot-ding", "birth-rise", "birth-greeting", "tap", "jump", "surprise", "first-boot-melody"))
STATES = frozenset(("idle", "dispatched", "playing", "completed", "dropped-busy", "dropped-cooldown",
                    "suppressed", "expired", "failed", "unavailable"))
PENDING = frozenset(("idle", "dispatched", "playing"))


class AuditionReceipt(ctypes.Structure):
    """Exact capacity queried from the native audition owner before admission."""
    _fields_ = [("abi", ctypes.c_uint32), ("size", ctypes.c_uint32), ("cue_id", ctypes.c_uint32),
                ("sequence", ctypes.c_uint32), ("error", ctypes.c_int32), ("state", ctypes.c_char * 24)]


class AudioProbe(ctypes.Structure):
    """Effective firmware PCM observed only by the explicitly selected test sink."""
    _fields_ = [("abi", ctypes.c_uint32), ("size", ctypes.c_uint32), ("blocks", ctypes.c_uint64),
                ("frames", ctypes.c_uint64), ("nonzero_frames", ctypes.c_uint64),
                ("effective_hash", ctypes.c_uint64), ("last_rate", ctypes.c_uint32),
                ("peak", ctypes.c_uint32), ("stored_frames", ctypes.c_uint32), ("truncated", ctypes.c_uint32)]


# directory: This job's unique artifact-owned directory.
# value: JSON-compatible current owner outcome; readers see complete replacements.
# Raises serialization/filesystem errors without damaging the previous status.
def write_status(directory, value):
    temporary = directory / "status.tmp.json"
    temporary.write_text(json.dumps(value, ensure_ascii=True), encoding="utf-8")
    temporary.replace(directory / "status.json")


class AudioAudition:
    """One disposable native runtime with its own settings, clock and speaker admission."""

    # directory: Unique artifacts/simulator/studio/audio-sessions directory.
    # process: Child runtime; the parent never loads another native library.
    # revision: Canonical original scenario identity retained through audition.
    # cue: Validated semantic cue key.
    # build: Verified build identity selected before launch.
    # Exceptions: Initialization creates no additional native state.
    def __init__(self, directory, process, revision, cue, build):
        self.directory, self.process = directory, process
        self.revision, self.cue, self.build = revision, cue, build
        self.done, self.result, self.error = False, None, None
        self.started = time.monotonic()

    # path: Atomically published status belonging to this isolated native job.
    # required: True only after the child exit barrier, when a final publication must exist.
    # Raises ValueError for changed scenario/cue/build identity or malformed outcomes.
    # Returns None for a missing in-progress publication; other I/O failures remain errors.
    def read_status(self, path, *, required=False):
        try:
            value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        except FileNotFoundError:
            # On a replace-based filesystem a reader may briefly miss the name.
            # The subsequent process barrier decides when absence becomes fatal.
            if not required:
                return None
            self.cancel()
            raise
        except (OSError, ValueError, UnicodeError):
            self.cancel()
            raise
        if (not isinstance(value, dict) or value.get("schema") != "cody.studio.audio.v1" or
                value.get("scenario_revision") != self.revision or value.get("cue") != self.cue or
                value.get("library_sha256") != self.build["library_sha256"] or
                value.get("state") not in STATES or type(value.get("error")) is not int):
            self.cancel()
            raise ValueError("Invalid native audition identity or status")
        return value

    # scenario: Current validated form; only a copied synthetic clock/policy reaches the child.
    # cue: Textual authored cue key; no numeric enum is accepted.
    # audio_mode: Host output or the explicit native PCM probe used by checks.
    # Raises invalid inputs, stale provenance, packaging or subprocess launch failures.
    @classmethod
    def start(cls, scenario, cue, audio_mode="host-output"):
        value = validate_scenario(scenario)
        if cue not in CUE_KEYS:
            raise ValueError(f"Unknown authored audio cue: {cue!r}")
        if audio_mode not in ("host-output", "test-probe"):
            raise ValueError("Audition requires host-output or the explicit test-probe")
        if getattr(sys, "frozen", False):
            raise ValueError("Native cue audition is unavailable in packaged simulator builds")
        revision = scenario_revision(value)
        build = verify_current_build()
        directory = SESSION_ROOT / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        request = {"scenario": value, "cue": cue, "audio_mode": audio_mode, "build": build}
        (directory / "request.json").write_text(json.dumps(request), encoding="utf-8")
        with (directory / "stdout.log").open("wb") as stdout, (directory / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()), str(directory)],
                                       cwd=ROOT, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
        return cls(directory, process, revision, cue, build)

    # No parameters: collect only this child's complete, identity-checked status.
    # Raises corrupt status/schema errors; worker failures remain in error and original logs.
    def poll(self):
        if self.done:
            return self.result
        path = self.directory / "status.json"
        value = self.read_status(path)
        if value is not None:
            self.result = value
        code = self.process.poll()
        if code is None and time.monotonic() - self.started > 25:
            self.cancel()
            self.error = "Native cue audition exceeded its 25-second deadline"
        elif code is not None:
            self.process.wait()
            # Read after the process barrier: its final probe/publication may
            # have replaced an earlier terminal admission between poll calls.
            try:
                self.result = self.read_status(path, required=True)
            except FileNotFoundError as error:
                diagnostics = (self.directory / "stderr.log").read_text(encoding="utf-8", errors="replace")
                self.error = f"Native cue audition failed ({code}) · final status missing: {diagnostics[-2500:]}"
                raise RuntimeError(self.error) from error
            self.done = True
            if self.process.stdin is not None:
                self.process.stdin.close()
            if code != 0 or self.result is None or self.result["state"] in PENDING:
                diagnostics = (self.directory / "stderr.log").read_text(encoding="utf-8", errors="replace")
                outcome = (f"{self.result['state']} · native error 0x{self.result['error'] & 0xffffffff:08x}"
                           if self.result is not None else "no native outcome published")
                self.error = f"Native cue audition failed ({code}) · {outcome}: {diagnostics[-2500:]}"
        return self.result

    # No parameters: kill and reap only this isolated audition; preserve all evidence artifacts.
    # Raises original OS/wait errors rather than leaving a child behind.
    def cancel(self):
        if not self.done:
            if self.process.poll() is None:
                self.process.kill()
            self.process.wait(timeout=5)
            if self.process.stdin is not None:
                self.process.stdin.close()
            self.done, self.error = True, "Audition stopped"


# receipt: Exact native owner outcome from this admitted request.
# Raises ValueError for an unsupported ABI or unrecognized semantic state.
def receipt_value(receipt):
    state = receipt.state.decode("ascii")
    if receipt.abi != 1 or receipt.size != ctypes.sizeof(receipt) or state not in STATES:
        raise ValueError("Invalid native cue audition receipt")
    return {"cue_id": receipt.cue_id, "sequence": receipt.sequence, "state": state, "error": receipt.error}


# argv: Exactly one private session directory supplied by AudioAudition.start.
# Raises native/provenance/settings/clock errors with retained child diagnostics.
def main(argv=None):
    arguments = sys.argv[1:] if argv is None else argv
    if len(arguments) != 1:
        raise ValueError("Cue audition requires one isolated artifact session directory")
    directory = Path(arguments[0]).resolve(strict=True)
    if not directory.is_dir() or directory.parent != SESSION_ROOT.resolve():
        raise ValueError("Cue audition session escaped simulator artifacts")
    parent_fd = sys.stdin.fileno()

    # No parameters: a departed parent cancels the child's native workers immediately.
    def watch_parent():
        os.read(parent_fd, 1)
        os._exit(125)

    # No parameters: real wall time bounds a hung foreign call independently of the scenario.
    def watch_timeout():
        time.sleep(25)
        os._exit(124)

    threading.Thread(target=watch_parent, daemon=True, name="cue-parent-watch").start()
    threading.Thread(target=watch_timeout, daemon=True, name="cue-deadline").start()
    request = json.loads((directory / "request.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if not isinstance(request, dict) or set(request) != {"scenario", "cue", "audio_mode", "build"}:
        raise ValueError("Unsupported native audition request")
    value = validate_scenario(request["scenario"])
    cue, mode = request["cue"], request["audio_mode"]
    if cue not in CUE_KEYS or mode not in ("host-output", "test-probe"):
        raise ValueError("Unknown native cue or output mode")
    build = verify_current_build()
    if build != request["build"]:
        raise ValueError("Native build changed before cue audition")
    # A static quiet-hours page prepares only existing settings/clock owners.
    # It is not saved as a new capture and is never used as route acceptance.
    initial = default_scenario("P12")
    initial["common"]["observations"] = deepcopy(value["common"]["observations"])
    for key in ("brightness_percent", "auto_rotate", "orientation_degrees"):
        initial["common"]["settings"][key] = value["common"]["settings"][key]
    initial["common"]["settings"].update(sounds_enabled=False,
                                          playback_volume=value["common"]["settings"].get("playback_volume", 90))
    for key in ("quiet_hours_enabled", "quiet_start_minute", "quiet_end_minute"):
        if key in value["page"]:
            initial["page"][key] = value["page"][key]
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    device = NativeDevice({"reason": "SIMULATED-STUDIO-AUDITION"}, directory / "session.nvs", 80, True, 1,
                          deterministic_clock=True, audio_mode=mode)
    device.seed_studio_boot_replay()
    device.seed_studio_voice_stopped()
    device.start()
    device.verify_studio_boot_replay()
    StudioRunner(device).prepare(initial)
    api = device.api
    query_size = api.cody_host_audio_audition_size
    query_size.argtypes, query_size.restype = [], ctypes.c_size_t
    if query_size() != ctypes.sizeof(AuditionReceipt):
        raise ValueError("Native cue audition ABI size mismatch")
    begin, poll = api.cody_host_audio_audition_begin, api.cody_host_audio_audition_poll
    begin.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32,
                      ctypes.POINTER(AuditionReceipt), ctypes.c_size_t]
    begin.restype = ctypes.c_int
    poll.argtypes, poll.restype = [ctypes.POINTER(AuditionReceipt), ctypes.c_size_t], ctypes.c_int
    receipt = AuditionReceipt()
    settings = value["common"]["settings"]
    result = begin(cue.encode("ascii"), int(settings.get("sounds_enabled", False)),
                   settings.get("playback_volume", 90), ctypes.byref(receipt), ctypes.sizeof(receipt))
    if result and not receipt.state:
        raise RuntimeError(f"Native audition admission failed: esp_err_t=0x{result & 0xffffffff:08x}")
    while True:
        status = {"schema": "cody.studio.audio.v1", "scenario_revision": scenario_revision(value),
                  "cue": cue, "library_sha256": build["library_sha256"],
                  "audio": device.audio_status(), **receipt_value(receipt)}
        write_status(directory, status)
        if status["state"] not in PENDING:
            break
        time.sleep(.025)
        error = poll(ctypes.byref(receipt), ctypes.sizeof(receipt))
        if error:
            raise RuntimeError(f"Native audition status failed: esp_err_t=0x{error & 0xffffffff:08x}")
    if mode == "test-probe":
        probe = AudioProbe()
        query = api.cody_host_audio_test_snapshot
        query.argtypes, query.restype = [ctypes.POINTER(AudioProbe), ctypes.c_size_t], ctypes.c_int
        error = query(ctypes.byref(probe), ctypes.sizeof(probe))
        if error or probe.abi != 1 or probe.size != ctypes.sizeof(probe):
            raise RuntimeError(f"Native PCM probe failed: esp_err_t=0x{error & 0xffffffff:08x}")
        status["probe"] = {name: getattr(probe, name) for name, _ in probe._fields_}
    if verify_current_build() != build:
        raise ValueError("Native build changed during cue audition")
    write_status(directory, status)
    return 1 if status["error"] or status["state"] in ("failed", "unavailable") else 0


if __name__ == "__main__":
    raise SystemExit(main())
