"""Check all five revised Cody PDFs and render them for explicitly authorized review."""

import argparse
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
import shutil
import subprocess

from PIL import Image, ImageDraw
from pypdf import PdfReader


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "094ac0e19a5d004add46dcc73a5dd140ee901c73"
REVISION = "CODY-RETAINED-FLOW-20261010"
OUTPUT = ROOT / "artifacts/pdf-review" / REVISION.lower()
ADDED_PAGES = 4


@dataclass(frozen=True)
class ReviewSpec:
    filename: str
    original_pages: int
    main_pages: int
    changed_pages: frozenset


DOCUMENTS = (
    ReviewSpec("cody-navigation-atlas-v1.pdf", 38, 12, frozenset({1, 2, 3, 4, 5, 6, 10, 12})),
    ReviewSpec("cody-planning-atlas-v1.pdf", 38, 22, frozenset({1, 2, 4, 5, 6, 7, 8, 9, 10, 14, 16, 21, 22})),
    ReviewSpec("cody-ccp-pairing-quickstart-a5-v1.pdf", 13, 2, frozenset({1, 2})),
    ReviewSpec("cody-ccp-service-atlas-v1.pdf", 20, 10, frozenset({1, 2, 5, 7, 8, 10})),
    ReviewSpec("cody-codex-connection-atlas-v1.pdf", 24, 14, frozenset({1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13})),
)


# spec: Independent expected source topology and intentional page replacements.
# Raises AssertionError for incomplete changes or modified retained pages.
# Raises CalledProcessError or OSError for an unreadable versioned baseline.
def check_document(spec):
    relative = "docs/Cody/" + spec.filename
    original = subprocess.run(["git", "show", BASELINE + ":" + relative], cwd=ROOT,
                              check=True, capture_output=True).stdout
    before = PdfReader(BytesIO(original))
    after = PdfReader(ROOT / relative)
    assert len(before.pages) == spec.original_pages, "Unexpected baseline topology: " + spec.filename
    assert len(after.pages) == spec.original_pages + ADDED_PAGES, "Unexpected revision topology: " + spec.filename
    assert all(number <= spec.main_pages for number in spec.changed_pages), "A revision overwrites historical pages"
    for number, original_page in enumerate(before.pages, 1):
        current_page = after.pages[number - 1]
        # PDF numeric serialization may round the last decimals without changing the paper size.
        assert all(abs(float(current) - float(original)) < 0.01
                   for current, original in zip(current_page.mediabox, original_page.mediabox)), (
            f"Page geometry changed: {spec.filename}/{number}")
        if number not in spec.changed_pages:
            assert current_page.extract_text() == original_page.extract_text(), (
                f"Retained text changed: {spec.filename}/{number}")
            assert current_page.get_contents().get_data() == original_page.get_contents().get_data(), (
                f"Retained drawing commands changed: {spec.filename}/{number}")
    authored = spec.changed_pages | frozenset(range(spec.original_pages + 1, len(after.pages) + 1))
    for number in authored:
        text = after.pages[number - 1].extract_text() or ""
        assert REVISION in text, f"Missing revision identity: {spec.filename}/{number}"
        assert "Choose the chat" not in text and "then explicitly select" not in text, (
            f"Obsolete manual-selection instruction: {spec.filename}/{number}")
    # Paragraph line wrapping is visual geometry, not a change to an action's wording.
    main_text = " ".join(" ".join(after.pages[number - 1].extract_text() or ""
                                 for number in sorted(spec.changed_pages)).split())
    for required in ("Already paired", "Keep pairing", "Pair again", "Forget", "Waiting for Codex"):
        assert required in main_text, f"Current main pages lack {required}: {spec.filename}"
    cover = after.pages[0].extract_text() or ""
    assert REVISION in cover, "Revision only appended; cover is still old: " + spec.filename
    last = " ".join((after.pages[-1].extract_text() or "").split())
    assert "Errata and corrections" in last and "10 ottobre 2026" in last, "Missing dated final errata"
    # The documented inclusive range 40-42 contains the Setup screen 41.
    assert "analisi iniziale" in last and "40" in last and ("41" in last or "40-42" in last), (
        "Missing cause or affected screen IDs")
    assert "Pagine originali" in last, "Missing original page references"
    print(spec.filename + ": current main pages, unchanged history and final errata passed")


# spec: Requested PDF and independent expected page count.
# renderer: Existing Poppler executable selected from PATH.
# output: Explicit repository artifact directory for review images.
# Raises CalledProcessError or OSError when rendering or preview output fails.
# Raises ValueError when the rendered pages do not match the document topology.
def render_document(spec, renderer, output):
    directory = output / spec.filename.removesuffix(".pdf")
    directory.mkdir(parents=True, exist_ok=True)
    prefix = directory / "page"
    subprocess.run([renderer, "-scale-to", "1300", "-png", str(ROOT / "docs/Cody" / spec.filename),
                    str(prefix)], check=True)
    pages = sorted(directory.glob("page-*.png"))
    if len(pages) != spec.original_pages + ADDED_PAGES:
        raise ValueError("Unexpected rendered page count: " + spec.filename)
    for offset in range(0, len(pages), 12):
        sheet = Image.new("RGB", (1020, 1120), "#DCE4E8")
        labels = ImageDraw.Draw(sheet)
        for index, path in enumerate(pages[offset:offset + 12]):
            x, y = (index % 3) * 340, (index // 3) * 280
            with Image.open(path) as page:
                page.thumbnail((326, 244))
                sheet.paste(page, (x + 7 + (326 - page.width) // 2, y + 7))
            labels.text((x + 10, y + 257), "Page " + str(offset + index + 1), fill="#142737")
        sheet.save(directory / f"contact-{offset // 12 + 1}.png")
    print(spec.filename + ": rendered for visual review at " + str(directory))


# argv: Explicit review arguments forwarded by scripts/device.ps1 after authorization.
# Raises AssertionError, RuntimeError, OSError or CalledProcessError on any failed check.
# Raises ValueError if the review destination escapes the repository artifact directory.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF review output must remain under repository artifacts")
    renderer = shutil.which("pdftoppm")
    if renderer is None:
        raise RuntimeError("Poppler pdftoppm is required for PDF visual review")
    for spec in DOCUMENTS:
        check_document(spec)
    for spec in DOCUMENTS:
        render_document(spec, renderer, output)


if __name__ == "__main__":
    main()
