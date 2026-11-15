"""Export inventory-linked native frames from a Cody Studio atlas run."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))

from cody_studio_pdf import export_atlas


# argv: Optional manifest, new artifact-owned PDF destination and preview rendering flag.
# Raises validation, PDF, missing Poppler, rendering or timeout errors; outputs are preserved.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", help="Published Studio atlas JSON; defaults to the last valid run")
    parser.add_argument("--output", help="New PDF destination beneath artifacts/")
    parser.add_argument("--render", action="store_true",
                        help="Render every PDF page to retained 90-DPI PNG previews using Poppler")
    args = parser.parse_args(argv)
    destination = export_atlas(args.manifest, args.output)
    print(destination, flush=True)
    if args.render:
        import shutil
        import subprocess
        from uuid import uuid4

        poppler = shutil.which("pdftoppm")
        if poppler is None:
            raise RuntimeError(f"PDF exported to {destination}; preview rendering requires Poppler pdftoppm")
        prefix = destination.parent / f"{destination.stem}-{uuid4().hex[:8]}-page"
        subprocess.run([poppler, "-r", "90", "-png", str(destination), str(prefix)],
                       check=True, cwd=ROOT, timeout=240)
        print(f"Rendered PDF page previews: {prefix}-*.png", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
