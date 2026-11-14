"""One native preparation/capture path shared by Studio and automatic execution."""

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from uuid import uuid4

from PIL import Image

from cody_native_runtime import ROOT
from cody_studio_catalog import RENDERING_MODES, entry_for
from cody_studio_scenario import scenario_revision, validate_scenario
from runtime_paths import simulator_library_path

ARTIFACT_ROOT = (ROOT / "artifacts/simulator").resolve()
CAPTURE_ROOT = ARTIFACT_ROOT / "studio/captures"


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
    if (not isinstance(receipt, dict) or receipt.get("target") != entry["native_target"] or
            receipt.get("scenario_revision") != revision or type(receipt.get("at_ms")) is not int or
            receipt["at_ms"] != scenario["time"]["capture_at_ms"][0] or
            not isinstance(expected_mode, str) or expected_mode not in RENDERING_MODES or
            receipt.get("rendering_mode") != expected_mode or
            type(receipt.get("revision")) is not int or not 1 <= receipt["revision"] <= 0xffffffff):
        raise ValueError("Studio capture lacks a matching completed-frame receipt")
    if "sample_at_ms" in scenario["page"]:
        observed = receipt.get("source_animation_sample_ms")
        if type(observed) is not int or observed != scenario["page"]["sample_at_ms"]:
            raise ValueError("Native completed-frame receipt has a different source animation sample")
    if entry["native_target"] == "cody.codex.calls":
        context = receipt.get("context")
        if (type(receipt.get("abi")) is not int or receipt["abi"] < 2 or not isinstance(context, dict) or
                any(context.get(key) != scenario["page"][key] for key in ("source_id", "chat_id", "turn_id"))):
            raise ValueError("Native C04 completed-frame receipt has a different effective query scope")
        calls = {call["id"]: call for call in scenario["services"]["domain"]["calls"]}
        visible = context.get("call_ids")
        if (not isinstance(visible, list) or len(visible) != min(3, len(calls)) or
                any(not isinstance(identity, str) for identity in visible) or len(set(visible)) != len(visible) or
                any(identity not in calls or calls[identity]["turn_id"] != context["turn_id"] for identity in visible)):
            raise ValueError("Native C04 completed-frame receipt has unrelated or missing visible calls")
        if type(receipt.get("call_count")) is not int or receipt["call_count"] != len(visible):
            raise ValueError("Native C04 completed-frame receipt has an inconsistent visible call count")
    if entry["native_target"] == "cody.provisioning.error":
        if (receipt.get("service_outcome") != scenario["services"]["provisioning"]["outcome"] or
                type(receipt.get("service_error")) is not int or receipt["service_error"] == 0):
            raise ValueError("Native provisioning completed-frame receipt does not retain its service failure")


# entry: Source-owned fixed dependency policy for native pilot preparation.
# scenario: Validated explicit inputs, including the editable P12 quiet-hours policy.
# Raises KeyError for a corrupt executable catalog rather than inventing a policy.
def fixed_dependencies(entry, scenario):
    policy = deepcopy(entry["fixed_dependencies"])
    if entry["native_target"] == "cody.settings.quiet-edit":
        page = scenario["page"]
        policy["quiet_hours"].update(enabled=page["quiet_hours_enabled"],
                                     start_minute=page["quiet_start_minute"],
                                     end_minute=page["quiet_end_minute"])
    return policy


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
            type(metadata.get("at_ms")) is not int or metadata["at_ms"] != 0 or
            not isinstance(metadata.get("rendering_mode"), str) or metadata["rendering_mode"] not in RENDERING_MODES):
        raise ValueError("Studio capture scenario and comparison identity disagree")
    receipt = metadata.get("frame_identity")
    validate_receipt(receipt, scenario, entry, rendering_mode=metadata["rendering_mode"])
    build = metadata.get("build")
    hashes = [metadata.get("source_sha256")]
    if not isinstance(build, dict) or not isinstance(build.get("inputs"), dict) or not build["inputs"]:
        raise ValueError("Studio capture is missing its native build provenance")
    hashes += [build.get("manifest_sha256"), build.get("library_sha256"), *build["inputs"].values()]
    if (metadata.get("source") != entry["source"] or
            build["inputs"].get(entry["source"]) != metadata.get("source_sha256") or
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

    # scenario: Strict synthetic contract for one catalog case, copied before mutation.
    # Raises original validation, stale-build or owner-preparation errors.
    def prepare(self, scenario):
        value = validate_scenario(scenario)
        entry = entry_for(value["context"]["target"])
        revision = scenario_revision(value)
        self.scenario = self.image = self.identity = self.build = None
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
        return revision

    # No parameters: persist the exact prepared frame and receipt as new artifacts.
    # Raises RuntimeError before preparation or the original provenance/filesystem error.
    def capture(self):
        if self.image is None or self.identity is None or self.scenario is None:
            raise RuntimeError("Prepare a native Studio scenario before capturing")
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
        metadata_path = directory / "capture.json"
        if verify_current_build() != self.build:
            raise ValueError("Native build changed while saving this frame; prepare it again")
        with metadata_path.open("x", encoding="utf-8") as output:
            json.dump(metadata, output, indent=2)
            output.write("\n")
        return CaptureRecord(image_path, metadata_path, metadata)
