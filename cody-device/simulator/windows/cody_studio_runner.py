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
from cody_studio_catalog import entry_for
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
            metadata.get("rendering_mode") != "runtime-full"):
        raise ValueError("Studio capture scenario and comparison identity disagree")
    receipt = metadata.get("frame_identity")
    if (not isinstance(receipt, dict) or receipt.get("target") != entry["native_target"] or
            receipt.get("scenario_revision") != revision or type(receipt.get("at_ms")) is not int or
            receipt["at_ms"] != metadata["at_ms"] or receipt.get("rendering_mode") != metadata["rendering_mode"] or
            type(receipt.get("revision")) is not int or not 1 <= receipt["revision"] <= 0xffffffff):
        raise ValueError("Studio capture lacks a matching completed-frame receipt")
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

    # scenario: Strict synthetic contract for P04, copied before any mutation.
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
        image, identity = self.device.prepare_studio_display(
            revision, value["time"]["capture_at_ms"][0], **value["common"]["settings"])
        if (identity.get("target") != entry["native_target"] or identity.get("scenario_revision") != revision or
                type(identity.get("at_ms")) is not int or identity["at_ms"] != 0 or
                type(identity.get("revision")) is not int or not 1 <= identity["revision"] <= 0xffffffff or
                identity.get("rendering_mode") != "runtime-full" or
                image.mode != "RGB" or image.size != (466, 466)):
            raise ValueError("Native preparation receipt does not identify the returned P04 frame")
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
                    "evidence": "Native owner receipt; host repeatability and device acceptance pending"}
        metadata_path = directory / "capture.json"
        if verify_current_build() != self.build:
            raise ValueError("Native build changed while saving this frame; prepare it again")
        with metadata_path.open("x", encoding="utf-8") as output:
            json.dump(metadata, output, indent=2)
            output.write("\n")
        return CaptureRecord(image_path, metadata_path, metadata)
