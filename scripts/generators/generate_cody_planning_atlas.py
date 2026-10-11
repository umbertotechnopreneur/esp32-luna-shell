"""Restore the illustrated Cody planning atlas and consolidate its dated history."""

import argparse
from io import BytesIO
import json
from pathlib import Path
import re
import subprocess
import sys

import pymupdf
from PIL import Image, ImageChops, ImageDraw, ImageFilter
from reportlab.lib import colors
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from reportlab.graphics import renderPDF
from reportlab.graphics.shapes import Drawing
from reportlab.graphics.barcode.qr import QrCodeWidget
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle, SimpleDocTemplate
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
STAGING = ROOT / "artifacts/docs/cody-planning-restoration-20261011"
OUTPUT = ROOT / "docs/cody/cody-planning-atlas-v1.pdf"
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, register_fonts
from generate_cody_security_atlases import latest_records, security_records

CYAN = colors.HexColor("#279EFF")
INK = colors.HexColor("#152A3B")
MUTED = colors.HexColor("#547080")
LINE = colors.HexColor("#D9E7EE")
SIZE = (841.89, 595.28)

# text: Printable document copy; newline boundaries are preserved.
# width: Maximum line width in PDF points.
# font: Registered drawing font identity.
# size: Font height in PDF points.
def wrapped(text, width, font="CodyDoc", size=11):
    from reportlab.pdfbase.pdfmetrics import stringWidth
    text = re.sub(r"(?<=[a-z])(?=[0-9])", " ", text)
    lines = []
    for paragraph in text.split("\n"):
        line = ""
        for word in paragraph.split():
            candidate = (line + " " + word).strip()
            if line and stringWidth(candidate, font, size) > width:
                lines.append(line)
                line = word
            else:
                line = candidate
        lines.append(line)
    return lines

# surface: ReportLab PDF drawing owner.
# text: English copy to wrap within its column.
# x: Left coordinate in PDF points.
# y: First baseline in PDF points.
# width: Available column width in PDF points.
# size: Body font size in PDF points.
# color: RGB drawing color.
def paragraph(surface, text, x, y, width, size=11, color=MUTED):
    surface.setFont("CodyDoc", size)
    surface.setFillColor(color)
    for line in wrapped(text, width, size=size):
        surface.drawString(x, y, line)
        y -= size * 1.4
    return y

# surface: PDF drawing owner.
# number: Stable physical main-atlas page number.
# title: Main page heading.
# subtitle: One-line page purpose and evidence boundary.
def frame(surface, number, title, subtitle):
    width, height = SIZE
    surface.setFillColor(CYAN)
    surface.setFont("Courier-Bold", 9)
    surface.drawString(36, height - 30, "CODY / ILLUSTRATED PLANNING ATLAS")
    surface.drawRightString(width - 36, height - 30, "11 OCT 2026")
    surface.setFillColor(INK)
    surface.setFont("CodyDocBold", 26)
    surface.drawString(36, height - 63, title)
    paragraph(surface, subtitle, 36, height - 86, width - 72, size=11)
    surface.setStrokeColor(LINE)
    surface.line(36, height - 104, width - 36, height - 104)
    surface.line(36, 36, width - 36, 36)
    surface.setFillColor(MUTED)
    surface.setFont("CodyDoc", 7)
    surface.drawString(36, 22, "Umberto Giacobbi | umbertogiacobbi.biz | confidential | illustrated source contracts, not device captures")
    surface.setFillColor(CYAN)
    surface.setFont("Courier-Bold", 9)
    surface.drawRightString(width - 36, 22, f"{number:02}")

# historical: Read-only historical source containing approved cyan artwork.
# Raises OSError for failed image staging.
def sprites(historical):
    image_data = historical.extract_image(24)["image"]
    sheet = Image.open(BytesIO(image_data)).convert("RGBA")
    results = []
    for index in range(8):
        column, row = index % 4, index // 4
        tile = sheet.crop((column * sheet.width // 4, row * sheet.height // 2,
                           (column + 1) * sheet.width // 4, (row + 1) * sheet.height // 2))
        # The historical sheet has an opaque black exterior. Flood only that
        # connected exterior to transparency; black facial details stay intact.
        for corner in ((0,0),(tile.width-1,0),(0,tile.height-1),(tile.width-1,tile.height-1)):
            ImageDraw.floodfill(tile, corner, (0,0,0,0), thresh=35)
        visible = tile.getchannel("A").point(lambda value:255 if value else 0)
        # Keep the connected character silhouette, dropping detached cyan
        # artifacts from the old multi-expression sheet's tile boundaries.
        ImageDraw.floodfill(visible,(tile.width//2,tile.height//2),128,thresh=0)
        silhouette = visible.point(lambda value:255 if value == 128 else 0)
        silhouette = silhouette.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
        tile.putalpha(ImageChops.multiply(tile.getchannel("A"),silhouette))
        visible = tile.getchannel("A")
        tile = tile.crop(visible.getbbox())
        results.append(ImageReader(tile))
    return results

# surface: Drawing owner for one illustrated circle.
# item: Stable screen ID, title, copy, controls, notes and expression definition.
# x: Left edge of the screen column.
# width: Screen column width.
# artwork: Eight approved cyan expression sprites.
def screen(surface, item, x, width, artwork):
    identity, title, copy, actions, notes, expression, *extra = item
    surface.setFillColor(CYAN)
    surface.setFont("Courier-Bold", 10)
    surface.drawString(x, 468, identity + " / " + title)
    radius = min(112, width / 2 - 5)
    center_x, center_y = x + width / 2, 343
    surface.setFillColor(colors.black)
    surface.setStrokeColor(INK)
    surface.circle(center_x, center_y, radius, fill=1, stroke=1)
    # Artwork remains cyan even on technical/reference screens; clear the lower
    # circle for captions and controls instead of using a white placeholder.
    native_width, native_height = artwork[expression].getSize()
    scale = min(radius * 1.42 / native_width, radius * 0.85 / native_height)
    image_width, image_height = native_width * scale,native_height * scale
    surface.saveState()
    clipping = surface.beginPath()
    clipping.circle(center_x, center_y, radius)
    surface.clipPath(clipping, stroke=0, fill=0)
    if extra and extra[0] == "voice":
        # A static precalculated seven-bar glyph represents the planned listening
        # animation. The expensive character renderer is absent on these routes.
        surface.setFillColor(CYAN)
        for bar,height in enumerate((14,28,45,60,45,28,14)):
            surface.roundRect(center_x-44+bar*13,center_y+44-height/2,7,height,3,fill=1,stroke=0)
    elif identity == "06":
        # Deliberately invalid editorial QR: the diagram must never send readers
        # to an unintended installer. Its complete quiet zone fits the chord.
        surface.setFillColor(colors.white)
        surface.rect(center_x-48,center_y+1,96,96,fill=1,stroke=0)
        code = QrCodeWidget("https://example.invalid/cody-atlas")
        bounds = code.getBounds()
        qr_scale = 80/(bounds[2]-bounds[0])
        drawing = Drawing(80,80,transform=[qr_scale,0,0,qr_scale,0,0])
        drawing.add(code)
        renderPDF.draw(drawing,surface,center_x-40,center_y+9)
    else:
        surface.drawImage(artwork[expression], center_x - image_width / 2, center_y + 2,
                          width=image_width, height=image_height, mask="auto")
    if extra and extra[0] != "voice":
        icon = ROOT / "assets/cody/personality-icons/generated" / (extra[0] + ".png")
        surface.drawImage(str(icon), center_x + radius * 0.36, center_y + radius * 0.34,
                          width=36, height=36, mask="auto")
    surface.restoreState()
    surface.setFont("CodyDocBold", 11)
    surface.setFillColor(colors.white)
    copy = re.sub(r"(?<=[a-z])(?=[0-9])", " ", copy)
    for index, line in enumerate(copy.split("\n")):
        surface.drawCentredString(center_x, center_y - 17 - index * 16, line)
    if actions:
        count = len(actions)
        for index, label in enumerate(actions):
            pair = count >= 2 and index < 2
            action_width = radius * (0.68 if pair else 1.28)
            action_x = center_x - action_width / 2
            action_y = center_y - 79
            if pair:
                action_x += (-1 if index == 0 else 1) * (action_width / 2 + 3)
            if count == 3 and index == 2:
                action_y -= 27
                action_width = radius * 0.83
                action_x = center_x - action_width / 2
            surface.setFillColor(colors.HexColor("#123E50" if index == 0 else "#263541"))
            surface.setStrokeColor(colors.HexColor("#42A9C4"))
            surface.roundRect(action_x, action_y, action_width, 24, 10, fill=1, stroke=1)
            surface.setFillColor(CYAN)
            surface.setFont("CodyDocBold", 9)
            surface.drawString(action_x + 9, action_y + 8, "+" if index == 0 else ">")
            surface.setFillColor(colors.white)
            surface.setFont("CodyDocBold", 8)
            surface.drawCentredString(action_x + action_width / 2 + 5, action_y + 8, label)
    # The original workbook leaves a generous separate writing area per screen.
    paragraph(surface, notes, x, 212, width, size=10)
    surface.setFillColor(CYAN)
    surface.setFont("Courier-Bold", 9)
    surface.drawString(x, 133, "NOTES")
    surface.setStrokeColor(LINE)
    for y in (120, 101, 82, 63):
        surface.line(x, y, x + width, y)

# number: Stable main-atlas page number.
# title: Page heading.
# subtitle: Evidence or route description.
# items: Two or three screen definitions.
# artwork: Historical cyan sprite images.
# Raises ValueError for unsupported screen count.
def screen_page(number, title, subtitle, items, artwork):
    if len(items) not in (2, 3):
        raise ValueError("Illustrated pages require two or three screens")
    stream = BytesIO()
    surface = canvas.Canvas(stream, pagesize=SIZE)
    frame(surface, number, title, subtitle)
    gap = 26
    column = (SIZE[0] - 72 - gap * (len(items) - 1)) / len(items)
    for index, item in enumerate(items):
        screen(surface, item, 36 + index * (column + gap), column, artwork)
        if index < len(items) - 1 and number != 15:
            arrow_x = 36 + (index + 1) * column + index * gap + 3
            surface.setStrokeColor(CYAN)
            surface.setLineWidth(1.5)
            surface.line(arrow_x,343,arrow_x + gap - 6,343)
            surface.line(arrow_x + gap - 6,343,arrow_x + gap - 12,347)
            surface.line(arrow_x + gap - 6,343,arrow_x + gap - 12,339)
    surface.save()
    return pymupdf.open(stream=stream.getvalue(), filetype="pdf")

# current: Protected previous English PDF containing the complete dated history.
# Raises ValueError if the historical appendix page range is unavailable.
def history_records(current):
    if len(current) < 53:
        raise ValueError("Expected the complete53-page planning history")
    records = []
    for index in range(22, 46):
        page = current[index]
        # Coordinate filtering drops running author/footer/page-number text only.
        # Body content, screen IDs, dates, observed errors and evidence survive.
        blocks = [block for block in page.get_text("blocks") if block[6] == 0]
        spans = [span for block in page.get_text("dict")["blocks"] if "lines" in block
                 for line in block["lines"] for span in line["spans"]]
        title = max(spans,key=lambda span:span["size"])["text"]
        body = [block[4].strip() for block in blocks if block[1] > 115 and block[3] < page.rect.height - 44]
        full_text = page.get_text()
        date = "03 October 2026" if index == 22 else "04 October 2026" if index < 26 else "09 October 2026" if index < 34 else "10 October 2026"
        kind = "Errata" if "Errata and corrections" in full_text else "Addendum" if "ADDENDUM" in full_text or "Addendum" in full_text else "Change"
        # Historical body may contain Unicode typographic dashes and one former
        # Italian aside. Preserve its meaning with English printable characters.
        paragraphs = tuple(" ".join(text.split()).replace("\u2010", "-").replace("\u2011", "-")
                           .replace("\u2013", "-").replace("\u2014", "-")
                           .replace("buco di analisi iniziale", "original analysis gap") for text in body)
        if not paragraphs:
            raise ValueError(f"Missing historical body on original page {index + 1}")
        # Join the former one-line diagram/caption blocks into readable prose.
        # Keep every word; universal historical provenance belongs to the grid
        # heading rather than creating an evidence-only continuation page.
        joined = " ".join(paragraphs)
        words = joined.split()
        grouped = []
        segment = ""
        for word in words:
            if len(segment) + len(word) > 700:
                grouped.append(segment)
                segment = ""
            segment = (segment + " " + word).strip()
        if segment:
            grouped.append(segment)
        records.append(RevisionRecord(date, kind, title, tuple(grouped),
            affected=f"Original planning page {index + 1}; original screen IDs retained in the description."))
    records.extend(latest_records(OUTPUT.name))
    records.append(RevisionRecord("11 October 2026", "Errata", "English throughout the document", (
        "Historical Italian headings, captions and body copy were translated into English while preserving dates, original page and screen IDs, technical values and evidence limits.",
        "The original English correction on former page53 is consolidated here. The connection atlas, four-page A5 flyer and manga status atlas required no language correction in that revision.",), affected="Original pages1,2,9-14,16,21,22,39-46; former revision-register page53.", evidence="Document language and visual review only; no firmware or physical verification."))
    records.append(RevisionRecord("11 October 2026", "Change", "Restore the illustrated review workbook", (
        "Restore the cyan Cody artwork and separate writable NOTES areas from the02 October source027498aa. Preserve layout geometry, expression studies, debug and all256 palette slots. Replace white placeholder screens with coherent illustrated current routes.",
        "Standalone appendices from original pages23-46 and the47-53 revision pages are consolidated into this single final grid. Historical dates, decision substance, original page/screen references and evidence are retained. Security remains a clearly identified subsection of this register.",
        "Screens28-30 restore the one-way voice design: wake word, double tap or button starts a dedicated audio route; suspend character rendering, use a precomputed animation, stream over authenticated encrypted Wi-Fi and retain up to ten seconds of unacknowledged audio in PSRAM. Native Codex audio input remains research, not implemented firmware. No OTA or audio implementation is claimed.",
        "Screens33/34 now use short manga notices without percentages: low at20 and10 percent, charging entered once, full only on fresh measured100 percent. Admit after ten seconds of uninterrupted free Home; never interrupt a request or prolong standby. Continuing charging remains silent.",), affected="Original main pages1,2,5-16,21,22; screens21-35B and39-42; restored voice pages23-24.", evidence="PDF authoring and visual review. Source-based battery changes are tracked separately; no build, simulator or physical validation."))
    # Authoring copy uses normal spaces around numeric references, while dates,
    # stable IDs and compact technical units remain unchanged.
    from dataclasses import replace
    return [replace(record, paragraphs=tuple(re.sub(r"(?<=[a-z])(?=[0-9])", " ", text) for text in record.paragraphs),
                    affected=re.sub(r"(?<=[a-z])(?=[0-9])", " ", record.affected)) for record in records]

# page: Preserved boot page receiving the explicit saved-state branching diagram.
def boot_routes(page):
    stream = BytesIO()
    surface = canvas.Canvas(stream,pagesize=SIZE)
    surface.setFont("CodyDocBold",12)
    surface.setFillColor(CYAN)
    surface.drawString(350,454,"After local startup")
    branches = ((420,"Wi-Fi configured?","04 / Welcome"),
                (370,"Current LAN/IP valid?","42 / No connection"),
                (320,"Retained pairing?","40 / Connect Codex?"))
    for y,question,destination in branches:
        for x,width,label in ((350,190,question),(630,175,destination)):
            surface.setFillColor(colors.HexColor("#EDF5FA"))
            surface.setStrokeColor(LINE)
            surface.roundRect(x,y,width,28,7,fill=1,stroke=1)
            surface.setFillColor(INK)
            surface.setFont("CodyDoc",10)
            surface.drawCentredString(x+width/2,y+10,label)
        surface.setStrokeColor(CYAN)
        surface.line(540,y+14,630,y+14)
        surface.line(630,y+14,623,y+18)
        surface.line(630,y+14,623,y+10)
        surface.setFillColor(MUTED)
        surface.setFont("CodyDoc",9)
        surface.drawCentredString(583,y+20,"No")
        surface.line(445,y,445,y-22)
        surface.line(445,y-22,441,y-16)
        surface.line(445,y-22,449,y-16)
        surface.drawString(452,y-14,"Yes")
    surface.setFillColor(colors.HexColor("#EDF5FA"))
    surface.roundRect(350,270,190,28,7,fill=1,stroke=1)
    surface.setFillColor(INK)
    surface.setFont("CodyDoc",10)
    surface.drawCentredString(445,280,"14 / Find the retained PC")
    surface.setFillColor(CYAN)
    surface.setFont("Courier-Bold",9)
    surface.drawString(36,130,"NOTES")
    surface.setStrokeColor(LINE)
    for y in (115,96,77):
        surface.line(36,y,SIZE[0]-36,y)
    surface.save()
    overlay = pymupdf.open(stream=stream.getvalue(),filetype="pdf")
    page.show_pdf_page(page.rect,overlay,0)

# records: All dated historical/current records in the final grid.
# start_page: Absolute physical page number at the grid's start.
# security: Security records placed under a clearly labelled table section.
# security_only: Use compact technical typography for the final Security subsection.
# Raises ValueError if a revision category is unsupported.
def compact_register(records,start_page,security=(),security_only=False):
    stream = BytesIO()
    width,height = SIZE
    body = ParagraphStyle("CompactBody",fontName="CodyDoc",fontSize=8.4 if security_only else 9,
                          leading=10 if security_only else 12,textColor=INK,spaceAfter=2 if security_only else 4)
    heading = ParagraphStyle("CompactHeading",parent=body,fontName="CodyDocBold",
                             fontSize=9 if security_only else 10,leading=11 if security_only else 13)
    metadata = ParagraphStyle("CompactMeta",parent=body,fontSize=8,leading=10,textColor=MUTED)
    small = ParagraphStyle("CompactSmall",parent=body,fontSize=8,leading=10,textColor=MUTED)
    header = ParagraphStyle("CompactHeader",parent=body,fontName="CodyDocBold",fontSize=8,textColor=colors.white)
    rows = [[Paragraph("DATE / TYPE",header),Paragraph("REVISION DETAILS AND ORIGINAL REFERENCES",header)]]
    section_row = None
    for index,record in enumerate((*records,*security)):
        if index == len(records):
            section_row = len(rows)
            rows.append([Paragraph("SECURITY",header),Paragraph("Security / retained safeguards, evidence and open risks",header)])
        if record.kind not in ("Security","Errata","Addendum","Change"):
            raise ValueError("Unsupported revision type: " + record.kind)
        left = [Paragraph(escape(record.date),metadata),Paragraph(escape(record.kind.upper()),metadata)]
        right = [Paragraph(escape(record.title),heading)]
        if record.affected:
            right.append(Paragraph(escape("Affected: " + record.affected),small))
        right.extend(Paragraph(escape(text),body) for text in record.paragraphs)
        if record.evidence:
            right.append(Paragraph(escape("Evidence / limits: " + record.evidence),small))
        rows.append([left,right])
    # Keep each dated record intact, preventing orphaned titles, missing dates on
    # continuations and repeated column headers within one physical page.
    table = Table(rows,colWidths=[118,width-84-118],repeatRows=1,splitByRow=1,splitInRow=0)
    styles = [("BACKGROUND",(0,0),(-1,0),INK),("BACKGROUND",(0,1),(0,-1),colors.HexColor("#F1F6FA")),
              ("VALIGN",(0,0),(-1,-1),"TOP"),("GRID",(0,0),(-1,-1),0.5,LINE),
              ("LEFTPADDING",(0,0),(-1,-1),9),("RIGHTPADDING",(0,0),(-1,-1),9),
              ("TOPPADDING",(0,0),(-1,-1),7),("BOTTOMPADDING",(0,0),(-1,-1),7)]
    if section_row is not None:
        styles.extend((("BACKGROUND",(0,section_row),(-1,section_row),colors.HexColor("#80543C")),
                       ("NOSPLIT",(0,section_row),(-1,section_row+1))))
    table.setStyle(TableStyle(styles))
    # surface: ReportLab drawing owner for the page frame.
    # document: Pagination state supplying the physical page counter.
    def grid_frame(surface,document):
        surface.setFillColor(CYAN)
        surface.setFont("CodyDocBold",9)
        surface.drawString(42,height-30,"CODY / PLANNING ATLAS")
        surface.setFillColor(INK)
        surface.setFont("CodyDocBold",23)
        surface.drawString(42,height-59,"Revision register / Security" if security_only else "Revision register")
        surface.setFillColor(MUTED)
        surface.setFont("CodyDoc",9)
        surface.drawString(42,height-77,"Historical dates, original screen references and evidence are retained in this final grid.")
        surface.setStrokeColor(LINE)
        surface.line(42,35,width-42,35)
        surface.setFont("CodyDoc",8)
        surface.drawString(42,21,"Cody / confidential / source review and physical evidence are distinct")
        surface.drawRightString(width-42,21,str(start_page+document.page-1))
    document = SimpleDocTemplate(stream,pagesize=SIZE,leftMargin=42,rightMargin=42,topMargin=88,bottomMargin=45)
    document.build([table],onFirstPage=grid_frame,onLaterPages=grid_frame)
    return stream.getvalue()


# argv: Literal authoring arguments passed through the repository device CLI.
# Raises OSError for missing sources or failed PDF publication.
# Raises subprocess.CalledProcessError if the historical Git source is unavailable.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect", action="store_true")
    args = parser.parse_args(argv)
    STAGING.mkdir(parents=True, exist_ok=True)
    history = subprocess.run(["git", "show", "027498aa:docs/Cody/cody-planning-atlas-v1.pdf"],
                             cwd=ROOT, capture_output=True, check=True).stdout
    (STAGING / "historical.pdf").write_bytes(history)
    source = pymupdf.open(stream=history, filetype="pdf")
    baseline = STAGING / "protected-current.pdf"
    if not baseline.exists():
        # Both protected sources are immutable Git records, so regenerating in
        # a clean checkout cannot treat this revision as its own input history.
        baseline.write_bytes(subprocess.run(["git","show",
            "1d46253f:docs/cody/cody-planning-atlas-v1.pdf"],cwd=ROOT,
            capture_output=True,check=True).stdout)
    current = pymupdf.open(baseline)
    for label, document in (("historical", source), ("current", current)):
        (STAGING / (label + "-text.txt")).write_text(
            "\n\n".join(f"PAGE {index + 1}\n{page.get_text()}" for index, page in enumerate(document)), encoding="utf-8")
        for index in (0, 1, 4, 8, 13, 14, 15, 20):
            if index < len(document):
                document[index].get_pixmap(matrix=pymupdf.Matrix(0.8, 0.8)).save(STAGING / f"{label}-{index + 1:02}.png")
    print(f"Historical pages: {len(source)}; current pages: {len(current)}; authoring source: {STAGING}")
    if args.inspect:
        return
    register_fonts()
    artwork = sprites(source)
    result = pymupdf.open()
    result.insert_pdf(source, from_page=0, to_page=1)
    # Restore cover artwork while retiring the source's obsolete integration claim.
    for page, old, replacement in (
        (result[0], "Character rig, composable expressions and local UI. HTTP, PC", "Character, setup, companion status and battery notices."),
        (result[0], "integration and voice remain future design work.", "Voice and OTA are explicitly future implementation work."),
        (result[1], "Cody's color", "Cody's look"),
        (result[1], "Error details", "Retired"),
        (result[1], "Future connection, work, PR and voice screens are visual plans, not implemented integrations.", "Illustrated source contracts; voice28-30 remains planned. Setup39-42 and panels are detailed later."),
    ):
        for rectangle in page.search_for(old):
            fill = (0.937, 0.969, 0.980) if page.number == 0 else (1,1,1)
            page.add_redact_annot(rectangle, fill=fill)
            page.apply_redactions(images=0, graphics=0)
            page.insert_text((rectangle.x0, rectangle.y1 - 2), replacement, fontsize=9,
                             color=(0.32,0.44,0.51), fontname="helv")
    result.insert_pdf(current, from_page=2, to_page=3)
    boot_routes(result[3])
    pages = {
        5: ("Meet Cody and set up Wi-Fi", "Installation and Wi-Fi provisioning are separate from Codex association.", [
            ("04", "Welcome", "Hi, I'm Cody!", [], "Welcome opens the local network journey. A stationary hold opens Setup when no operation is busy.", 0),
            ("05", "Let's pair", "Let's pair\nInstall the pairing app", ["Show QR"], "Show QR opens06 without starting Bluetooth. Controls appear together after the main copy.", 1),
            ("06", "Install the app", "Scan to install\nQR in reserved area", ["Pair Cody"], "Use the real installation QR with its quiet zone. This illustration is not a scannable code. Pair Cody starts provisioning07.", 0)]),
        6: ("Finish network setup with real state", "A current valid Wi-Fi association and IP are required before Continue.", [
            ("07", "Pairing", "Pairing...\nFinish on your phone", ["Cancel"], "The session QR exists only while ReadyToScan. Retire it during credential transfer; Cancel waits for confirmed cleanup.", 1),
            ("08", "Try again", "Try again\nNetwork setup failed", ["Retry"], "Show the real failure. Retry returns to the appropriate network step; never substitute failure with online state.", 5),
            ("09", "You're online", "You're online.\nWi-Fi connected", ["Continue"], "Continue requires current valid Wi-Fi/IP. LAN association does not prove Internet access or Codex presence.", 2)]),
        7: ("Preferences and Cody's look", "10 ->36 ->11; Setup Options returns to its caller and preserves network/pairing.", [
            ("10", "Make it yours", "Shake / Rotate / Sounds\nOrientation: Auto or0/180", ["Next"], "Preferences persist only after successful storage. Sounds Off stops output; microphone and wake word continue.", 0),
            ("36", "Personalization", "Personalization\nChoose Cody's look", ["Continue"], "Continue opens the stable-ID skin selector. No implicit reset, silent correction or association change.", 1),
            ("11", "Cody's look", "Choose Cody's look\nPreview selected skin", ["Save"], "Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic, Dreamwave. Save failures preserve the cause and previous stored value.", 0)]),
        8: ("Pair Codex, then find the retained PC", "Association is durable; a live PC connection requires fresh authenticated proof.", [
            ("12", "Connect Codex", "Preparing secure code\nPlease wait", [], "About30 seconds is an estimate. Enter all six characters on the intended Cody; association follows both persisted sides.", 1),
            ("13", "You're all set", "You're all set.\nPairing saved", ["Let's go"], "Let's go immediately searches the retained PC. Successful association alone does not establish current activity.", 2),
            ("14 ->15", "Find the PC", "Looking for your PC...\nThen: Waiting for Codex", [], "Fresh authenticated presence admits Connected. Wait for a genuine prompt hook before following a chat.", 0)]),
        9: ("Cody follows the work you submit", "16/17/18/18U; identity comes from genuine prompt hooks, not tab clicks.", [
            ("16 /17", "Home and context", "Here with you.\nContext from Codex", [], "Home breathes and blinks. The latest genuine prompt selects context; project labels are display text, not identities.", 0),
            ("18", "Working", "Working in Codex\nReading / Editing / Running", [], "Tool hooks update activity for the followed chat. Events from background chats cannot steal context.", 3),
            ("18U", "Unavailable", "No recent activity\nCheck on your PC", [], "After120 seconds without a new hook, current work is unavailable. An authenticated PC heartbeat does not prove ongoing work.", 5)]),
        10: ("Reply, pause and return to rest", "Response completion, task success and link health remain separate facts.", [
            ("19", "Response ready", "Response ready\nRead it in Codex", [], "An attention cue invites reading on the PC. It does not certify tests, publication or task success.", 2),
            ("20A", "Paused", "Paused\nContinue in Codex", [], "Preserve the followed context and any open question. Routine status changes never replace a pending decision.", 7),
            ("20B ->16", "Ended", "Session ended\nWaiting for Codex", [], "The character settles back to Home. A broker restart waits for a new genuine prompt rather than inventing context.", 6)]),
        11: ("Work status can include PR observations", "21-24 are status variants; Cody does not manage pull requests or native approvals.", [
            ("21", "PR pending", "PR pending", [], "An actual observation can produce a concise notice in the companion workflow. No separate PR management application.", 1),
            ("22", "Checks running", "Checks running", [], "Only show reported status and measured counts when supplied. Missing data never becomes invented progress.", 3),
            ("23 ->24", "Checks failed", "Checks failed\nRead failures", ["View failures"], "Existing View failures shows real names only, then returns to context. Logs, diagnosis and fixes remain on the PC;25 is retired.", 5)]),
        12: ("A question deserves its own attention", "26/27; permission requests summon attention, while native approval remains on the PC.", [
            ("26", "Codex needs you", "From Codex\nCodex needs you", ["Open"], "An explicit MCP caller supplies English ASCII text. Caller-declared context does not prove a verified hook identity.", 1),
            ("27", "Keep the question", "Choose a reply\nRead before answering", [], "An open question is protected for180 seconds. Battery, charging, routine notices and later activity cannot hide it.", 0),
            ("PC", "Native approval", "Approval needed\nOn your PC", [], "Cody may request attention. It never provides generic Allow/Deny based on incomplete text or replaces native approval.", 3)]),
        13: ("Choose, review, then explicitly Send", "Two side-by-side cards; add a centered third card when needed. Stable choice IDs survive reordering.", [
            ("27", "Two choices", "Choose a reply", ["Memory", "Redis"], "Selecting a card opens its description; it never sends. Complete controls appear after title/body narration.", 1),
            ("27", "Three choices", "Choose a reply", ["Memory", "Redis", "None"], "The third choice sits below the pair. Keep lateral Back separate and preserve the circle's available chord.", 0),
            ("27", "Review choice", "Redis\nShared external cache", ["Send"], "Read the selected description and press Send. Back returns to choices without silently changing the choice identity.", 0)]),
        14: ("A receipt is not confirmed delivery", "31; received, delivered, cancellation and expiry are separate outcomes.", [
            ("27", "Sending", "Sending...\nWaiting for receipt", [], "Send fixes the selected answer. Matching repeated answers are idempotent; a different answer cannot replace it.", 1),
            ("31", "Reply sent", "Reply sent\nBack to your project", [], "Show only after the intended MCP caller acknowledges the matching answer_id. Admission or PC receipt alone is insufficient.", 2),
            ("31", "Unconfirmed", "Reply unconfirmed\nCheck on your PC", ["Done"], "At expiry after Send, only the acknowledgement may be missing. Before Send use Request expired. Terminal requests never reopen.", 5)]),
        15: ("Battery notices feel like Cody", "Admit after10 seconds of uninterrupted free Home; show manga and caption together for4 seconds.", [
            ("33", "Low battery", "A little tired.\nCharge me?", [], "At20% use this notice; at10%: Please charge me soon. Remind after5 minutes /1 minute respectively. No percentage in the caption.", 7, "low-battery"),
            ("34", "Charging starts", "Thanks!\nRecharging.", [], "One notice on actual entry into charging. Ongoing charging is silent. No connected-to-power inference and no numeric overlay.", 0, "charging"),
            ("34", "Fully charged", "All charged.\nHappy!", [], "One Joy notice only at fresh measured100%. Canonical valid battery data only; notices never interrupt work/questions or extend standby.", 2, "joy")]),
        16: ("Sleep, wake and restore the route", "35A/35B and32; sleeping is presentation, not cancellation of a pending question.", [
            ("35A", "Sleep", "", [], "Close the eyes with a slow breath. Battery notices do not delay standby. Retain Wi-Fi on paired Cody and preserve AFE/wake word.", 6, "sleep"),
            ("35B", "Wake", "Welcome back", [], "Restore the prior route and selection. A touch held across wake starts its1.5-second Setup hold only after a fresh visible acquired sample.", 0),
            ("32", "Link lost", "Connection lost\nLooking for your PC...", [], "Preserve context while searching for the retained PC. Question expiry continues; disconnect and timeout never choose an answer.", 5)]),
        21: ("Setup keeps intent and routes explicit", "39-42; local Wi-Fi setup and Codex association have independent owners and purposes.", [
            ("41", "Setup", "Setup", ["Codex", "Network", "Options"], "Hold the visible character still for1.5 seconds; release before selecting. Keep valid previous-route lateral Back separate.", 0),
            ("40", "Retained pairing", "Already paired", ["Keep", "Pair again", "Forget"], "Keep searches the saved PC; Pair again explicitly admits replacement. Forget requires a separate confirmation and preserves unrelated settings.", 1),
            ("42 /39", "Network and handoff", "No connection\nNetwork setup or Later", ["Network", "Later"], "No valid association/IP ->42. Debug handoff39 requires a fresh tap; Release continues to09. No public Internet requirement.", 5)]),
        22: ("Technical panels, then back to Cody", "Passive panel taps cycle; a deliberate shake returns directly to Home. These panels have no Back.", [
            ("PANEL", "Activity", "Activity\nLatest actual work", [], "A short passive tap advances to Model & Context. Retain real validity, timestamps and failure state; no fabricated metrics.", 3),
            ("PANEL", "Model & Context", "Model & Context\nAvailable reported data", [], "A short passive tap advances to Session & Link. Model and context are reported fields, not inferred from the visible character.", 1),
            ("PANEL", "Session & Link", "Session & Link\nCurrent link health", [], "A short passive tap returns to Activity. Deliberate shake returns Home. Setup and remote questions retain their own routes.", 0)]),
        23: ("Planned voice: wake, capture, send", "28/29; one-way voice to Codex is a design proposal, not implemented firmware or a verified service.", [
            ("28", "Start voice", "Listening\nSpeak to Cody", [], "Wake word, double tap or button requests the same priority event. Do not start a second capture. Explicit request/approval behavior needs design before implementation.", 1,"voice"),
            ("28", "Capture to Wi-Fi", "Recording...\nSending to your PC", [], "Suspend character rendering. Use a small precomputed animation; this static glyph is illustrative, not a measured waveform.", 3,"voice"),
            ("29", "Processing", "Processing audio\nOn your PC", ["Cancel"], "A dedicated authenticated encrypted channel streams canonical audio acquisition to the PC. Native Codex audio support requires installed-version/account verification.", 1,"voice")]),
        24: ("Planned voice: bounded buffer and outcomes", "30/31; ten seconds is a transport-stall budget, not the intended maximum utterance duration.", [
            ("28", "PC slows down", "Waiting for your PC\nAudio buffered", [], "Proposed PSRAM queue: up to10 seconds of unacknowledged PCM16 mono16kHz,320000 bytes plus metadata. Stop explicitly at capacity; never overwrite unheard words.", 4,"voice"),
            ("30", "Review transcript", "Review transcript\nReady to send", ["Send"], "Historical review screen30 is retained as an optional future mode. Initial wake-word dictation may use automatic end-of-speech delivery; no firmware behavior is claimed.", 0),
            ("31", "Delivered or failed", "Sent to Codex\nOr: audio delivery failed", [], "Received by the PC, accepted by Codex and work completed are distinct. No full duplex, SD writes or flash recording required by this proposal.", 2)]),
    }
    for number in range(5, 25):
        if number in (17, 18, 19, 20):
            result.insert_pdf(current, from_page=number - 1, to_page=number - 1)
        else:
            title, subtitle, items = pages[number]
            illustrated = screen_page(number, title, subtitle, items, artwork)
            result.insert_pdf(illustrated)
            illustrated.close()
    records = history_records(current)
    register = pymupdf.open(stream=compact_register(records,len(result)+1),filetype="pdf")
    result.insert_pdf(register)
    security = pymupdf.open(stream=compact_register(security_records(),len(result)+1,security_only=True),filetype="pdf")
    result.insert_pdf(security)
    result.set_metadata({"title": "Cody / Planning Atlas", "author": "Umberto Giacobbi",
                         "subject": "Illustrated current routes and consolidated dated revision register"})
    temporary = STAGING / "revised.pdf"
    result.save(temporary, garbage=4, deflate=True)
    result.close()
    OUTPUT.write_bytes(temporary.read_bytes())
    final = pymupdf.open(OUTPUT)
    rendered = STAGING / "final-render"
    rendered.mkdir(exist_ok=True)
    # Remove only this generator's previous page/contact PNGs, so a shorter
    # revision cannot leave stale pages looking like part of the latest PDF.
    for pattern in ("page-*.png","contact-*.png"):
        for old_render in rendered.glob(pattern):
            old_render.unlink()
    thumbs = []
    for index, page in enumerate(final):
        pixmap = page.get_pixmap(matrix=pymupdf.Matrix(1.15, 1.15))
        pixmap.save(rendered / f"page-{index + 1:02}.png")
        image = Image.frombytes("RGB", [pixmap.width, pixmap.height], pixmap.samples)
        image.thumbnail((421,298))
        thumbs.append(image)
    for group in range((len(thumbs) + 5) // 6):
        sheet = Image.new("RGB", (1263,596), "#c7d1db")
        for offset, thumb in enumerate(thumbs[group * 6:group * 6 + 6]):
            sheet.paste(thumb, ((offset % 3) * 421,(offset // 3) * 298))
        sheet.save(rendered / f"contact-{group + 1:02}.png")
    (STAGING / "receipt.json").write_text(json.dumps({"historical_commit": "027498aa", "main_pages":24,
        "total_pages":len(final), "records":len(records), "security_records":len(security_records()),
        "evidence":"PDF authoring only; firmware validation deferred"}, indent=2), encoding="utf-8")
    print(f"Restored {OUTPUT}: 24 illustrated/source main pages; {len(final)} total. Full render: {rendered}")


if __name__ == "__main__":
    main()
