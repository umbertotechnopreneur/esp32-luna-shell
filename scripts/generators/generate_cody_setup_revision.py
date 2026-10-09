"""Append the requested startup/setup correction to the five existing Cody PDFs."""

import argparse
from io import BytesIO
from pathlib import Path
from xml.sax.saxutils import escape

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph

ROOT = Path(__file__).resolve().parents[2]
REVISION = "09 October 2026"
REVISION_ID = "CODY-SETUP-20261009"
INK = HexColor("#142737")
MUTED = HexColor("#536776")
BLUE = HexColor("#359FE7")
PALE = HexColor("#F1F6F9")
RULE = HexColor("#D6E1E8")
DOCUMENTS = {
    "cody-navigation-atlas-v1.pdf": (
        "NAVIGATION ATLAS", "Original pages 02, 04, 16 and 25; screens 04-16, 35B, 36. "
        "Earlier errata on pages 19, 23 and 26 remain unchanged."),
    "cody-planning-atlas-v1.pdf": (
        "PLANNING ATLAS", "Original pages 02 (inventory), 07 (preferences/PC setup) and 26 "
        "(A04 handoff); screens 04-16, 35B, 36. The inventory gains 40, 41 and 42."),
    "cody-ccp-pairing-quickstart-a5-v1.pdf": (
        "CCP QUICK START", "Original page 01 (ZIP installation concept) and page 02 "
        "(pairing entry and reconnect); screens 11-16. The original two-sided leaflet is retained."),
    "cody-ccp-service-atlas-v1.pdf": (
        "CCP SERVICE ATLAS", "Original pages 01 (scope), 06 (retained link) and 07 "
        "(rediscovery); screens 12-16. Existing protocol/security qualifications remain in force."),
    "cody-codex-connection-atlas-v1.pdf": (
        "CODEX CONNECTION ATLAS", "Original pages 02 (whole journey), 03 (prerequisites), "
        "04 (code), 07-08 (Idle/search), 11 (returning PC) and 13 (owners); screens 11-16."),
}


# fonts: Explicit Windows font directory containing the original atlas type family.
# Raises OSError for absent fonts; no font substitution changes the document silently.
def register_fonts(fonts):
    pdfmetrics.registerFont(TTFont("SetupBody", str(fonts / "segoeui.ttf")))
    pdfmetrics.registerFont(TTFont("SetupBold", str(fonts / "segoeuib.ttf")))


# pdf: Current revision canvas.
# text: Plain text; user/document text cannot inject paragraph markup.
# x: Left edge in points.
# top: Top edge in points, measured down from the page top.
# width: Bounded paragraph width.
# page_height: Original page height in points.
# size: Embedded body font size.
# bold: Selects the explicitly registered bold face.
# color: Text ink.
# Raises ValueError when a paragraph leaves the printable page.
def paragraph(pdf, text, x, top, width, page_height, size=11, bold=False, color=INK):
    style = ParagraphStyle("setup", fontName="SetupBold" if bold else "SetupBody",
                           fontSize=size, leading=size * 1.4, textColor=color)
    block = Paragraph(escape(text), style)
    _, height = block.wrap(width, page_height)
    if top + height > page_height - 52:
        raise ValueError(f"Revision paragraph overflows page: {text[:60]}")
    block.drawOn(pdf, x, page_height - top - height)
    return top + height


# pdf: Canvas using the original document dimensions.
# dimensions: Original width/height, including the A5 quick start.
# label: Exact atlas identity.
# title: Revision-page title.
# number: Physical page number, continuing the preserved originals.
# subtitle: Short explanation of the page's purpose.
# Raises ValueError for title/subtitle overflow.
def heading(pdf, dimensions, label, title, number, subtitle):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    paragraph(pdf, "CODY / " + label + " / SETUP REVISION", margin, 22,
              width - 2 * margin, height, 8, True, MUTED)
    paragraph(pdf, title, margin, 58, width - 2 * margin, height,
              24 if width < 500 else 29, True)
    paragraph(pdf, subtitle, margin, 108, width - 2 * margin, height, 10, False, MUTED)
    pdf.setStrokeColor(RULE)
    pdf.line(margin, 47, width - margin, 47)
    pdf.setFillColor(MUTED)
    pdf.setFont("SetupBody", 7)
    pdf.drawString(margin, 32, "Umberto Giacobbi | hello@umbertogiacobbi.biz | confidential")
    pdf.drawString(margin, 20, REVISION + " | Source implementation; firmware/device unverified")
    pdf.drawRightString(width - margin, 20, str(number))


# pdf: Revision canvas; the diagram is illustrative and contains no actual device pixels.
# x: Center of the circular screen.
# top: Screen center distance from page top.
# radius: Diagram radius in points.
# page_height: Original page height.
# title: Printable screen caption.
# upper: First capsule label.
# lower: Second capsule label.
# back: Shows the separate left previous-route control only when valid.
def illustrative_screen(pdf, x, top, radius, page_height, title, upper, lower, back=False):
    y = page_height - top
    scale = radius / 233
    pdf.setFillColor(HexColor("#000000"))
    pdf.circle(x, y, radius, stroke=0, fill=1)
    pdf.setFillColor(HexColor("#FFFFFF"))
    for dx, dy, r in ((-44, -5, 33), (-12, 15, 43), (34, 9, 40), (63, -7, 27)):
        pdf.circle(x + dx * scale, y + (73 + dy) * scale, r * scale, stroke=0, fill=1)
    pdf.setFillColor(INK)
    for dx in (-24, 24):
        pdf.circle(x + dx * scale, y + 75 * scale, 5 * scale, stroke=0, fill=1)
    pdf.setFont("SetupBold", 26 * scale)
    pdf.setFillColor(HexColor("#FFFFFF"))
    pdf.drawCentredString(x, y - 94 * scale, title)
    for logical_top, text in ((346, upper), (390, lower)):
        pdf.setFillColor(BLUE)
        bottom = y + (233 - logical_top - 40) * scale
        pdf.roundRect(x - 120 * scale, bottom, 240 * scale, 40 * scale, 20 * scale, stroke=0, fill=1)
        pdf.setFillColor(INK)
        pdf.setFont("SetupBold", 18 * scale)
        pdf.drawCentredString(x, bottom + 14 * scale, text)
    if back:
        pdf.setStrokeColor(HexColor("#FFFFFF"))
        pdf.setLineWidth(3 * scale)
        left = x - 193 * scale
        pdf.line(left + 4 * scale, y + 8 * scale, left - 4 * scale, y)
        pdf.line(left - 4 * scale, y, left + 4 * scale, y - 8 * scale)


# pdf: Revision canvas.
# dimensions: Preserved source page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def startup_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Startup: two saved decisions", number,
            "Local Wi-Fi/IP first. Then the retained Codex pairing. Public Internet is not required.")
    text_width = width - 2 * margin if compact else 478
    top = 161
    cases = [
        ("1 / No saved Wi-Fi profile", "A valid absent-profile observation opens 04 > 05 > 06 > 07 > 09. "
         "Debug may show 39 before 09. Continue: 10 > 36 > 11 (skin), then the Codex decision."),
        ("2 / Saved Wi-Fi; waiting for the network", "42 / No connection keeps the profile while the Wi-Fi owner "
         "reconnects. Network setup reopens 04-09; Later returns to Idle. Invalid/stale data shows UNAVAILABLE."),
        ("3 / LAN ready; no saved Codex pairing", "40 / Connect Codex? Yes starts the code on 12. "
         "No returns to 16 / Idle without a settings write or CCP request. The next boot may ask again."),
        ("4 / LAN ready; saved Codex pairing", "Adopt the saved attempt and search on 14. Only authenticated "
         "READY enters 15. A retained identity alone never means the PC is connected."),
    ]
    size = 9.5 if compact else 11
    for title, body in cases:
        top = paragraph(pdf, title, margin, top, text_width, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 3, text_width, height, size) + (10 if compact else 15)
    if not compact:
        illustrative_screen(pdf, 660, 274, 88, height, "Connect Codex?", "Yes", "No")
        paragraph(pdf, "40 / Explicit Yes or No", 558, 381, 225, height, 12, True)
        paragraph(pdf, "Illustrative UI. Choice controls do not start pairing until the user acts.",
                  558, 407, 225, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def setup_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Reopen setup from Cody", number,
            "Hold the visible character still for 1.5 seconds. No button-based factory reset is added.")
    content_width = width - 2 * margin if compact else 470
    top = 163
    rows = [
        ("41 / Setup", "Connect Codex opens a fresh display-code attempt. Network setup reopens 04-09. "
         "Left Back returns to Idle. Opening or leaving the menu erases no Wi-Fi, skin or pairing."),
        ("Keep the gesture deliberate", "Available from stable Idle, PC search/connected and terminal CCP errors. "
         "Moving more than 12 px, a sample gap over 250 ms, rotation or a lifecycle interruption cancels the hold. "
         "Busy work and active provisioning are excluded; release before choosing."),
        ("PWR / existing policy", "Single: standby/wake; active Debug defaults to screenshot countdown. "
         "Long release: standby. Debug double within 500 ms, or a continuous 3-second hold: restart preserving data."),
        ("BOOT / existing policy", "Short: restore a dim/sleeping panel first, otherwise toggle fullscreen/status arcs. "
         "Hold 3 seconds: Debug restart; Release has no firmware reset action. Saved Wi-Fi and CCP survive restart."),
    ]
    size = 9.5 if compact else 11
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, content_width, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 3, content_width, height, size) + 11
    if not compact:
        illustrative_screen(pdf, 660, 273, 88, height, "Setup", "Connect Codex", "Network setup", True)
        paragraph(pdf, "41 / Reversible setup access", 558, 383, 225, height, 12, True)
        paragraph(pdf, "Illustrative UI. A true factory erase is a separate explicitly requested operation.",
                  558, 410, 225, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def install_page(pdf, dimensions, label, number):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "CCP: current local installation", number,
            "This checkout provides a local marketplace. The original ZIP leaflet remains a packaging concept.")
    top = 165
    rows = [
        ("1 / Register the checkout", "Use this repository's .agents/plugins/marketplace.json. The catalog is "
         "Cody local plugins; the package is CCP - Cody's Codex Plugin. Keep the checkout in its current location."),
        ("2 / Install, enable, new chat", "Open the repository installation link below in Codex desktop. Install "
         "and enable CCP, then start a new chat. Python 3 must be available as python. Codex starts the MCP script."),
        ("3 / Pair from Cody", "At startup, choose Yes on 40; alternatively hold Cody and select Connect Codex on 41. "
         "Keep the six-character code on 12 visible. In the CCP-enabled chat, ask to pair Cody using that exact code."),
        ("4 / Confirm and reconnect", "After authenticated completion, 13 > Let's go > 16 (10 active seconds) > "
         "14 > 15. On a later boot with a valid saved link, go straight to authenticated search on 14. "
         "Keep Codex/CCP awake on the same LAN; screen 15 certifies CCP presence, not chat/task transport."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, 10.5, True)
        top = paragraph(pdf, body, margin, top + 3, width - 2 * margin, height, 9.5) + 12
    url = ("codex://plugins/cody-codex-plugin?marketplacePath="
           "E%3A%2FESP32%20Luna%20Shell%2F.agents%2Fplugins%2Fmarketplace.json")
    pdf.setFillColor(BLUE)
    pdf.setFont("SetupBold", 10)
    y = height - top - 12
    pdf.drawString(margin, y, "Open CCP installation in Codex")
    pdf.linkURL(url, (margin, y - 3, margin + 240, y + 12), relative=0)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# affected: Original physical page/screen IDs whose statements this revision supersedes.
# number: Final physical page number.
# original_count: Preserved original page count, including earlier errata.
# Raises ValueError for content overflow.
def errata_page(pdf, dimensions, label, affected, number, original_count):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "Errata and corrections", number,
            "Revision date: " + REVISION + " / " + REVISION_ID)
    top = 162
    rows = [
        ("Original analysis gap", "The initial analysis omitted startup with Wi-Fi already configured but no "
         "saved Codex pairing, and did not define a reversible user entry to setup. This was an original analysis "
         "gap (buco di analisi iniziale), now addressed in source."),
        ("Affected original pages and screen IDs", affected),
        ("Corrections", "Add 40 / optional Yes-No Codex choice, 41 / character-hold Setup, and 42 / reconnect wait. "
         "09 means local Wi-Fi/IP; 11 selects a skin. No/Later performs no persistence or new CCP request. "
         "Saved pairing returns to 14 and requires authenticated READY for 15. PWR/BOOT do not clear pairing."),
        ("Preservation", f"All {original_count} previous pages, earlier corrections, bookmarks and unrelated "
         "content are retained. This dated addendum supersedes only the cited startup/setup/install statements; "
         "the original numbering stays stable."),
        ("Evidence and limits", "Firmware source implementation and static integration review completed. "
         "PDF pages rendered and visually reviewed. No firmware tests, validation scans, configure/build/size, "
         "simulator execution, device pairing/input, flash or erase accompanied this revision. Runtime validation is pending."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, 10.5, True)
        top = paragraph(pdf, body, margin, top + 3, width - 2 * margin, height,
                        9 if width < 500 else 11) + 11
    pdf.showPage()


# name: One explicitly requested source PDF filename.
# label: Canonical atlas label.
# affected: Original affected page/screen IDs.
# staging: Repository-local artifact directory for preserved originals and authoring output.
# Raises OSError or ValueError for missing/ambiguous inputs; publishes only fully authored PDFs.
def revise(name, label, affected, staging):
    source = ROOT / "docs/cody" / name
    reader = PdfReader(source)
    if any(REVISION_ID in (page.extract_text() or "") for page in reader.pages):
        raise ValueError(f"Revision {REVISION_ID} already exists in {name}; do not duplicate it")
    dimensions = tuple(float(value) for value in (reader.pages[0].mediabox.width, reader.pages[0].mediabox.height))
    original_count = len(reader.pages)
    staging.mkdir(parents=True, exist_ok=True)
    backup = staging / (source.stem + "-original.pdf")
    if backup.exists():
        raise ValueError(f"Preserved original already exists: {backup}")
    backup.write_bytes(source.read_bytes())
    buffer = BytesIO()
    pdf = canvas.Canvas(buffer, pagesize=dimensions, pageCompression=1)
    pdf.setAuthor("Umberto Giacobbi")
    startup_page(pdf, dimensions, label, original_count + 1)
    setup_page(pdf, dimensions, label, original_count + 2)
    added = 2
    if "quickstart" in name:
        install_page(pdf, dimensions, label, original_count + 3)
        added += 1
    errata_page(pdf, dimensions, label, affected, original_count + added + 1, original_count)
    pdf.save()
    buffer.seek(0)
    writer = PdfWriter()
    writer.clone_document_from_reader(reader)
    writer.append(PdfReader(buffer), import_outline=False)
    staged = staging / name
    with staged.open("wb") as output:
        writer.write(output)
    print(f"{name}: {original_count} -> {original_count + added + 1} pages; earlier content preserved")
    return source, staged


# argv: Literal authoring arguments forwarded by the sole public device.ps1 entrypoint.
# Raises OSError for unreadable documents/fonts and ValueError for duplicate/overflow revisions.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, default=Path("C:/Windows/Fonts"))
    parser.add_argument("--staging", type=Path, default=ROOT / "artifacts/docs/cody-setup-20261009")
    args = parser.parse_args(argv)
    staging = args.staging.resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF staging must stay under the repository artifacts directory")
    register_fonts(args.font_dir)
    prepared = [revise(name, label, affected, staging)
                for name, (label, affected) in DOCUMENTS.items()]
    # Complete every requested revision before changing any of the original documents.
    for source, staged in prepared:
        source.write_bytes(staged.read_bytes())


if __name__ == "__main__":
    main()
