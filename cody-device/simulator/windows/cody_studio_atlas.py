"""Isolated native atlas jobs shared by Cody Studio and its public generator.

Only a verified, finished suite can replace the last-valid pointer. Native
libraries remain in the child process, so a rebuild cannot contaminate the
interactive runtime. Static catalog fixtures deliberately avoid nested workers.
"""

from copy import deepcopy
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from uuid import uuid4

from cody_native_runtime import ROOT
from cody_studio_catalog import entry_for, load_catalog
from cody_studio_runner import ARTIFACT_ROOT, load_capture, verify_current_build
from cody_studio_scenario import default_scenario, scenario_revision, unique_object
from runtime_paths import simulator_library_path

ATLAS_ROOT = ARTIFACT_ROOT / "studio/atlas"
SCHEMA = "cody.studio.atlas.v1"
SUITES = ("pilot", "supported", "all")


# path: Destination owned by this atlas, including its replaceable publication pointer.
# value: JSON-compatible contract; temporary files stay beside their destination.
# Raises filesystem/serialization failures without replacing the previous destination.
def atomic_json(path, value):
    temporary = path.with_name(path.name + "." + uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


# identities: Optional unique stable identities or mapped inventory IDs.
# suite: Declared catalog selection when identities is None.
# Raises ValueError for empty, duplicate or unknown selections before spawning a child.
def selected_entries(identities=None, suite="pilot"):
    if suite not in SUITES:
        raise ValueError(f"Unknown Studio atlas suite: {suite!r}")
    if identities is not None:
        if not isinstance(identities, (list, tuple)) or not identities:
            raise ValueError("Atlas selection requires a nonempty list of identities")
        entries = [entry_for(identity) for identity in identities]
    else:
        entries = [entry for entry in load_catalog() if suite == "all" or
                   (suite == "pilot" and entry.get("suite") == "pilot") or
                   (suite == "supported" and entry.get("scenario") is not None)]
    if not entries or len({entry["id"] for entry in entries}) != len(entries):
        raise ValueError("Atlas selection is empty or contains duplicate screen identities")
    return entries


# entries: Current source contracts for the selected suite.
# Returns repository-relative source hashes for the catalog and selected fixtures.
# Raises invalid scenario/source paths before any native process is started.
def contract_inputs(entries):
    paths = {Path(__file__).with_name("studio_catalog.json"),
             Path(__file__).with_name("cody_studio_catalog.py"), ROOT / "docs/cody/screen-catalog.md"}
    for entry in entries:
        if entry.get("scenario") is not None:
            default_scenario(entry["id"])
            paths.add(Path(__file__).with_name("scenarios") / entry["scenario"])
    result = {}
    for path in paths:
        path = path.resolve(strict=True)
        if not path.is_relative_to(ROOT) or path.is_relative_to(ARTIFACT_ROOT):
            raise ValueError("Atlas contract source escaped the source checkout")
        result[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


# inputs: Retained source hashes for the selected catalog and scenario fixtures.
# Raises ValueError or OSError when a scenario/catalog changes during generation.
def verify_contract_inputs(inputs):
    if not isinstance(inputs, dict) or not inputs:
        raise ValueError("Atlas is missing its catalog/scenario provenance")
    for relative, digest in inputs.items():
        if (not isinstance(relative, str) or not relative or "\\" in relative or Path(relative).is_absolute() or
                not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest)):
            raise ValueError("Atlas catalog/scenario provenance is invalid")
        path = (ROOT / relative).resolve(strict=True)
        if not path.is_relative_to(ROOT) or path.is_relative_to(ARTIFACT_ROOT):
            raise ValueError("Atlas contract source escaped the source checkout")
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Studio catalog/scenario changed during atlas generation: {relative}")


# relative: Repository-relative metadata path from a retained atlas contract.
# Raises ValueError or OSError for absolute, escaping or nonexistent paths.
def artifact_path(relative):
    if not isinstance(relative, str) or not relative or "\\" in relative or Path(relative).is_absolute():
        raise ValueError("Atlas paths require repository-relative artifact names")
    path = (ROOT / relative).resolve(strict=True)
    if not path.is_relative_to(ARTIFACT_ROOT):
        raise ValueError("Atlas path escaped simulator artifacts")
    return path


# No parameters: resolve the explicitly published, hash-checked last-valid result.
# Returns None only when no atlas has ever been published.
# Raises original I/O/JSON/hash errors; corrupt publication never silently falls back.
def latest_atlas_path():
    pointer_path = ATLAS_ROOT / "last-valid.json"
    if not pointer_path.exists():
        return None
    pointer = json.loads(pointer_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if not isinstance(pointer, dict) or set(pointer) != {"schema", "manifest", "sha256"} or pointer["schema"] != "cody.studio.atlas-pointer.v1":
        raise ValueError("Unsupported Studio atlas publication pointer")
    path = artifact_path(pointer["manifest"])
    if not path.is_relative_to(ATLAS_ROOT.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != pointer["sha256"]:
        raise ValueError("Studio atlas publication hash mismatch")
    return path


# path: Optional historical atlas manifest; default selects the published last-valid result.
# Returns validated historical evidence without claiming that its build is current.
# Raises I/O, schema, capture or hash errors instead of presenting damaged frames.
def load_atlas(path=None):
    path = latest_atlas_path() if path is None else Path(path).resolve(strict=True)
    if path is None:
        raise ValueError("No valid Studio atlas has been published")
    if not path.is_relative_to(ATLAS_ROOT.resolve()):
        raise ValueError("Studio atlas manifest must remain beneath its artifact directory")
    value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if (not isinstance(value, dict) or value.get("schema") != SCHEMA or
            not isinstance(value.get("run_id"), str) or not re.fullmatch(r"[0-9a-f]{32}", value["run_id"]) or
            value.get("status") not in ("complete", "failed", "cancelled", "stale") or
            value.get("suite") not in SUITES or type(value.get("auto")) is not bool or
            not isinstance(value.get("build"), dict) or not isinstance(value.get("inventory"), dict) or
            not isinstance(value.get("contract_inputs"), dict) or not value["contract_inputs"] or
            not isinstance(value.get("cases"), list) or not value["cases"]):
        raise ValueError("Unsupported Studio atlas manifest")
    if (not isinstance(value.get("selection"), list) or
            value["selection"] != [case.get("id") if isinstance(case, dict) else None for case in value["cases"]]):
        raise ValueError("Atlas result does not cover its declared selection")
    seen = set()
    for case in value["cases"]:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or case["id"] in seen:
            raise ValueError("Atlas cases require unique stable identities")
        entry = entry_for(case["id"])
        if case["id"] != entry["id"]:
            raise ValueError("Atlas cases require qualified source identities")
        if (case.get("inventory_id") != entry.get("inventory_id") or
                case.get("inventory_family_id") != entry.get("inventory_family_id")):
            raise ValueError("Atlas case inventory identity disagrees with its source mapping")
        seen.add(case["id"])
        acquisition = case.get("acquisition")
        if acquisition not in ("captured", "failed", "unsupported", "not-applicable"):
            raise ValueError("Invalid Studio atlas acquisition state")
        if acquisition != "captured":
            if not isinstance(case.get("reason"), str) or not case["reason"].strip() or case.get("capture") is not None:
                raise ValueError("Atlas absence requires a reason and no captured frame")
            if value["status"] == "complete" and acquisition == "failed":
                raise ValueError("A failed atlas case cannot belong to a complete result")
            continue
        capture_path = artifact_path(case.get("capture"))
        if hashlib.sha256(capture_path.read_bytes()).hexdigest() != case.get("metadata_sha256"):
            raise ValueError("Atlas capture metadata hash mismatch")
        metadata = load_capture(capture_path).metadata
        fields = ("id", "inventory_id", "inventory_family_id", "variant", "scenario_revision", "source", "symbol", "rendering_mode")
        if any(metadata.get(field) != case.get(field) for field in fields) or metadata["build"] != value["build"]:
            raise ValueError("Atlas case identity disagrees with its completed native frame")
        if metadata.get("inventory") != value["inventory"]:
            raise ValueError("Atlas inventory revision disagrees with its completed native frame")
    return value


# path: Immutable finished manifest, checked again before atomic publication.
# expected_build: Validated build fingerprint selected when the job started.
# expected_contracts: Optional exact contract hashes selected when the job started.
# Raises stale/failed/damaged result errors while preserving the last valid pointer.
def publish_atlas(path, expected_build, expected_contracts=None):
    value = load_atlas(path)
    request_path = Path(path).parent / "request.json"
    request = json.loads(request_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if (not isinstance(request, dict) or request.get("schema") != "cody.studio.atlas-request.v1" or
            value["run_id"] != Path(path).parent.name or value["selection"] != request.get("identities") or
            value["suite"] != request.get("suite") or value.get("auto") != request.get("auto") or
            value["build"] != request.get("build") or value["contract_inputs"] != request.get("contract_inputs")):
        raise ValueError("Atlas result does not match its original request; previous result preserved")
    if value["status"] != "complete" or not any(case["acquisition"] == "captured" for case in value["cases"]):
        raise ValueError("Atlas did not finish with valid native captures; previous result preserved")
    if expected_contracts is not None and value["contract_inputs"] != expected_contracts:
        raise ValueError("Atlas catalog/scenario provenance changed; previous result preserved")
    verify_contract_inputs(value["contract_inputs"])
    if value["build"] != expected_build or verify_current_build() != expected_build:
        raise ValueError("Native build changed during atlas generation; previous result preserved")
    atomic_json(ATLAS_ROOT / "last-valid.json", {"schema": "cody.studio.atlas-pointer.v1",
                "manifest": Path(path).relative_to(ROOT).as_posix(),
                "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest()})


class BuildWatcher:
    """Observe native build publications only; atlas output never triggers itself."""

    # No parameters: seed the current valid build without requesting regeneration.
    # Errors remain in error for the UI; an incomplete build is never accepted.
    def __init__(self):
        self.error = None
        self._stamp = None
        self._fingerprint = None
        self._initialized = False
        self.poll()
        self._initialized = True

    # No parameters: return only a newly validated manifest/library fingerprint.
    # Missing/in-progress/stale builds remain visible through error, without raising.
    def poll(self):
        library = simulator_library_path(ROOT, "graphics-device")
        paths = (library.parent / "manifest.json", library)
        try:
            stamp = tuple((path.stat().st_mtime_ns, path.stat().st_size, path.stat().st_ino) for path in paths)
            if stamp == self._stamp:
                return None
            fingerprint = verify_current_build()
        except (OSError, ValueError, TypeError) as error:
            self.error = str(error)
            # Retry incomplete publication even when its timestamps stay unchanged.
            self._stamp = None
            return None
        self._stamp = stamp
        self.error = None
        changed = fingerprint != self._fingerprint
        self._fingerprint = fingerprint
        return deepcopy(fingerprint) if self._initialized and changed else None


class AtlasJob:
    """A single separately loaded native runtime, polled by UI or public CLI."""

    # directory: Unique retained artifact directory allocated by start.
    # process: This job's child; no native library is loaded by the parent.
    # build: Validated dependency fingerprint against which publication is checked.
    # logs: Parent-owned stdout/stderr files, closed after the child is reaped.
    # contracts: Catalog/scenario source hashes selected before the child starts.
    # Raises no additional native operations.
    def __init__(self, directory, process, build, logs, contracts=None):
        self.contract_inputs = contracts
        self.directory, self.process, self.build, self._logs = directory, process, build, logs
        self.done, self.result, self.error = False, None, None
        self._offset, self._cancelled = 0, False
        self._started = time.monotonic()

    # identities: Optional explicit selection; duplicates/unknown IDs are rejected.
    # suite: Catalog-driven pilot/supported/all selection when identities is None.
    # auto: Whether a new verified native build triggered this request.
    # Raises invalid inputs, stale build, frozen-app or subprocess launch failures.
    @classmethod
    def start(cls, identities=None, suite="pilot", auto=False):
        if getattr(sys, "frozen", False):
            raise ValueError("Isolated atlas generation is unavailable in packaged simulator builds")
        if type(auto) is not bool:
            raise ValueError("Atlas automatic trigger must be boolean")
        entries = selected_entries(identities, suite)
        build = verify_current_build()
        contracts = contract_inputs(entries)
        directory = ATLAS_ROOT / "runs" / uuid4().hex
        directory.mkdir(parents=True, exist_ok=False)
        atomic_json(directory / "request.json", {"schema": "cody.studio.atlas-request.v1", "suite": suite,
                    "identities": [entry["id"] for entry in entries], "build": build, "auto": auto, "contract_inputs": contracts})
        logs = [(directory / name).open("xb") for name in ("worker.stdout.log", "worker.stderr.log")]
        try:
            process = subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()),
                                        "--internal-worker", str(directory)], cwd=ROOT,
                                       stdin=subprocess.DEVNULL, stdout=logs[0], stderr=logs[1],
                                       start_new_session=os.name != "nt")
        except BaseException:
            for stream in logs:
                stream.close()
            raise
        return cls(directory, process, build, logs, contracts)

    # No parameters: signal and reap only this suite process and its process group.
    # Raises OS/wait errors; static fixtures never create nested worker processes.
    def _stop_child(self):
        if self.process.poll() is None:
            try:
                if os.name == "nt":
                    self.process.kill()
                else:
                    os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.process.wait(timeout=5)

    # No parameters: reap this job immediately; incomplete artifacts remain for inspection.
    # No nested workers are admitted by the static suite contract.
    # Raises signal/wait/filesystem errors rather than silently orphaning the child.
    def cancel(self):
        if self.done:
            return
        self._cancelled = True
        self._stop_child()
        self.poll()

    # No parameters: return new complete JSON events and publish only after child exit.
    # Validation failures populate error, preserve the last valid result and finish once.
    def poll(self):
        events = []
        if self.done:
            return events
        event_path = self.directory / "events.jsonl"
        try:
            if event_path.exists():
                with event_path.open("rb") as stream:
                    stream.seek(self._offset)
                    for line in stream:
                        if not line.endswith(b"\n"):
                            break
                        event = json.loads(line, object_pairs_hook=unique_object)
                        if not isinstance(event, dict) or not isinstance(event.get("type"), str):
                            raise ValueError("Invalid atlas progress event")
                        events.append(event)
                        self._offset = stream.tell()
            if self.process.poll() is None:
                if time.monotonic() - self._started > 600:
                    self.cancel()
                    self.error = "Atlas generation exceeded its 600-second deadline; previous result preserved"
                return events
            self.process.wait()
            for stream in self._logs:
                stream.close()
            self.done = True
            if self._cancelled:
                self.error = "Atlas generation cancelled; previous result preserved"
            elif self.process.returncode:
                stderr = (self.directory / "worker.stderr.log").read_text(encoding="utf-8", errors="replace")
                self.error = f"Atlas worker failed ({self.process.returncode}): {stderr[-2500:]}"
            else:
                manifest_path = self.directory / "manifest.json"
                publish_atlas(manifest_path, self.build, self.contract_inputs)
                self.result = manifest_path
            events.append({"type": "finished", "manifest": str(self.result) if self.result else None,
                           "message": self.error or "Native atlas published", "error": self.error})
        except (OSError, ValueError, TypeError) as error:
            self._stop_child()
            for stream in self._logs:
                stream.close()
            self.done, self.error = True, str(error)
            events.append({"type": "finished", "manifest": None, "message": self.error, "error": self.error})
        return events


# directory: Unique internal job directory supplied by AtlasJob.start.
# Raises request, provenance, native startup or per-suite publication failures.
def run_worker(directory):
    directory = Path(directory).resolve(strict=True)
    if directory.parent != (ATLAS_ROOT / "runs").resolve() or not re.fullmatch(r"[0-9a-f]{32}", directory.name):
        raise ValueError("Atlas worker requires a unique artifact job directory")
    request = json.loads((directory / "request.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if not isinstance(request, dict) or set(request) != {"schema", "suite", "identities", "build", "auto", "contract_inputs"} or request["schema"] != "cody.studio.atlas-request.v1":
        raise ValueError("Unsupported isolated atlas request")
    if type(request["auto"]) is not bool:
        raise ValueError("Atlas automatic trigger must be boolean")
    entries = selected_entries(request["identities"], request["suite"])
    if contract_inputs(entries) != request["contract_inputs"]:
        raise ValueError("Studio catalog/scenario changed before atlas worker startup")
    verify_contract_inputs(request["contract_inputs"])
    if verify_current_build() != request["build"]:
        raise ValueError("Native build changed before atlas worker startup")
    from cody_native_device import NativeDevice
    from cody_studio_runner import StudioRunner

    device = NativeDevice({"reason": "SIMULATED-STUDIO-ATLAS"}, directory / "session.nvs",
                          80, True, 1, deterministic_clock=True, audio_mode="silent")
    device.start()
    runner = StudioRunner(device)
    cases = []
    inventories = [entry["inventory"] for entry in entries if "inventory" in entry]
    inventory = deepcopy(inventories[0]) if inventories else {}
    with (directory / "events.jsonl").open("x", encoding="utf-8", buffering=1) as events:
        events.write(json.dumps({"type": "started", "total": len(entries), "message": "Isolated native atlas started"}) + "\n")
        for index, entry in enumerate(entries):
            events.write(json.dumps({"type": "case-start", "id": entry["id"], "completed": index,
                                     "total": len(entries), "message": entry.get("inventory_id") or entry["id"]}) + "\n")
            case = {key: deepcopy(entry.get(key)) for key in
                    ("id", "inventory_id", "inventory_family_id", "title", "source", "symbol", "rendering_mode")}
            case.update({"variant": None, "scenario_revision": None, "capture": None})
            try:
                available, reason = runner.availability(entry)
                if not available:
                    case.update(acquisition="not-applicable" if entry["availability"] in
                                ("profile-conditional", "not-applicable") else "unsupported", reason=reason)
                else:
                    scenario = default_scenario(entry["id"])
                    if scenario["time"]["mode"] != "controlled-static":
                        raise ValueError("Atlas suites require static native fixtures; nested worker replay is unavailable")
                    case.update(variant=scenario["variant"], scenario_revision=scenario_revision(scenario))
                    runner.prepare(scenario)
                    record = runner.capture()
                    case.update(acquisition="captured", capture=record.metadata_path.relative_to(ROOT).as_posix(),
                                metadata_sha256=hashlib.sha256(record.metadata_path.read_bytes()).hexdigest(), reason=None)
            except Exception as error:
                case.update(acquisition="failed", reason=f"{type(error).__name__}: {error}")
            cases.append(case)
            events.write(json.dumps({"type": "case-result", "completed": index + 1,
                                     "total": len(entries), "case": case}) + "\n")
        status = "failed" if any(case["acquisition"] == "failed" for case in cases) else "complete"
        if verify_current_build() != request["build"]:
            status = "stale"
        verify_contract_inputs(request["contract_inputs"])
        manifest = {"schema": SCHEMA, "run_id": directory.name, "suite": request["suite"],
                    "status": status, "auto": request["auto"], "build": request["build"],
                    "contract_inputs": request["contract_inputs"], "selection": request["identities"],
                    "inventory": inventory, "cases": cases}
        atomic_json(directory / "manifest.json", manifest)
    return 0


# argv: Public generator suite/case arguments, or the private child directory selector.
# Returns nonzero on generation/cancellation errors and retains diagnostic artifacts.
def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate a native Studio atlas in an isolated process")
    parser.add_argument("--suite", choices=SUITES, default="pilot")
    parser.add_argument("--case", action="append", dest="identities")
    parser.add_argument("--internal-worker", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.internal_worker is not None:
        return run_worker(args.internal_worker)
    job = AtlasJob.start(args.identities, args.suite)
    try:
        while not job.done:
            for event in job.poll():
                print(json.dumps(event, ensure_ascii=True), flush=True)
            if not job.done:
                time.sleep(.05)
    except KeyboardInterrupt:
        job.cancel()
    if job.error:
        print(job.error, file=sys.stderr)
        return 1
    print(job.result, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
