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

"""Append the VibeWare marketing/boot revision while preserving both existing atlases."""

import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import shutil
import subprocess
import sys

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "simulator/windows"))
from scripts.generators.generate_cody_idle_atlas import ATLAS, W, H, text, paragraph
from scripts.generators.generate_cody_documentation_revision import register_fonts
from vibeware_boot import VibeWareBoot

OUTPUT = ROOT / "artifacts/pdf-review/vibeware-boot-2026-10-06"
NAMES = ("cody-manga-status-atlas-v1.pdf", "cody-navigation-atlas-v1.pdf")
MARKER = "VIBEWARE BOOT IDENTITY / 06 OCT 2026"
MANIFESTO = "https://umbertogiacobbi.biz/vibeware/manifesto"
INK = "#142737"
BLUE = "#359FE7"


# document: Destination ReportLab canvas.
# title: English page heading.
# subtitle: Concise scope and original references.
# number: Absolute page number in the revised PDF.
# kind: Stable atlas identity.
def page_frame(document, title, subtitle, number, kind):
    document.setFillColor(HexColor("#FFFFFF"))
    document.rect(0, 0, W, H, fill=1, stroke=0)
    text(document, "CODY / " + kind + " / " + MARKER, 36, 29, 8, BLUE, "MonoBold")
    text(document, title, 36, 66, 25, INK, "BodyBold")
    text(document, subtitle, 36, 91, 10)
    document.setStrokeColor(HexColor("#D6E1E8"))
    document.line(36, H - 108, W - 36, H - 108)
    document.line(36, 43, W - 36, 43)
    text(document, "06 October 2026 | Approved artwork; implemented source; device behavior unverified", 36, H - 27, 8)
    text(document, "Previous pages and earlier errata retained. This revision supersedes only the cited boot/branding statements.", 36, H - 13, 7)
    text(document, f"{number:02d}", W - 53, H - 13, 9, BLUE, "MonoBold")


# document: Destination canvas.
# title: Short section heading.
# copy: English wrapped explanation.
# x: Left coordinate in PDF points.
# y: Heading baseline measured from the top.
# width: Text column width.
def block(document, title, copy, x, y, width):
    text(document, title, x, y, 13, INK, "BodyBold")
    return paragraph(document, copy, x, y + 23, width, 10.5) + 18


# document: Destination canvas.
# number: Absolute page number.
# kind: Stable atlas identity.
# artwork: Read-only original animation owner used for document illustrations.
# Raises OSError for unavailable illustration output.
def marketing_page(document, number, kind, artwork):
    page_frame(document, "VibeWare first. Cody next.",
        "B01 / The approved floppy-insertion animation becomes the opening brand signature.", number, kind)
    paragraph(document, "Every cold boot begins with the VibeWare floppy. Its familiar pixel form introduces the maker before the companion comes to life.", 36, 133, W - 72, 13, INK)
    samples = (0, 580, 1250, 1910, 3000)
    for index, at_ms in enumerate(samples):
        image_path = OUTPUT / f"floppy-{at_ms:04d}.png"
        artwork.image(at_ms).save(image_path)
        x = 36 + index * 157
        document.saveState()
        clip = document.beginPath()
        clip.circle(x + 67.5, H - 238.5, 67.5)
        document.clipPath(clip, stroke=0, fill=0)
        document.drawImage(str(image_path), x, H - 306, width=135, height=135)
        document.restoreState()
        text(document, f"{at_ms / 1000:.2f} s", x + 45, 325, 10, BLUE, "MonoBold")
    y = block(document, "A recognizable opening", "The blue floppy and VW mark remain the original approved artwork. Black resolves the transparent background; exact twofold pixel scaling keeps the retro visual character crisp and centered on the circular display.", 36, 363, 364)
    block(document, "From identity to personality", "The insertion motion belongs to the VibeWare introduction. Cody's existing Sleeping 01, Waking 02 and Ready 03 follow it, so the brand has its own moment before the character takes over.", 439, 363, 366)
    text(document, "VibeWare manifesto", 36, y + 9, 11, BLUE, "BodyBold")
    text(document, MANIFESTO, 36, y + 28, 10, BLUE)
    document.linkURL(MANIFESTO, (36, H - y - 33, 415, H - y + 3), relative=0)
    text(document, "A shared opening signature for Cody and Luna.", 439, y + 27, 10, INK)
    document.showPage()


# document: Destination canvas.
# number: Absolute page number.
# kind: Stable atlas identity.
def behavior_page(document, number, kind):
    page_frame(document, "The VibeWare startup sequence", "B02 / VibeWare precedes screens 01-03; first-run setup and returning-device routes retain their identities.", number, kind)
    stages = (("VIBEWARE", "3.5 s per cycle", "Approved floppy"),
              ("01 / SLEEPING", "2.0 s", "Existing Cody fade"),
              ("02 / WAKING", "1.5 s", "Existing expression"),
              ("03 / READY", "2.0 s", "Existing expression"),
              ("NEXT ROUTE", "Existing policy", "Hello 04 or Idle 16"))
    for index, (title, duration, detail) in enumerate(stages):
        x = 36 + index * 156
        document.setFillColor(HexColor("#EAF4FB" if index == 0 else "#F1F6F9"))
        document.roundRect(x, H - 243, 145, 105, 10, stroke=0, fill=1)
        text(document, title, x + 11, 164, 10, BLUE, "MonoBold")
        text(document, duration, x + 11, 193, 12, INK, "BodyBold")
        text(document, detail, x + 11, 219, 9)
    paragraph(document, "Nominal artwork dwell: at least 3.5 s of branding plus the existing 5.5 s character sequence. Real startup work, frame transfer and deliberate DMA parking can add wall time.", 36, 270, W - 72, 11)
    block(document, "Complete the visible cycle", "The 42 original frames retain their 80/90-ms timings. Cody starts each dwell after frame retirement and repeats while the real service checkpoint is pending. A successful checkpoint never truncates the current cycle or invents numeric loading progress.", 36, 323, 236)
    block(document, "Keep the right owner", "Cody uses native 2D and its existing bounded DMA owner. Luna uses LVGL with the same flash-owned data. Original transparency is composited on black. No removable SD, network request or on-device GIF decoder is required.", 302, 323, 236)
    block(document, "Preserve ordinary wake", "The introduction belongs to cold boot and software reset. Cody standby wake and actual deep-sleep Wake keep their existing presentation. First-boot, pairing and stored preferences are unchanged. Errors remain real and startup-critical failures stop handoff.", 568, 323, 236)
    text(document, "Implementation evidence", 36, 496, 11, INK, "BodyBold")
    paragraph(document, "Approved GIF packaged; native/LVGL and Windows source integrated. Firmware build/size, host checks, simulator execution, rotations and physical fluidity are still pending explicit authorization.", 36, 516, W - 72, 10)
    document.showPage()


# document: Destination canvas.
# number: Absolute final page number.
# kind: Stable atlas identity.
# prior_pages: Count of preserved original pages, including every earlier erratum.
def errata_page(document, number, kind, prior_pages):
    page_frame(document, "Errata and corrections", "Revision date: 06 October 2026 / VibeWare boot identity / Every earlier page and correction is preserved.", number, kind)
    if kind == "NAVIGATION":
        affected = "Original page 02 (Boot and entry); page 20 / N01 (shorter boot); page 22 / N03 (Studio branding); page 23 (earlier errata). Screen IDs 01-05 and 16 remain unchanged; 35A/35B wake is unaffected."
        corrections = "Replace the statement that startup shows only Cody with the VibeWare introduction before screens 01-03. Retain the existing 2 / 1.5 / 2-second character phases. Replace the old public GitHub manifesto reference with the user-provided website URL. Desktop-only branding is now extended to the device boot."
    else:
        affected = "Original page 02 (meaning before decoration); page 14 (circular layout); page 24 (earlier errata). The branding introduction precedes screens 01-03; the fourteen manga cue identities, face geometry and Idle 16 remain unchanged."
        corrections = "Add the maker identity before the character and its cue vocabulary. Place the original floppy artwork at (105,87), size 256 x 292, wholly within the central safe square. Keep brand animation independent of device measurements, narrative cue selection and ordinary wake."
    y = block(document, "Affected original pages and screens", affected, 36, 144, 365)
    y = block(document, "Corrections in this revision", corrections, 36, y + 11, 365)
    block(document, "Preservation and evidence", f"All {prior_pages} existing pages, earlier errata, bookmarks and original page content are retained. New pages B01 and B02 document the supplied artwork and implemented behavior. This final page records the revision; it does not certify firmware compilation or physical behavior.", 439, 144, 365)
    block(document, "Approved source and canonical reference", "Source: Obsidian / VibeWare / press-kit / assets / boot / vibeware-floppy-insert-transparent.gif. The original is copied unchanged into the repository; generated metadata records its SHA-256, disposal-composited crop, original durations and flash pixel-data size.", 439, 283, 365)
    text(document, MANIFESTO, 439, 437, 10, BLUE)
    document.linkURL(MANIFESTO, (439, H - 443, W - 36, H - 423), relative=0)
    text(document, "Pending validation", 36, 493, 11, INK, "BodyBold")
    paragraph(document, "Build and size budgets, frame dwell under load, real errors, rotation, first setup/returning boot, standby/deep-sleep wake, Windows presentation and physical appearance remain unverified. No firmware tests, simulator launch or hardware operation accompanied this revision.", 36, 514, W - 72, 10)
    document.showPage()


# manifest: Reviewed PDF identities and their original/staged content hashes.
# Raises OSError or ValueError if originals/staged files changed or local application fails.
def apply_reviewed(manifest):
    for item in manifest["documents"]:
        if sha256((ATLAS / item["file"]).read_bytes()).hexdigest() != item["original_sha256"]:
            raise ValueError(f"Original changed during review: {item['file']}")
        if sha256((OUTPUT / item["file"]).read_bytes()).hexdigest() != item["revised_sha256"]:
            raise ValueError(f"Staged PDF changed during review: {item['file']}")
        mirror = ROOT / "docs/cody" / item["file"]
        if sha256(mirror.read_bytes()).hexdigest() != item["original_sha256"]:
            raise ValueError(f"Repository mirror differs from the preserved original: {item['file']}")
    for item in manifest["documents"]:
        for destination in (ATLAS / item["file"], ROOT / "docs/cody" / item["file"]):
            shutil.copy2(OUTPUT / item["file"], destination)
            print("Updated", destination)
    shutil.copy2(OUTPUT / "manifest.json", ATLAS / "source/vibeware-boot-2026-10-06-manifest.json")


# No parameters: stage/render or apply exactly the requested two local PDF revisions.
# Raises OSError, ValueError or a subprocess error for invalid sources or failed PDF rendering.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdftoppm", help="Installed Poppler executable for mandatory document review")
    parser.add_argument("--apply", action="store_true", help="Apply visually reviewed staged PDFs to Obsidian and repository mirrors")
    args = parser.parse_args()
    if args.apply:
        apply_reviewed(json.loads((OUTPUT / "manifest.json").read_text(encoding="utf-8")))
        return
    if not args.pdftoppm:
        parser.error("--pdftoppm is required for visual document review")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    register_fonts()
    artwork = VibeWareBoot(ROOT)
    manifest = {"revision_date": "2026-10-06", "scope": "VibeWare boot identity", "manifesto": MANIFESTO, "documents": []}
    for index, name in enumerate(NAMES):
        source = ATLAS / name
        reader = PdfReader(source)
        if any(MARKER in page.extract_text() for page in reader.pages):
            raise ValueError(f"VibeWare revision already present: {name}")
        first = len(reader.pages) + 1
        backup = OUTPUT / ("original-" + name)
        if not backup.exists():
            shutil.copy2(source, backup)
        buffer = BytesIO()
        document = canvas.Canvas(buffer, pagesize=(W, H))
        kind = "MANGA STATUS" if index == 0 else "NAVIGATION"
        marketing_page(document, first, kind, artwork)
        behavior_page(document, first + 1, kind)
        errata_page(document, first + 2, kind, len(reader.pages))
        document.save()
        writer = PdfWriter()
        writer.clone_document_from_reader(reader)
        for page in PdfReader(BytesIO(buffer.getvalue())).pages:
            writer.add_page(page)
        writer.add_outline_item("VibeWare boot identity - 06 October 2026", first - 1)
        writer.add_outline_item("Errata and corrections - VibeWare - 06 October 2026", first + 1)
        writer.add_metadata({"/ModDate": "D:20261006000000+07'00'", "/Subject": MARKER})
        destination = OUTPUT / name
        with destination.open("wb") as stream:
            writer.write(stream)
        revised = PdfReader(destination)
        if len(revised.pages) != len(reader.pages) + 3 or "Errata and corrections" not in revised.pages[-1].extract_text():
            raise ValueError(f"Incomplete VibeWare revision: {name}")
        for before, after in zip(reader.pages, revised.pages):
            if before.get_contents().get_data() != after.get_contents().get_data():
                raise ValueError(f"Original page content changed: {name}")
        subprocess.run([args.pdftoppm, "-f", str(first), "-l", str(first + 2),
                        "-scale-to", "1400", "-png", str(destination), str(OUTPUT / destination.stem)], check=True)
        manifest["documents"].append({"file": name, "preserved_pages": len(reader.pages), "added_pages": 3,
            "final_pages": len(revised.pages), "original_sha256": sha256(source.read_bytes()).hexdigest(),
            "revised_sha256": sha256(destination.read_bytes()).hexdigest()})
        print("Staged for visual review", destination, "pages", len(revised.pages))
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
