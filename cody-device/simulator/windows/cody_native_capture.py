"""Capture completed native Cody frames without reconstructing any screen."""

from pathlib import Path
from uuid import uuid4

from cody_native_runtime import ROOT, load_runtime_library


# device: Initialized native owner supplying an atomic completed-frame snapshot.
# Returns the new artifact path and the native frame revision. No route identity
# is inferred from a frame, an earlier touch, or an elapsed-time assumption.
# Raises the native display or filesystem error; an existing file is never replaced.
def capture_completed_frame(device):
    load_runtime_library("graphics-device")
    image, revision = device.frame()
    if image.mode != "RGB" or image.size != (466, 466):
        raise ValueError("Native capture must be one complete 466 x 466 RGB frame")
    base = (ROOT / "artifacts" / "simulator" / "captures").resolve()
    allowed = (ROOT / "artifacts" / "simulator").resolve()
    if not base.is_relative_to(allowed):
        raise ValueError("Native capture directory escaped simulator artifacts")
    base.mkdir(parents=True, exist_ok=True)
    path = base / f"native-frame-{revision}-{uuid4().hex}.png"
    with path.open("xb") as output:
        image.save(output, format="PNG")
    return path, revision
