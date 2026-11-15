"""Generate native Studio suites through the shared isolated atlas runner.

Public entry: scripts/device.ps1 generate -Generator cody-studio-atlas.
The command never builds implicitly or selects the legacy Python preview.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))

from cody_studio_atlas import main


if __name__ == "__main__":
    raise SystemExit(main())
