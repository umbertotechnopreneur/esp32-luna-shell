"""Check the requested atlas consolidation and render its pages for visual review."""

from io import BytesIO
from pathlib import Path
import shutil
import subprocess

from PIL import Image, ImageDraw
from pypdf import PdfReader


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "2f7a5f5374fc96a8fc1b54fc4579787e8b8b376b"
REVISION = "CODY-AUTO-CONTEXT-20261010"
OUTPUT = ROOT / "artifacts/pdf-review" / REVISION.lower()
CHANGED = {
    "navigation": {1, 2, 4, 5, 6, 12, 35},
    "planning": {1, 2, 7, 8, 9, 10, 22, 35},
}


# kind: Exact requested atlas identity.
# Raises AssertionError for regressions and CalledProcessError for missing baseline data.
def check_document(kind):
    relative = "docs/Cody/cody-" + kind + "-atlas-v1.pdf"
    original = subprocess.run(["git", "show", BASELINE + ":" + relative], cwd=ROOT,
                              check=True, capture_output=True).stdout
    before = PdfReader(BytesIO(original))
    after = PdfReader(ROOT / relative)
    assert len(before.pages) == 36 and len(after.pages) == 38, "Unexpected atlas topology"
    for number, page in enumerate(before.pages, 1):
        if number not in CHANGED[kind]:
            current = after.pages[number - 1]
            assert current.extract_text() == page.extract_text(), f"Changed retained text: {kind}/{number}"
            assert current.get_contents().get_data() == page.get_contents().get_data(), (
                f"Changed retained drawing commands: {kind}/{number}")
    for number in CHANGED[kind] | {37, 38}:
        text = after.pages[number - 1].extract_text()
        assert REVISION in text, f"Missing revision identity: {kind}/{number}"
        assert "Choose the chat" not in text and "then explicitly select" not in text, (
            f"Obsolete manual-selection instruction: {kind}/{number}")
    assert "Waiting for Codex" in after.pages[0].extract_text(), "Cover is not current"
    last = after.pages[-1].extract_text()
    assert "Errata and corrections" in last and "10 October 2026" in last, "Missing dated final errata"
    assert "15-20" in last and "32" in last, "Missing affected screen IDs"
    print(kind + ": current main pages, retained history and final errata passed")


# kind: Requested atlas to rasterize for human inspection.
# renderer: Existing Poppler executable selected explicitly from PATH.
# Raises CalledProcessError or OSError if rendering or preview output fails.
# Raises ValueError if the rendered page count differs from the authored count.
def render_document(kind, renderer):
    directory = OUTPUT / kind
    directory.mkdir(parents=True, exist_ok=True)
    prefix = directory / "page"
    subprocess.run([renderer, "-scale-to", "1300", "-png",
                    str(ROOT / "docs/Cody" / ("cody-" + kind + "-atlas-v1.pdf")), str(prefix)],
                   check=True)
    pages = sorted(directory.glob("page-*.png"))
    if len(pages) != 38:
        raise ValueError("Unexpected number of rendered pages: " + kind)
    for offset in range(0, len(pages), 12):
        sheet = Image.new("RGB", (1020, 1040), "#DCE4E8")
        labels = ImageDraw.Draw(sheet)
        for index, path in enumerate(pages[offset:offset + 12]):
            x, y = (index % 3) * 340, (index // 3) * 260
            with Image.open(path) as page:
                page.thumbnail((326, 232))
                sheet.paste(page, (x + 7, y + 7))
            labels.text((x + 10, y + 240), f"{kind} / page {offset + index + 1}", fill="#142737")
        sheet.save(directory / f"contact-{offset // 12 + 1}.png")
    print(kind + ": 38 Poppler pages rendered for visual review at " + str(directory))


# No parameters: runs only when explicitly invoked through scripts/device.ps1.
# Raises AssertionError, RuntimeError, OSError or CalledProcessError on failed checks.
def main():
    renderer = shutil.which("pdftoppm")
    if renderer is None:
        raise RuntimeError("Poppler pdftoppm is required for PDF visual review")
    for kind in CHANGED:
        check_document(kind)
        render_document(kind, renderer)


if __name__ == "__main__":
    main()
