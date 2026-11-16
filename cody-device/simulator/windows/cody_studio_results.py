"""Stage complete capture presentations before the Tk window adopts them."""

from dataclasses import dataclass
from hashlib import sha256
from io import BytesIO

from PIL import Image

from cody_studio_catalog import entry_for, routes_for
from cody_studio_comparison import ComparisonResult, compare_captures, comparison_image, load_review
from cody_studio_runner import CaptureRecord, load_capture


@dataclass(frozen=True)
class CapturePresentation:
    """One validated result; native pixels, references and review belong together."""

    record: CaptureRecord
    entry: dict
    routes: list
    review: dict
    image: Image.Image
    display_image: Image.Image
    comparison: ComparisonResult | None


class CaptureResultController:
    """Retain the last adopted result while a later result is being validated."""

    # No parameters: this controller owns evidence only, never Tk or native state.
    # Exceptions: None; construction does not access files or services.
    def __init__(self):
        self.current = None

    # record: Candidate capture returned by a worker or chosen from history.
    # baseline: Explicit optional comparison baseline; it is never changed here.
    # mode: Current, side-by-side, overlay or difference presentation.
    # opacity: Current-frame blend fraction for an overlay.
    # Exceptions: Original evidence, catalog, review and image failures propagate.
    # No state is published until every read needed by the view has succeeded.
    def prepare(self, record, baseline=None, mode="current", opacity=0.5):
        verified = load_capture(record.metadata_path)
        entry = entry_for(verified.metadata["id"])
        routes = routes_for(entry["id"])
        review = load_review(verified)
        pixels = verified.image_path.read_bytes()
        if sha256(pixels).hexdigest() != verified.metadata["png_sha256"]:
            raise ValueError("Studio capture changed while preparing its presentation")
        with Image.open(BytesIO(pixels)) as source:
            image = source.copy()
        comparison = compare_captures(baseline, verified) if baseline is not None else None
        display = image
        if comparison is not None and comparison.compatible and mode != "current":
            display = comparison_image(baseline, verified, mode, opacity)
        return CapturePresentation(verified, entry, routes, review, image, display, comparison)

    # presentation: Fully staged result whose Tk presentation succeeded.
    # Exceptions: None; the caller retains control of when adoption becomes visible.
    def commit(self, presentation):
        self.current = presentation
