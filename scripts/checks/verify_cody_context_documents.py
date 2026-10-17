# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Review the canonical Cody PDFs after explicit authorization through device.ps1."""

import argparse
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import re
import shutil
import subprocess

from PIL import Image, ImageDraw
from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "artifacts/pdf-review/cody-current-documents"


class DocumentKind(str, Enum):
    Atlas = "atlas"
    Quickstart = "quickstart"


@dataclass(frozen=True)
class ReviewSpec:
    filename: str
    kind: DocumentKind
    # Original body pages exclude the publisher's cover and live contents.
    main_pages: int | None
    required: tuple[str, ...]


DOCUMENTS = (
    ReviewSpec("cody-navigation-atlas-v1.pdf", DocumentKind.Atlas, 15,
               ("Model & Context", "Session & Link", "shake", "planned", "NOTES")),
    ReviewSpec("cody-planning-atlas-v1.pdf", DocumentKind.Atlas, 24,
               ("Model & Context", "Session & Link", "planned voice", "NOTES")),
    ReviewSpec("cody-ccp-connection-atlas-v1.pdf", DocumentKind.Atlas, 10,
               ("FIND", "READY", "COMMITTED", "DPAPI", "NVS", "answer_id", "ACK",
                "PCM", "5 s quiet", "queue", "omitted", "No transcript", "not encrypted",
                "PAKE", "CCP1.md", "model/list", "Debug and Release", "Memory")),
    ReviewSpec("cody-ccp-pairing-quickstart-a5-v1.pdf", DocumentKind.Quickstart, 4,
               ("Wi-Fi", "Codex", "code")),
)
RETIRED_DOCUMENTS = ("cody-ccp-service-atlas-v1.pdf", "cody-codex-connection-atlas-v1.pdf")


# value: Extracted PDF text whose visual line wrapping has no semantic meaning.
def normalized(value):
    return " ".join(value.split())


# value: Extracted page text used to locate the final dated grid.
def is_register_page(value):
    lines = [normalized(line).casefold() for line in value.splitlines() if line.strip()]
    heading = any(line == "security" or line.startswith("revision register") for line in lines)
    compact = normalized(value).casefold().replace(" ", "")
    return heading and "date/type" in compact


# spec: Canonical document identity, main section boundary and independent content requirements.
# Raises AssertionError for obsolete topology, absent content or misplaced revision history.
# Raises OSError for an unreadable PDF.
# Raises ValueError for an invalid PDF bookmark destination.
def check_document(spec):
    reader = PdfReader(ROOT / "docs/cody" / spec.filename)
    texts = [page.extract_text() or "" for page in reader.pages]
    assert texts, "Empty document: " + spec.filename
    expected_size = (419.53, 595.28) if spec.kind == DocumentKind.Quickstart else (841.89, 595.28)
    for number, page in enumerate(reader.pages, 1):
        dimensions = (float(page.mediabox.width), float(page.mediabox.height))
        assert all(abs(actual - expected) < 1 for actual, expected in zip(dimensions, expected_size)), (
            f"Unexpected paper geometry: {spec.filename}/{number}")
    if spec.kind == DocumentKind.Quickstart:
        assert len(texts) == 4, "The nontechnical A5 flyer must remain four pages"
        content = normalized(" ".join(texts)).casefold()
        for forbidden in ("revision register", "errata", "addendum", "security appendix"):
            assert forbidden not in content, "Technical appendix leaked into the A5 flyer: " + forbidden
        main_count = 4
    else:
        # The current publisher adds a cover and navigable contents without
        # renumbering historical screen/page identities in the retained body.
        frontmatter_pages = 2
        assert "CODY / VIBEWARE" in texts[0], "Missing publisher cover: " + spec.filename
        assert texts[1].lstrip().startswith("Contents"), "Missing live contents: " + spec.filename
        bookmarks = {}
        for destination in reader.outline:
            title = getattr(destination, "title", None)
            if title in ("Cover", "Contents", "Revision register", "Security"):
                assert title not in bookmarks, "Ambiguous section bookmark: " + title
                bookmarks[title] = reader.get_destination_page_number(destination)
        assert bookmarks.get("Cover") == 0 and bookmarks.get("Contents") == 1, (
            "Front matter bookmarks do not match their pages: " + spec.filename)
        register_pages = [number for number, text in enumerate(texts) if is_register_page(text)]
        assert register_pages, "Missing final dated revision grid: " + spec.filename
        main_count = register_pages[0]
        if spec.main_pages is not None:
            expected_boundary = frontmatter_pages + spec.main_pages
            assert main_count == expected_boundary, (
                f"Unexpected main section boundary: {spec.filename}; "
                f"expected {expected_boundary}, found {main_count}")
        assert bookmarks.get("Revision register") == main_count, (
            "Contents bookmark does not locate the final revision grid: " + spec.filename)
        assert all(is_register_page(text) for text in texts[main_count:]), (
            "Standalone appendix remains outside the final grid: " + spec.filename)
        history = normalized(" ".join(texts[main_count:]))
        for category in ("Addendum", "Change", "Errata", "Security"):
            assert category.casefold() in history.casefold(), "Missing historical category: " + category
        assert re.search(r"(?:[0-9]{1,2} October 2026|2026-10-[0-9]{2})", history), (
            "Historical revision dates are absent: " + spec.filename)
        # A later dated Change may follow Security. Require an actual labelled
        # Security section inside the final grid, with its own working bookmark.
        security_pages = [number for number in register_pages if any(
            normalized(line).casefold() == "security" or
            normalized(line).casefold().endswith(" / security")
            for line in texts[number].splitlines()[:5])]
        assert security_pages, "Missing labelled Security register section: " + spec.filename
        assert bookmarks.get("Security") == security_pages[0], (
            "Contents bookmark does not locate Security: " + spec.filename)
    main = normalized(" ".join(texts[:main_count])).casefold()
    for required in spec.required:
        assert required.casefold() in main, f"Current main section lacks {required}: {spec.filename}"
    if spec.filename == "cody-ccp-connection-atlas-v1.pdf":
        # The current customer guide delegates exact wire fields to CCP1.md.
        # Retired listener/turn injection and Debug-only diagnostics must not be
        # reintroduced as current behavior; dated register history stays intact.
        for obsolete in ("turn/start", "turn/steer", "only on a debug device"):
            assert obsolete not in main, "Obsolete current communication route: " + obsolete
    for obsolete in ("choose the chat", "then explicitly select"):
        assert obsolete not in main, "Obsolete manual chat-selection route: " + spec.filename
    print(f"{spec.filename}: {len(texts)} pages; canonical topology and content passed")


# spec: Canonical PDF selected for explicitly authorized visual review.
# renderer: Existing Poppler executable selected from PATH.
# output: Repository artifact directory for disposable review images.
# Raises CalledProcessError or OSError when rendering or contact-sheet output fails.
# Raises ValueError if rendered output disagrees with the current PDF page count.
def render_document(spec, renderer, output):
    directory = output / spec.filename.removesuffix(".pdf")
    directory.mkdir(parents=True, exist_ok=True)
    # Remove only this renderer's generated files; leave PDFs and unrelated artifacts intact.
    for pattern in ("page-*.png", "contact-*.png"):
        for existing in directory.glob(pattern):
            existing.unlink()
    prefix = directory / "page"
    subprocess.run([renderer, "-scale-to", "1300", "-png", str(ROOT / "docs/cody" / spec.filename),
                    str(prefix)], check=True)
    pages = sorted(directory.glob("page-*.png"))
    expected = len(PdfReader(ROOT / "docs/cody" / spec.filename).pages)
    if len(pages) != expected:
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
# Raises AssertionError, RuntimeError, OSError or CalledProcessError on failed review.
# Raises ValueError if the review destination escapes the repository artifact directory.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF review output must remain under repository artifacts")
    for retired in RETIRED_DOCUMENTS:
        assert not (ROOT / "docs/cody" / retired).exists(), "Retired technical duplicate remains: " + retired
    renderer = shutil.which("pdftoppm")
    if renderer is None:
        raise RuntimeError("Poppler pdftoppm is required for PDF visual review")
    for spec in DOCUMENTS:
        check_document(spec)
    for spec in DOCUMENTS:
        render_document(spec, renderer, output)


if __name__ == "__main__":
    main()
