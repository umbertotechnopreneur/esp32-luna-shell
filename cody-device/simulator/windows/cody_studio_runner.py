"""One native preparation/capture path shared by Studio and automatic execution."""

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import threading
from uuid import uuid4

from PIL import Image

from cody_native_runtime import ROOT
from cody_studio_catalog import RENDERING_MODES, entry_for, routes_for
from cody_studio_scenario import default_scenario, scenario_revision, validate_scenario
from runtime_paths import simulator_library_path

ARTIFACT_ROOT = (ROOT / "artifacts/simulator").resolve()
CAPTURE_ROOT = ARTIFACT_ROOT / "studio/captures"
MAX_ROUTE_STEPS = 128


@dataclass(frozen=True)
class CaptureRecord:
    """An immutable capture's paths and decoded identity; review lives separately."""

    image_path: Path
    metadata_path: Path
    metadata: dict


# No parameters: recheck every declared dependency even when the CDLL is cached.
# Raises I/O or ValueError for incomplete, altered or stale build provenance.
def verify_current_build():
    library = simulator_library_path(ROOT, "graphics-device")
    path = library.parent / "manifest.json"
    raw = path.read_bytes()
    manifest = json.loads(raw)
    required = {"cody-device/simulator/native/CODY_host_display.cpp",
                "cody-device/simulator/native/CODY_host_input.cpp",
                "cody-device/simulator/native/CODY_host_studio_domain.cpp",
                "cody-device/simulator/native/platform/cody_host_studio.h",
                "cody-device/components/CODY_companion/src/runtime/CODY_device_ui.cpp",
                "cody-device/components/CODY_companion/src/scenes/CODY_onboarding.cpp"}
    inputs = manifest.get("inputs")
    if manifest.get("schema") != "cody.host-graphics.v1" or not isinstance(inputs, dict) or not required.issubset(inputs):
        raise ValueError("Native Studio build provenance is incomplete; regenerate the native device runtime")
    for relative, digest in inputs.items():
        source = (ROOT / relative).resolve(strict=True)
        if not source.is_relative_to(ROOT) or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Stale native Studio build input: {relative}; regenerate the native device runtime")
    digest = hashlib.sha256(library.read_bytes()).hexdigest()
    if digest != manifest.get("library_sha256"):
        raise ValueError("Native Studio library hash does not match its build provenance")
    return {"manifest_sha256": hashlib.sha256(raw).hexdigest(), "library_sha256": digest,
            "inputs": deepcopy(inputs)}


# receipt: Native metadata committed under the same lock as its completed pixels.
# scenario: Strict synthetic inputs whose canonical revision identifies this request.
# entry: Current executable source contract, including the actual rendering mode.
# rendering_mode: Optional stored historical mode; new preparation uses the current contract.
# Raises ValueError for wrong target/time/mode, source animation or effective query scope.
def validate_receipt(receipt, scenario, entry, rendering_mode=None):
    revision = scenario_revision(scenario)
    expected_mode = entry["rendering_mode"] if rendering_mode is None else rendering_mode
    worker_mode = scenario["time"]["mode"] == "controlled-worker"
    if (not isinstance(receipt, dict) or receipt.get("target") != entry["native_target"] or
            receipt.get("scenario_revision") != revision or type(receipt.get("at_ms")) is not int or
            (not worker_mode and receipt["at_ms"] != scenario["time"]["capture_at_ms"][0]) or
            (worker_mode and not 0 <= receipt["at_ms"] <= entry["worker_replay"]["max_ms"]) or
            not isinstance(expected_mode, str) or expected_mode not in RENDERING_MODES or
            receipt.get("rendering_mode") != expected_mode or
            type(receipt.get("revision")) is not int or not 1 <= receipt["revision"] <= 0xffffffff):
        raise ValueError("Studio capture lacks a matching completed-frame receipt")
    if "frame_revision" in receipt and (type(receipt["frame_revision"]) is not int or
                                        receipt["frame_revision"] != receipt["revision"]):
        raise ValueError("Studio completed-frame receipt has inconsistent frame revision aliases")
    if "sample_at_ms" in scenario["page"]:
        observed = receipt.get("source_animation_sample_ms")
        if (type(observed) is not int or
                (worker_mode and not 0 <= observed <= receipt["at_ms"]) or
                (not worker_mode and observed != scenario["page"]["sample_at_ms"])):
            raise ValueError("Native completed-frame receipt has a different source animation sample")
    if entry["native_target"] in ("cody.codex.hub", "cody.codex.chats", "cody.codex.turns", "cody.codex.calls",
                                 "cody.codex.active", "cody.codex.questions"):
        context = receipt.get("context")
        if (type(receipt.get("abi")) is not int or receipt["abi"] < 2 or not isinstance(context, dict) or
                any(context.get(key) != scenario["page"][key] for key in ("source_id", "chat_id", "turn_id"))):
            raise ValueError("Native Codex completed-frame receipt has a different effective query scope")
        calls = {call["id"]: call for call in scenario["services"]["domain"]["calls"]}
        if entry["native_target"] == "cody.codex.active":
            expected_ids = [identity for identity, call in calls.items() if call["state"] in ("queued", "running")][:3]
        elif entry["native_target"] == "cody.codex.calls":
            expected_ids = list(calls)[:3]
        else:
            expected_ids = []
        visible = context.get("call_ids")
        if (not isinstance(visible, list) or visible != expected_ids or
                any(not isinstance(identity, str) for identity in visible) or len(set(visible)) != len(visible) or
                any(identity not in calls or calls[identity]["turn_id"] != context["turn_id"] for identity in visible)):
            raise ValueError("Native Codex completed-frame receipt has unrelated or missing visible calls")
        if type(receipt.get("call_count")) is not int or receipt["call_count"] != len(visible):
            raise ValueError("Native Codex completed-frame receipt has an inconsistent visible call count")
    if entry["native_target"] == "cody.provisioning.error":
        if (receipt.get("service_outcome") != scenario["services"]["provisioning"]["outcome"] or
                type(receipt.get("service_error")) is not int or receipt["service_error"] == 0):
            raise ValueError("Native provisioning completed-frame receipt does not retain its service failure")


# entry: Source-owned fixed dependency policy for native pilot preparation.
# scenario: Validated explicit inputs, including the editable P12 quiet-hours policy.
# Raises KeyError for a corrupt executable catalog rather than inventing a policy.
def fixed_dependencies(entry, scenario):
    policy = deepcopy(entry["fixed_dependencies"])
    settings = scenario["common"]["settings"]
    if "sounds_enabled" in settings:
        policy["sounds_enabled"] = settings["sounds_enabled"]
    if entry["native_target"] in ("cody.settings.quiet-edit", "cody.settings.quiet-hours"):
        page = scenario["page"]
        policy["quiet_hours"].update(enabled=page["quiet_hours_enabled"],
                                     start_minute=page["quiet_start_minute"],
                                     end_minute=page["quiet_end_minute"])
    return policy


# base: Complete scenario selected for this source-animation sequence.
# sample: Completed scenario with the source-animation instant actually rendered.
# worker: Optional validated native display-worker evidence for the requested instant.
# Raises ValueError when anything except the source sample changed between them.
def timeline_metadata(base, sample, worker=None):
    original = validate_scenario(base)
    actual = validate_scenario(sample)
    if actual["time"]["mode"] == "controlled-worker":
        if original != actual or not isinstance(worker, dict) or not isinstance(worker.get("receipt"), dict):
            raise ValueError("Worker timeline requires its unchanged base scenario and native receipt")
        receipt = worker["receipt"]
        return {"schema": "cody.studio.timeline.v1", "base_scenario": original,
                "base_revision": scenario_revision(original), "sample_ms": receipt["frame_at_ms"],
                "requested_ms": receipt["requested_ms"], "clock": "display-worker", "worker_replay": True}
    if "sample_at_ms" not in original["page"] or "sample_at_ms" not in actual["page"]:
        raise ValueError("This Studio scenario has no native source-animation sample")
    expected = deepcopy(original)
    expected["page"]["sample_at_ms"] = actual["page"]["sample_at_ms"]
    if expected != actual:
        raise ValueError("Timeline frame differs from its base scenario beyond its source sample")
    return {"schema": "cody.studio.timeline.v1", "base_scenario": original,
            "base_revision": scenario_revision(original), "sample_ms": actual["page"]["sample_at_ms"],
            "clock": "source-animation", "worker_replay": False}


# scenario: Complete controlled-worker scenario with immutable forward boundaries.
# identity: Frame identity retired together with the native worker receipt.
# receipt: Native typed output from the display worker's atomic snapshot.
# Raises ValueError for a foreign scenario, instant, frame, clock or malformed receipt.
def worker_metadata(scenario, identity, receipt):
    value = validate_scenario(scenario)
    if value["time"]["mode"] != "controlled-worker":
        raise ValueError("Worker evidence requires a controlled-worker scenario")
    entry = entry_for(value["context"]["target"])
    fields = {"abi", "target", "scenario_revision", "requested_ms", "frame_at_ms", "tick_count", "frame_revision"}
    if (not isinstance(receipt, dict) or set(receipt) != fields or
            type(receipt.get("abi")) is not int or receipt["abi"] != 1 or
            receipt.get("target") != entry["native_target"] or
            receipt.get("scenario_revision") != scenario_revision(value) or
            type(receipt.get("requested_ms")) is not int or receipt["requested_ms"] not in value["time"]["capture_at_ms"] or
            type(receipt.get("frame_at_ms")) is not int or not 0 <= receipt["frame_at_ms"] <= receipt["requested_ms"] or
            type(receipt.get("tick_count")) is not int or not 0 <= receipt["tick_count"] <= 10000 or
            type(receipt.get("frame_revision")) is not int or receipt["frame_revision"] <= 0 or
            not isinstance(identity, dict) or identity.get("target") != receipt["target"] or
            identity.get("scenario_revision") != receipt["scenario_revision"] or
            type(identity.get("at_ms")) is not int or identity["at_ms"] != receipt["frame_at_ms"] or
            type(identity.get("revision")) is not int or identity["revision"] != receipt["frame_revision"] or
            type(identity.get("source_animation_sample_ms")) is not int or
            not 0 <= identity["source_animation_sample_ms"] <= receipt["frame_at_ms"]):
        raise ValueError("Native worker receipt does not identify the requested instant and completed frame")
    return {"schema": "cody.studio.worker.v1", "reset": "isolated-process", "receipt": deepcopy(receipt)}


# origin: Validated scenario whose completed frame preceded the native gesture.
# destination: Scenario corresponding to the frame retired by that gesture.
# receipt: Native same-frame route evidence, not inferred from a catalog edge.
# identity: Completed native destination frame identity from the same snapshot.
# Raises ValueError if scenario, route or frame identities do not agree.
def route_metadata(origin, destination, receipt, identity):
    origin = validate_scenario(origin)
    destination = validate_scenario(destination)
    if (origin["context"]["target"] == destination["context"]["target"] or
            origin["common"] != destination["common"]):
        raise ValueError("Route replay requires different destinations and unchanged common inputs")
    if (not isinstance(receipt, dict) or set(receipt) != {"abi", "route_id", "origin_scenario_revision",
            "origin_frame_revision", "destination_frame_revision"} or type(receipt["abi"]) is not int or
            receipt["abi"] != 1 or not isinstance(receipt["route_id"], str) or
            not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,30}", receipt["route_id"]) or
            receipt["origin_scenario_revision"] != scenario_revision(origin) or
            type(receipt["origin_frame_revision"]) is not int or receipt["origin_frame_revision"] <= 0 or
            type(receipt["destination_frame_revision"]) is not int or
            receipt["destination_frame_revision"] <= receipt["origin_frame_revision"] or
            not isinstance(identity, dict) or identity.get("revision") != receipt["destination_frame_revision"] or
            identity.get("scenario_revision") != scenario_revision(destination)):
        raise ValueError("Native route receipt does not identify the source and completed destination frame")
    declared = [route for route in routes_for(origin["context"]["target"])
                if route.get("origin") == origin["context"]["target"] and
                route.get("destination") == destination["context"]["target"] and
                route.get("route_id") == receipt["route_id"] and route.get("replay") is True]
    if len(declared) != 1:
        raise ValueError("Native route receipt does not match a supported catalog transition")
    return {"schema": "cody.studio.route.v1", "origin_scenario": origin,
            "destination_revision": scenario_revision(destination), "receipt": deepcopy(receipt)}


# provenance: Full prepared root and native receipts in actual navigation order.
# scenario/identity/route: The immutable completed destination evidence.
# Returns a stable semantic path; process-local frame revisions are only validated.
# Raises ValueError for gaps, altered receipts, unsupported edges or a foreign destination.
def validate_navigation_provenance(provenance, scenario, identity, route):
    if (not isinstance(provenance, dict) or set(provenance) != {"schema", "root_scenario", "root_identity", "steps"}
            or provenance["schema"] != "cody.studio.navigation.v1" or not isinstance(provenance["steps"], list)
            or len(provenance["steps"]) > MAX_ROUTE_STEPS):
        raise ValueError("Invalid Studio navigation provenance")
    root = validate_scenario(provenance["root_scenario"])
    current_identity = provenance["root_identity"]
    validate_receipt(current_identity, root, entry_for(root["context"]["target"]),
                     rendering_mode=current_identity.get("rendering_mode") if isinstance(current_identity, dict) else None)
    current = root
    semantic = [("prepare", scenario_revision(root))]
    last_route = None
    for step in provenance["steps"]:
        if not isinstance(step, dict) or set(step) != {"destination_scenario", "identity", "receipt"}:
            raise ValueError("Invalid Studio navigation step")
        destination = validate_scenario(step["destination_scenario"])
        next_identity, receipt = step["identity"], step["receipt"]
        validate_receipt(next_identity, destination, entry_for(destination["context"]["target"]),
                         rendering_mode=next_identity.get("rendering_mode") if isinstance(next_identity, dict) else None)
        last_route = route_metadata(current, destination, receipt, next_identity)
        if receipt["origin_frame_revision"] != current_identity["revision"]:
            raise ValueError("Studio navigation steps do not share a completed origin frame")
        semantic.append((receipt["route_id"], scenario_revision(destination)))
        current, current_identity = destination, next_identity
    if current != scenario or current_identity != identity or last_route != route:
        raise ValueError("Studio navigation provenance does not end at the captured frame")
    return tuple(semantic)


# metadata_path: Immutable capture JSON beneath artifacts/simulator, including baselines.
# Raises I/O, image or ValueError for an invalid identity, path, pixel or file hash.
def load_capture(metadata_path):
    path = Path(metadata_path).resolve(strict=True)
    if not path.is_relative_to(ARTIFACT_ROOT) or path.suffix.lower() != ".json":
        raise ValueError("Studio capture metadata must be JSON beneath artifacts/simulator/")
    metadata = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(metadata, dict) or metadata.get("schema") != "cody.studio.capture.v1" or metadata.get("acquisition") != "captured":
        raise ValueError("Unsupported or unsuccessful Studio capture")
    if not isinstance(metadata.get("capture_id"), str) or not re.fullmatch(r"[0-9a-f]{32}", metadata["capture_id"]):
        raise ValueError("Studio capture requires a canonical capture UUID")
    scenario = validate_scenario(metadata.get("scenario"))
    entry = entry_for(scenario["context"]["target"])
    revision = scenario_revision(scenario)
    if (metadata.get("id") != entry["id"] or metadata.get("inventory_id") != entry["inventory_id"] or
            metadata.get("scenario_revision") != revision or
            metadata.get("scenario_id") != scenario["id"] or metadata.get("variant") != scenario["variant"] or
            type(metadata.get("at_ms")) is not int or
            (scenario["time"]["mode"] == "controlled-static" and metadata["at_ms"] != 0) or
            not isinstance(metadata.get("rendering_mode"), str) or metadata["rendering_mode"] not in RENDERING_MODES):
        raise ValueError("Studio capture scenario and comparison identity disagree")
    receipt = metadata.get("frame_identity")
    validate_receipt(receipt, scenario, entry, rendering_mode=metadata["rendering_mode"])
    if metadata["at_ms"] != receipt["at_ms"]:
        raise ValueError("Studio capture time disagrees with its completed frame")
    worker = metadata.get("worker")
    if scenario["time"]["mode"] == "controlled-worker":
        if not isinstance(worker, dict) or "receipt" not in worker:
            raise ValueError("Controlled-worker capture is missing its atomic worker receipt")
        expected_worker = worker_metadata(scenario, receipt, worker["receipt"])
        if (worker != expected_worker or type(metadata.get("requested_at_ms")) is not int or
                metadata["requested_at_ms"] != worker["receipt"]["requested_ms"]):
            raise ValueError("Studio worker capture does not match its requested instant")
    elif worker is not None or "requested_at_ms" in metadata:
        raise ValueError("Static capture cannot contain worker replay evidence")
    if "timeline" in metadata:
        timeline = metadata["timeline"]
        if (not isinstance(timeline, dict) or "base_scenario" not in timeline or
                type(timeline.get("sample_ms")) is not int or
                type(timeline.get("base_revision")) is not str or
                type(timeline.get("worker_replay")) is not bool or
                timeline != timeline_metadata(timeline["base_scenario"], scenario, worker)):
            raise ValueError("Studio timeline does not match its base scenario and completed sample")
    if "route" in metadata:
        route = metadata["route"]
        if (not isinstance(route, dict) or "origin_scenario" not in route or "receipt" not in route or
                route != route_metadata(route["origin_scenario"], scenario, route["receipt"], receipt)):
            raise ValueError("Studio route evidence does not match the completed frame")
    if "navigation_provenance" in metadata:
        validate_navigation_provenance(metadata["navigation_provenance"], scenario, receipt, metadata.get("route"))
    build = metadata.get("build")
    hashes = [metadata.get("source_sha256")]
    if not isinstance(build, dict) or not isinstance(build.get("inputs"), dict) or not build["inputs"]:
        raise ValueError("Studio capture is missing its native build provenance")
    hashes += [build.get("manifest_sha256"), build.get("library_sha256"), *build["inputs"].values()]
    # Historical receipts retain the source path from their own build. A later
    # catalog move must not invalidate an otherwise comparable saved capture.
    # This path is an identity key only; it is never opened from the receipt.
    source = metadata.get("source")
    if (not isinstance(source, str) or not source or "\\" in source or ":" in source or
            any(part in ("", ".", "..") for part in source.split("/")) or
            build["inputs"].get(source) != metadata.get("source_sha256") or
            any(not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value) for value in hashes)):
        raise ValueError("Studio capture source or historical build hashes are invalid")
    if not isinstance(metadata.get("png"), str) or not metadata["png"]:
        raise ValueError("Studio capture requires a PNG filename")
    image_path = (path.parent / metadata["png"]).resolve(strict=True)
    if not image_path.is_relative_to(path.parent) or image_path.suffix.lower() != ".png":
        raise ValueError("Studio capture PNG must remain beside its metadata")
    if hashlib.sha256(image_path.read_bytes()).hexdigest() != metadata.get("png_sha256"):
        raise ValueError("Studio capture PNG hash mismatch")
    with Image.open(image_path) as image:
        if image.format != "PNG" or image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Studio capture must contain a complete native 466 x 466 RGB PNG")
        if [image.width, image.height] != metadata.get("geometry"):
            raise ValueError("Studio capture geometry disagrees with its metadata")
        if hashlib.sha256(image.tobytes()).hexdigest() != metadata.get("pixel_sha256"):
            raise ValueError("Studio capture pixel hash mismatch")
    return CaptureRecord(image_path, path, metadata)


class StudioRunner:
    """Own one prepared static native case without reconstructing a device page.

    A deterministic runtime must be selected at bootstrap. Preparation is a
    display-owner command and capture uses its returned pixels and receipt,
    never a later framebuffer plus separately queried identity.
    """

    # device: Started NativeDevice using isolated host settings and a controlled clock.
    # Raises no native operations; setup stays under the existing public command.
    def __init__(self, device):
        self.device = device
        self.scenario = None
        self.image = None
        self.identity = None
        self.build = None
        self.route = None
        self.worker = None
        self.navigation_provenance = None
        self._worker_lock = threading.Lock()
        self._worker_process = None
        self._closed = False

    # process: Child process to register, or None when it has been reaped.
    # Exceptions: A child spawned during close is killed before it can become orphaned.
    def register_worker_process(self, process):
        with self._worker_lock:
            self._worker_process = process
            if process is not None and self._closed and process.poll() is None:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass

    # No parameters: close admission and terminate only this runner's isolated child.
    # Exceptions: A process that already exited needs no signal or additional cleanup.
    def cancel_worker(self):
        with self._worker_lock:
            self._closed = True
            process = self._worker_process
            if process is not None and process.poll() is None:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass

    # entry: Machine-readable screen contract for this native session.
    # Returns availability and a reason derived from the loaded native profile.
    # Raises native/provenance errors instead of guessing an unavailable capability.
    def availability(self, entry):
        mode = entry.get("availability")
        if mode in ("runtime-full", "presenter-isolated"):
            return True, mode
        if mode != "profile-conditional":
            return False, entry.get("reason", "No supported native preparation contract")
        required = entry.get("required_capabilities")
        if not isinstance(required, list) or not required or any(not isinstance(name, str) or not name for name in required):
            raise ValueError("Profile-conditional entry requires explicit native capabilities")
        for capability in required:
            if not self.device.studio_has_capability(capability):
                return False, f"Native profile does not provide {capability}"
        return True, "profile-conditional · native capability available"

    # scenario: Strict synthetic contract for one catalog case, copied before mutation.
    # requested_at_ms: Explicit worker capture boundary, required for multiple captures.
    # Raises original validation, stale-build or owner-preparation errors.
    def prepare(self, scenario, *, requested_at_ms=None):
        value = validate_scenario(scenario)
        entry = entry_for(value["context"]["target"])
        available, reason = self.availability(entry)
        if not available:
            raise ValueError(reason)
        revision = scenario_revision(value)
        if value["time"]["mode"] == "controlled-worker":
            captures = value["time"]["capture_at_ms"]
            if requested_at_ms is None:
                if len(captures) != 1:
                    raise ValueError("A worker scenario with multiple captures requires an explicit requested instant")
                requested_at_ms = captures[0]
            if type(requested_at_ms) is not int or requested_at_ms not in captures:
                raise ValueError("Requested worker instant is not a declared capture boundary")
            self.scenario = self.image = self.identity = self.build = self.route = self.worker = None
            self.navigation_provenance = None
            build = verify_current_build()
            if getattr(self.device.api, "_cody_library_sha256", None) != build["library_sha256"]:
                raise ValueError("A different native build is loaded in Studio; restart this session")
            from cody_studio_worker import run_isolated_worker
            with self._worker_lock:
                if self._closed:
                    raise RuntimeError("This Studio runner has already been closed")
            record = run_isolated_worker(value, requested_at_ms, self.register_worker_process)
            if (record.metadata["scenario"] != value or record.metadata["requested_at_ms"] != requested_at_ms or
                    record.metadata["build"] != build or verify_current_build() != build):
                raise ValueError("Isolated worker returned a different scenario, instant or native build")
            with Image.open(record.image_path) as image:
                self.image = image.copy()
            self.scenario, self.identity, self.build = value, deepcopy(record.metadata["frame_identity"]), build
            self.worker = deepcopy(record.metadata["worker"])
            self.navigation_provenance = {"schema": "cody.studio.navigation.v1", "root_scenario": deepcopy(value),
                                          "root_identity": deepcopy(self.identity), "steps": []}
            return revision
        if requested_at_ms is not None:
            raise ValueError("An explicit worker instant requires a controlled-worker scenario")
        self.scenario = self.image = self.identity = self.build = self.route = self.worker = None
        self.navigation_provenance = None
        build = verify_current_build()
        if getattr(self.device.api, "_cody_library_sha256", None) != build["library_sha256"]:
            raise ValueError("A different native build is loaded in Studio; restart this session")
        self.device.apply_snapshot(deepcopy(value["common"]["observations"]))
        image, identity = self.device.prepare_studio_case(
            entry["native_target"], revision, value["time"]["capture_at_ms"][0],
            settings=deepcopy(value["common"]["settings"]), page=deepcopy(value["page"]),
            services=deepcopy(value["services"]))
        validate_receipt(identity, value, entry)
        if image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Native preparation did not return a complete 466 x 466 RGB frame")
        # Reject a build/source edit during preparation; the completed native frame
        # must have the exact dependency set validated before invoking its owner.
        if verify_current_build() != build:
            raise ValueError("Native build changed while preparing the Studio frame")
        self.scenario, self.image, self.identity, self.build = value, image.copy(), deepcopy(identity), build
        self.navigation_provenance = {"schema": "cody.studio.navigation.v1", "root_scenario": deepcopy(value),
                                      "root_identity": deepcopy(identity), "steps": []}
        return revision

    # scenario: Validated controlled-worker scenario owned by a fresh child process.
    # requested_at_ms: Declared forward boundary to reproduce from zero in that process.
    # Raises validation, provenance, owner, receipt or frame errors without fallback.
    def prepare_worker_local(self, scenario, requested_at_ms):
        value = validate_scenario(scenario)
        if (value["time"]["mode"] != "controlled-worker" or type(requested_at_ms) is not int or
                requested_at_ms not in value["time"]["capture_at_ms"] or self.scenario is not None):
            raise ValueError("Isolated worker requires a fresh session and a declared capture boundary")
        entry = entry_for(value["context"]["target"])
        available, reason = self.availability(entry)
        if not available:
            raise ValueError(reason)
        build = verify_current_build()
        if getattr(self.device.api, "_cody_library_sha256", None) != build["library_sha256"]:
            raise ValueError("A different native build is loaded in the worker process")
        revision = scenario_revision(value)
        self.device.apply_snapshot(deepcopy(value["common"]["observations"]))
        self.device.prepare_studio_case(entry["native_target"], revision, 0,
                                        settings=deepcopy(value["common"]["settings"]),
                                        page=deepcopy(value["page"]), services=deepcopy(value["services"]))
        elapsed = 0
        boundaries = []
        for advance in value["time"]["advances_ms"]:
            elapsed += advance
            if elapsed > requested_at_ms:
                break
            boundaries.append(elapsed)
        if requested_at_ms == 0:
            boundaries.append(0)
        if not boundaries or boundaries[-1] != requested_at_ms:
            raise ValueError("Requested worker instant is not a forward advance boundary")
        for boundary in boundaries:
            image, identity, observed = self.device.replay_studio_worker(revision, boundary)
        validate_receipt(identity, value, entry)
        worker = worker_metadata(value, identity, observed)
        if observed["requested_ms"] != requested_at_ms or image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Native worker returned a different instant or incomplete frame")
        if verify_current_build() != build:
            raise ValueError("Native build changed during isolated worker replay")
        self.scenario, self.image, self.identity, self.build, self.worker = (
            value, image.copy(), deepcopy(identity), build, worker)
        self.route = None
        self.navigation_provenance = {"schema": "cody.studio.navigation.v1", "root_scenario": deepcopy(value),
                                      "root_identity": deepcopy(identity), "steps": []}
        return revision

    # route_id: Catalog route whose source has already been prepared on this owner.
    # Returns its captured observed destination, never a direct destination prepare.
    # Raises validation, native route, provenance or capture errors without fallback.
    def replay_route(self, route_id):
        if self.scenario is None or self.identity is None or self.build is None:
            raise RuntimeError("Prepare the route origin before replaying it")
        if not isinstance(self.navigation_provenance, dict):
            raise ValueError("Route replay requires a known prepared navigation origin")
        if len(self.navigation_provenance["steps"]) >= MAX_ROUTE_STEPS:
            raise ValueError("Studio navigation provenance reached its 128-step limit; prepare a new origin")
        validate_navigation_provenance(self.navigation_provenance, self.scenario, self.identity, self.route)
        provenance = deepcopy(self.navigation_provenance)
        origin = deepcopy(self.scenario)
        if origin["time"]["mode"] != "controlled-static":
            raise ValueError("Route replay requires an origin prepared in the interactive native owner")
        candidates = [route for route in routes_for(origin["context"]["target"])
                      if route.get("origin") == origin["context"]["target"] and
                      route.get("route_id") == route_id and route.get("replay") is True]
        if len(candidates) != 1:
            raise ValueError("Route is unsupported or does not originate at the prepared scenario")
        destination = default_scenario(candidates[0]["destination"])
        destination["common"] = deepcopy(origin["common"])
        destination = validate_scenario(destination)
        destination_entry = entry_for(destination["context"]["target"])
        available, reason = self.availability(destination_entry)
        if not available:
            raise ValueError(reason)
        origin_revision, destination_revision = scenario_revision(origin), scenario_revision(destination)
        old_identity, build = deepcopy(self.identity), self.build
        if verify_current_build() != build or getattr(self.device.api, "_cody_library_sha256", None) != build["library_sha256"]:
            raise ValueError("Native build changed since preparing the route origin")
        # A failed owner command can leave the native scene changed; never allow
        # the previously prepared Python frame to masquerade as its new state.
        self.scenario = self.image = self.identity = self.build = self.route = self.worker = None
        self.navigation_provenance = None
        image, identity, observed = self.device.replay_studio_route(route_id, origin_revision, destination_revision)
        validate_receipt(identity, destination, destination_entry)
        evidence = route_metadata(origin, destination, observed, identity)
        if (observed["route_id"] != route_id or
                observed["origin_frame_revision"] != old_identity["revision"]):
            raise ValueError("Native route replay did not start at the selected completed origin frame")
        if image.mode != "RGB" or image.size != (466, 466):
            raise ValueError("Native route did not return a complete 466 x 466 RGB frame")
        if verify_current_build() != build:
            raise ValueError("Native build changed during route replay")
        provenance["steps"].append({"destination_scenario": deepcopy(destination),
                                    "identity": deepcopy(identity), "receipt": deepcopy(observed)})
        validate_navigation_provenance(provenance, destination, identity, evidence)
        self.scenario, self.image, self.identity, self.build, self.route = (
            destination, image.copy(), deepcopy(identity), build, evidence)
        self.navigation_provenance = provenance
        return self.capture()

    # timeline_base: Optional base scenario identifying a native source-animation sequence.
    # Raises RuntimeError before preparation or the original provenance/filesystem error.
    def capture(self, timeline_base=None):
        if self.image is None or self.identity is None or self.scenario is None:
            raise RuntimeError("Prepare a native Studio scenario before capturing")
        timeline = None if timeline_base is None else timeline_metadata(timeline_base, self.scenario, self.worker)
        if self.navigation_provenance is None:
            raise ValueError("Studio capture has no verified navigation provenance")
        validate_navigation_provenance(self.navigation_provenance, self.scenario, self.identity, self.route)
        if verify_current_build() != self.build:
            raise ValueError("Native build changed since this frame was prepared; prepare it again")
        entry = entry_for(self.scenario["context"]["target"])
        identity = uuid4().hex
        directory = (CAPTURE_ROOT / identity).resolve()
        if not directory.is_relative_to(ARTIFACT_ROOT):
            raise ValueError("Studio capture escaped simulator artifacts")
        directory.mkdir(parents=True, exist_ok=False)
        image_path = directory / "frame.png"
        with image_path.open("xb") as output:
            self.image.save(output, format="PNG")
        metadata = {"schema": "cody.studio.capture.v1", "capture_id": identity,
                    "id": entry["id"], "inventory_id": entry["inventory_id"],
                    "inventory": entry["inventory"], "variant": self.scenario["variant"],
                    "acquisition": "captured", "review": "unreviewed", "baseline": False,
                    "rendering_mode": self.identity["rendering_mode"], "at_ms": self.identity["at_ms"],
                    "scenario_id": self.scenario["id"], "scenario_revision": scenario_revision(self.scenario),
                    "scenario": deepcopy(self.scenario), "frame_identity": deepcopy(self.identity),
                    "geometry": list(self.image.size), "png": image_path.name,
                    "png_sha256": hashlib.sha256(image_path.read_bytes()).hexdigest(),
                    "pixel_sha256": hashlib.sha256(self.image.tobytes()).hexdigest(),
                    "source": entry["source"], "symbol": entry["symbol"],
                    "source_sha256": self.build["inputs"][entry["source"]],
                    "build": deepcopy(self.build), "time_control": entry["time_control"],
                    "limitations": deepcopy(entry.get("limitations", [])),
                    "fixed_dependencies": fixed_dependencies(entry, self.scenario),
                    "hard_to_reach": entry.get("hard_to_reach", False),
                    "transient_error": entry.get("transient_error", False),
                    "evidence": "Native owner receipt; acquisition alone does not establish repeatability or physical acceptance"}
        if timeline is not None:
            metadata["timeline"] = timeline
        if self.worker is not None:
            worker = worker_metadata(self.scenario, self.identity, self.worker["receipt"])
            if worker != self.worker:
                raise ValueError("Worker replay evidence changed after acquisition")
            metadata["worker"] = worker
            metadata["requested_at_ms"] = worker["receipt"]["requested_ms"]
        if self.route is not None:
            metadata["route"] = deepcopy(self.route)
        metadata["navigation_provenance"] = deepcopy(self.navigation_provenance)
        metadata_path = directory / "capture.json"
        if verify_current_build() != self.build:
            raise ValueError("Native build changed while saving this frame; prepare it again")
        with metadata_path.open("x", encoding="utf-8") as output:
            json.dump(metadata, output, indent=2)
            output.write("\n")
        return CaptureRecord(image_path, metadata_path, metadata)
