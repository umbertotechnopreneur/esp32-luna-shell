"""Compare native artifacts; keep acquisition, difference and review independent."""

from copy import deepcopy
from dataclasses import dataclass
import json
import os
import time
from uuid import uuid4

from PIL import Image, ImageChops

from cody_studio_runner import ARTIFACT_ROOT, CaptureRecord, load_capture, validate_navigation_provenance

COMPATIBILITY_FIELDS = ("id", "scenario_revision", "variant", "geometry", "at_ms", "rendering_mode")


@dataclass(frozen=True)
class ComparisonResult:
    """An observed visual difference is independent of human approval."""

    compatible: bool
    reasons: tuple
    changed_pixels: int | None

    @property
    # No parameters: derive only the visual comparison state, never correctness.
    def status(self):
        return "not-comparable" if not self.compatible else ("changed" if self.changed_pixels else "unchanged")


# metadata: Immutable captured input metadata, possibly predating explicit fixed inputs.
# Raises ValueError for malformed fixed-input objects; descriptive prose is not an input.
def fixed_input_values(metadata):
    policy = metadata.get("fixed_dependencies")
    if policy is None:
        return None
    if not isinstance(policy, dict) or not isinstance(policy.get("quiet_hours"), dict):
        raise ValueError("Capture fixed native inputs must contain a quiet-hours object")
    values = deepcopy(policy)
    values.pop("application", None)
    values["quiet_hours"].pop("source", None)
    return values


# baseline: Explicitly selected saved native capture.
# current: Current saved native capture; no baseline is selected or replaced here.
# Returns both reloaded records and their comparison so every view uses the
# same validated image paths. Raises original file or evidence errors.
def _validated_comparison(baseline, current):
    baseline = load_capture(baseline.metadata_path)
    current = load_capture(current.metadata_path)
    reasons = tuple(f"Incompatible {field.replace('_', ' ')}"
                    for field in COMPATIBILITY_FIELDS
                    if baseline.metadata.get(field) is None or baseline.metadata.get(field) != current.metadata.get(field))
    baseline_inputs = fixed_input_values(baseline.metadata)
    current_inputs = fixed_input_values(current.metadata)
    if baseline_inputs is None or current_inputs is None or baseline_inputs != current_inputs:
        reasons += ("Incompatible fixed native inputs",)
    if (baseline.metadata.get("worker") is not None or current.metadata.get("worker") is not None) and (
            baseline.metadata.get("requested_at_ms") != current.metadata.get("requested_at_ms")):
        reasons += ("Incompatible requested worker instant",)
    paths = []
    for metadata in (baseline.metadata, current.metadata):
        provenance = metadata.get("navigation_provenance")
        if provenance is None:
            # Historical direct captures have no route history to reconstruct.
            paths.append(None if "route" in metadata else (("prepare", metadata["scenario_revision"]),))
        else:
            paths.append(validate_navigation_provenance(provenance, metadata["scenario"],
                                                        metadata["frame_identity"], metadata.get("route")))
    if any(path is None for path in paths):
        reasons += ("Unverified historical navigation provenance",)
    elif paths[0] != paths[1]:
        reasons += ("Incompatible navigation provenance",)
    if reasons:
        return baseline, current, ComparisonResult(False, reasons, None)
    with Image.open(baseline.image_path) as before, Image.open(current.image_path) as after:
        difference = ImageChops.difference(before, after)
        # Count pixels whose RGB tuple has any changed channel, not channel totals.
        changed = sum(pixel != (0, 0, 0) for pixel in difference.getdata())
    return baseline, current, ComparisonResult(True, (), changed)


# baseline: Explicitly selected saved native capture.
# current: Current saved native capture; no baseline is selected or replaced here.
# Raises original file/image errors or ValueError for corrupted captures.
def compare_captures(baseline, current):
    return _validated_comparison(baseline, current)[2]


# baseline: Explicit saved capture supplying the left/bottom frame.
# current: Compatible saved capture supplying the right/top frame.
# mode: 'side-by-side', 'overlay' or 'difference'; device pixels remain unchanged.
# opacity: Current-frame blend fraction from 0 to 1, used only for overlay.
# Raises ValueError for incompatible captures, modes or opacity and preserves I/O errors.
def comparison_image(baseline, current, mode="side-by-side", opacity=0.5):
    baseline, current, result = _validated_comparison(baseline, current)
    if not result.compatible:
        raise ValueError("; ".join(result.reasons))
    if mode not in ("side-by-side", "overlay", "difference"):
        raise ValueError(f"Unknown Studio comparison mode: {mode}")
    if type(opacity) not in (int, float) or not 0 <= opacity <= 1:
        raise ValueError("Comparison opacity must be between 0 and 1")
    with Image.open(baseline.image_path) as left, Image.open(current.image_path) as right:
        if mode == "overlay":
            return Image.blend(left, right, opacity)
        if mode == "difference":
            return ImageChops.difference(left, right)
        image = Image.new("RGB", (left.width + right.width, left.height))
        image.paste(left, (0, 0))
        image.paste(right, (left.width, 0))
        return image


# record: Explicit user-selected immutable capture; originals are never modified.
# Raises original I/O/hash errors; a unique directory prevents baseline replacement.
def preserve_baseline(record):
    record = load_capture(record.metadata_path)
    directory = (ARTIFACT_ROOT / "studio/baselines" / uuid4().hex).resolve()
    if not directory.is_relative_to(ARTIFACT_ROOT):
        raise ValueError("Studio baseline escaped simulator artifacts")
    directory.mkdir(parents=True, exist_ok=False)
    image_path, metadata_path = directory / "frame.png", directory / "capture.json"
    with image_path.open("xb") as output:
        output.write(record.image_path.read_bytes())
    metadata = deepcopy(record.metadata)
    metadata.update({"baseline": True, "png": image_path.name,
                     "baseline_of": record.metadata["capture_id"]})
    with metadata_path.open("x", encoding="utf-8") as output:
        json.dump(metadata, output, indent=2)
        output.write("\n")
    return CaptureRecord(image_path, metadata_path, metadata)


# record: Specific capture being reviewed; its native evidence remains immutable.
# note: Human text saved as a new review event, including an empty approval note.
# status: Explicit human state: unreviewed, annotated or approved.
# Raises ValueError for invalid review input and preserves capture/filesystem errors.
def save_review(record, note, status="annotated"):
    record = load_capture(record.metadata_path)
    if not isinstance(note, str) or len(note) > 16000 or status not in ("unreviewed", "annotated", "approved"):
        raise ValueError("Review requires text up to 16000 characters and an explicit human status")
    directory = (ARTIFACT_ROOT / "studio/reviews" / record.metadata["capture_id"]).resolve()
    if not directory.is_relative_to(ARTIFACT_ROOT):
        raise ValueError("Studio review escaped simulator artifacts")
    directory.mkdir(parents=True, exist_ok=True)
    previous = review_events(record)
    sequence = max((event["sequence"] for event in previous), default=0) + 1
    path = directory / f"review-{uuid4().hex}.json"
    # A failed or interrupted write must never become a discoverable review.
    # Keep the unpublished temporary artifact for inspection on I/O failure.
    temporary = path.with_suffix(".tmp")
    with temporary.open("x", encoding="utf-8") as output:
        json.dump({"schema": "cody.studio.review.v1", "capture_id": record.metadata["capture_id"],
                   "pixel_sha256": record.metadata["pixel_sha256"], "status": status, "note": note,
                   "created_at_ns": time.time_ns(), "sequence": sequence}, output, indent=2)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)
    return path


# record: Validated immutable capture whose review events are being read.
# Raises original I/O/JSON errors or ValueError for unrelated/corrupted review events.
def review_events(record):
    directory = ARTIFACT_ROOT / "studio/reviews" / record.metadata["capture_id"]
    resolved = directory.resolve()
    if not resolved.is_relative_to(ARTIFACT_ROOT):
        raise ValueError("Studio reviews escaped simulator artifacts")
    events = []
    for path in directory.glob("review-*.json"):
        if not path.resolve(strict=True).is_relative_to(resolved):
            raise ValueError("Studio review event escaped its capture directory")
        event = json.loads(path.read_text(encoding="utf-8"))
        if (not isinstance(event, dict) or event.get("schema") != "cody.studio.review.v1" or
                event.get("capture_id") != record.metadata["capture_id"] or
                event.get("pixel_sha256") != record.metadata["pixel_sha256"] or
                event.get("status") not in ("unreviewed", "annotated", "approved") or
                not isinstance(event.get("note"), str) or len(event["note"]) > 16000 or
                type(event.get("created_at_ns")) is not int or event["created_at_ns"] <= 0 or
                type(event.get("sequence")) is not int or event["sequence"] <= 0):
            raise ValueError(f"Invalid or unrelated Studio review event: {path.name}")
        events.append(dict(event, event_id=path.stem))
    return events


# record: Specific immutable capture, checked before reading its human review state.
# Raises capture, review-integrity or filesystem errors; no implicit approval is added.
def load_review(record):
    record = load_capture(record.metadata_path)
    events = review_events(record)
    if not events:
        return {"status": "unreviewed", "note": "", "count": 0}
    latest = max(events, key=lambda event: (event["sequence"], event["created_at_ns"], event["event_id"]))
    return dict(latest, count=len(events))
