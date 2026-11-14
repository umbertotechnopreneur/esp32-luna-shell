"""Collect user-labelled frames retired by the native Cody display owner."""

import hashlib
import json
from pathlib import Path
import re
from tkinter import messagebox, simpledialog
from uuid import uuid4

from cody_native_capture import capture_completed_frame
from cody_native_runtime import ROOT

SOURCE = "cody-device/components/CODY_companion/src/runtime/CODY_device_ui.cpp"
CATALOG = ROOT / "docs/cody/screen-catalog.md"
MANIFEST = ROOT / "artifacts/build-cody-simulator-graphics-device/manifest.json"


# No parameters: enumerate current, named catalog identities, including conditional pages.
# Raises the catalog I/O error or a duplicate-identity error.
def catalog_identities():
    identities = set()
    for line in CATALOG.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip().replace("`", "") for cell in line.strip("|").split("|")]
        if len(cells) < 4 or not re.match(r"^(?:atlas|next|ui|widget)-v1:", cells[0]):
            continue
        if cells[-1].lower().startswith(("retained", "retired", "planned", "reference only")):
            continue
        prefix, names = cells[0].split(" / ", 1)[0].split(":", 1)
        parts = [part.strip() for part in names.split(",")]
        if len(parts) == 1 and re.fullmatch(r"\d+[A-Z]-\d+[A-Z]", parts[0]):
            parts = parts[0].split("-")
        for part in parts:
            identity = f"{prefix}:{part}"
            if identity in identities:
                raise ValueError(f"Duplicate catalog identity: {identity}")
            identities.add(identity)
    return identities


class AtlasCaptureSession:
    """Keep a set of explicitly user-labelled captures without replacing artifacts."""

    # No parameters: create an initially empty host-only collection.
    def __init__(self):
        self.records = {}

    # parent: Tk window owning the identity prompt and failure notices.
    # device: Running native simulator device; no physical hardware is contacted.
    # Captures before prompting; identity is supplied by the operator, not inferred.
    # Raises no exception to Tk: acquisition or persistence failures are shown.
    def capture(self, parent, device):
        try:
            build = json.loads(MANIFEST.read_text(encoding="utf-8"))
            source_sha = hashlib.sha256((ROOT / SOURCE).read_bytes()).hexdigest()
            if build.get("inputs", {}).get(SOURCE) != source_sha:
                raise ValueError("Native device source changed since this runtime was built")
            path, revision = capture_completed_frame(device)
            identities = catalog_identities()
            identity = simpledialog.askstring(
                "Native atlas capture - SIMULATED",
                "Frame captured. Enter its screen catalog ID (for example atlas-v1:05).\n"
                "The label is supplied by you and is not verified by the simulator.",
                parent=parent)
            if identity is None:
                return
            identity = identity.strip()
            if identity not in identities:
                raise ValueError(f"Unknown or inactive catalog identity: {identity}")
            records = dict(self.records)
            records[identity] = {
                "id": identity, "status": "native", "png": path.name,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "source": SOURCE, "source_sha256": source_sha,
                "frame_revision": revision, "identity_evidence": "Operator-labelled; not independently verified",
            }
            out = path.parent / f"atlas-manifest-{uuid4().hex}.json"
            with out.open("x", encoding="utf-8") as stream:
                json.dump({"schema": "cody.native-atlas.v1", "screens": list(records.values())}, stream, indent=2)
                stream.write("\n")
            self.records = records
            messagebox.showinfo("Native atlas capture - SIMULATED",
                f"Captured {identity}.\n{len(records)} screen(s) in this collection.\nManifest: {out}", parent=parent)
        except (OSError, ValueError, RuntimeError, TypeError) as error:
            messagebox.showerror("Native atlas capture - SIMULATED", str(error), parent=parent)
