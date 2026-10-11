"""Re-author the navigation atlas with illustrated routes and one final history grid."""

from io import BytesIO
from pathlib import Path
import re
import subprocess
import sys

import pymupdf
from PIL import Image, ImageDraw, ImageChops
from reportlab.lib import colors
from reportlab.lib.pagesizes import landscape, A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader
from reportlab.platypus import Paragraph, SimpleDocTemplate, Table, TableStyle, Spacer
from reportlab.graphics.barcode.qr import QrCodeWidget
from reportlab.graphics.shapes import Drawing
from reportlab.graphics import renderPDF

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, register_fonts, plain_paragraph, ACCENTS
from generate_cody_security_atlases import latest_records, security_records

DESTINATION = ROOT / "docs/cody/cody-navigation-atlas-v1.pdf"
STAGING = ROOT / "artifacts/docs/cody-navigation-revision-20261011"
WIDTH, HEIGHT = landscape(A4)
CYAN = colors.HexColor("#229EEA")
INK = colors.HexColor("#142938")
MUTED = colors.HexColor("#536C7B")
LINE = colors.HexColor("#CBDCE6")
SPRITES = []

# Route IDs remain stable, independently of the document's physical pages.
PAGES = [
    ("Cody / Navigation atlas", "A continuous journey, with current routes and clearly marked future voice input.",
     [("04", "Welcome", "Let's begin", "Setup"), ("16", "Home", "Here with you", ""), ("26", "Codex needs you", "Read before replying", "Open")],
     "First setup -> connected companion -> an explicit question. Screen IDs identify routes; page numbers identify this printout.",
     "Cyan Cody is the common visual reference. Schematics are editorial illustrations, not device captures. Current contracts: docs/cody/navigation.md and runtime.md. Historical decisions are retained only in the final revision register."),
    ("Boot selects the right journey", "Branding precedes screens 01-03; persisted state and real network observations select the route.",
     [("04-09", "First setup", "No Wi-Fi profile", "Network setup"), ("42", "No connection", "Profile saved; no IP", "Network setup"), ("14", "Looking for PC", "PC remembered", "")],
     "Absent Wi-Fi -> 04; association/IP unavailable -> 42; LAN valid and unpaired -> 40; retained pairing -> 14.",
     "VibeWare floppy animation and Cody identity precede route selection. Storage errors remain visible errors; they are never treated as missing settings. LAN access and Internet access are separate observations."),
    ("Wi-Fi setup / install, then pair", "04 -> 05 -> 06 -> 07 -> 09. Installation and provisioning use different QR payloads.",
     [("05", "Let's pair", "Install the pairing app", "Show QR"), ("06", "Install app", "Installation QR", "Pair Cody"), ("07", "Scan to pair", "Session QR when ready", "Cancel")],
     "Show QR opens 06 without starting radios. Pair Cody starts BLE; only ReadyToScan reveals the session QR in 07.",
     "Credentials received remove the session QR. Cancel waits for confirmed cleanup before returning to 06. Retry resumes the failed owner stage; it never silently restarts another attempt. Historical 06-bis is now a state of screen 07."),
    ("Wi-Fi handoff and personalization", "Successful connection and persistence precede confirmation. Debug handoff has its own fresh tap.",
     [("09", "You're online", "Wi-Fi and IP valid", "Continue"), ("10", "Make it yours", "Preferences", "Next"), ("11", "Cody's look", "Select a stable skin ID", "Done")],
     "09 Continue -> 10 Next -> 36 Continue -> 11; Done returns to its caller and evaluates the saved Codex link.",
     "Debug uses 39 before 09; Release proceeds directly. Screen 09 admits Continue only with valid current Wi-Fi/IP. Four preference rows and Desk/Away usage preserve real storage results. First setup requires an explicit skin selection; reusable settings retain their caller."),
    ("Connect Codex / a separate decision", "40 -> 12 -> 13 -> 14 -> 15. Local pairing is independent of the followed chat.",
     [("40", "Connect Codex?", "Choose explicitly", "Yes / No"), ("12", "Pair Codex", "ABC123 (example)", "Cancel"), ("13", "You're all set", "Pairing persisted", "Let's go")],
     "Yes -> code preparation -> confirmed handshake -> Let's go -> immediate retained-PC search. No keeps the local companion.",
     "Use all six characters only on the intended device. The preparation estimate is not measured progress. Backend pairing lasts five minutes; code-screen inactivity is 180 seconds. All set requires two-sided persisted confirmation, not merely a packet ACK."),
    ("Find the PC, then follow real work", "14 -> 15 -> activity variants 17-20. Pairing, presence and actual chat activity remain distinct.",
     [("14", "Looking for PC", "Saved link retained", ""), ("15", "Connected", "Start a chat in Codex", ""), ("17-20", "Working with you", "Genuine prompt context", "")],
     "Authenticated current presence -> Connected. A genuine followed prompt -> activity; response ready and chat ended are distinct.",
     "The latest authorized UserPromptSubmit establishes exact chat and turn identities. Background events and opening a tab do not select a chat. Broker restart requires another real prompt. Project names are labels, and unmeasured metrics remain unavailable."),
    ("Home and companion information", "A passive tap cycles the technical panels; a deliberate shake returns directly to Home.",
     [("TECH", "Activity", "Current followed work", ""), ("TECH", "Model & Context", "Measured context only", ""), ("TECH", "Session & Link", "Next tap: Activity", "")],
     "Home -> Activity -> Model & Context -> Session & Link -> Activity. Shake -> Home. Technical panels have no Back.",
     "Short passive taps are distinct from the 1.5-second stationary hold that opens Setup. Idle scenes compose on the current route; they do not create new screens or prolong standby. Canonical snapshots are the only runtime measurement source."),
    ("Reopen Setup without replacing the PC", "Hold visible Cody still for 1.5 seconds, then release before selecting an action.",
     [("41", "Setup", "Reopen preferences", "Codex / Network / Options"), ("RETAINED", "PC remembered", "Keep or replace", "Keep / Pair again / Forget"), ("FORGET", "Forget pairing?", "Only Codex is reset", "Go back / Forget")],
     "Setup -> Connect Codex, Network setup or Options. Keep link reuses the saved PC; Pair again explicitly authorizes replacement.",
     "Forget needs a second confirmation and removes only Cody's CCP records. Wi-Fi, preferences and logs remain; the PC record is not remotely revoked. Pending candidates and storage errors keep their real identity and cause. Back restores the valid caller."),
    ("Read a question, then choose", "26 -> 27 -> review 27. A choice is reversible until Send; native approvals stay on the PC.",
     [("26", "Codex needs you", "Declared question context", "Open"), ("27", "Choose a reply", "Stable choice IDs", "Choice A / B"), ("27", "Review choice", "Read the full meaning", "Send")],
     "Open -> two or three reply cards -> review -> Send. Back from review returns to choices; Back from choices returns to 26.",
     "Two actions use side-by-side cards; three add a centered card below. One action uses an icon-left capsule. Controls reveal together after narration, without teletype captions. An open question keeps priority over routine updates and battery notices."),
    ("Delivery, offline and real outcomes", "Send -> received -> recipient delivered -> 31. A received ACK is not final delivery.",
     [("WAIT", "Sending reply", "Confirming delivery", ""), ("31", "Reply sent", "Delivered to Codex", "Done"), ("32", "Offline", "Keep the question", "")],
     "Recipient ACK -> Reply sent -> previous context. Before Send: Request expired. After Send: Reply unconfirmed / Check on your PC.",
     "The 180-second deadline includes delivery acknowledgement. Reconnecting does not renew it. Cancel, expiry and offline never choose an answer. Terminal states cannot reopen on late ACKs. A notice has no choices and produces no reply."),
    ("PR status belongs to ordinary activity", "21-24 are information variants of the followed work; no separate PR-management application.",
     [("21", "PR pending", "Current explicit snapshot", ""), ("22", "Checks running", "Same PR head", ""), ("23", "Checks failed", "Real failed check names", "View failures")],
     "Active work -> PR pending -> checks running -> passed or failed. View failures -> 24; Back -> 23. Screen 25 is retired.",
     "The 04 October A03 decision limits device interaction to status and failure names. Counts require a complete current list for the same head. No merge, approve, close, rerun or View log route. This page explains navigation, rather than adding another work mode."),
    ("Voice input / retained planned journey", "28-30 are planned, unimplemented audio routes. The Wi-Fi input discussion remains visible.",
     [("28", "Listening", "Wake word / tap / button", "Stop / Cancel"), ("29", "Sending audio", "PC receives and processes", "Cancel"), ("30", "Review transcript", "Read before sending", "Send")],
     "Trigger -> dedicated listening view -> one-way audio to PC -> processing -> review. Question replies retain recipient delivery rules.",
     "Planned architecture: suspend character rendering, show a small precomputed listening animation, stream authenticated audio over a dedicated Wi-Fi channel and retain up to ten seconds of unacknowledged PCM in PSRAM. Codex native audio compatibility remains to be verified; no audio implementation is claimed."),
    ("Battery notices at a quiet moment", "33 / 34 are expressive notices, not navigation destinations or persistent numeric banners.",
     [("33", "Battery low", "Please charge me soon.", ""), ("34", "Charging", "Thanks! Recharging.", ""), ("34", "Fully charged", "All charged. Happy!", "")],
     "Queue low at 20%, critical at 10%, charging-start and full events. Present only when Home is eligible; retain the current route.",
     "After ten quiet Home seconds, show a four-second notice without extending standby. Charging entry and full charge notify once; continuous charging stays silent. Low reminders repeat after five minutes at 20%, one minute at 10%. Use manga symbols without numeric captions; never interrupt a question or setup."),
    ("Standby and recovery keep their owner", "35A / 35B and 32 preserve saved context. Sleep is never an automatic reply or reset.",
     [("35A", "Going to sleep", "Quiet companion", ""), ("35B", "Sleeping", "Wake route preserved", ""), ("32", "PC unavailable", "Reconnect saved link", "")],
     "Ten eligible quiet Home seconds admit a brief notice; the existing standby deadline is unchanged. Wake restores the route.",
     "Paired standby retains Wi-Fi and the existing wake-word owner. Restart retains data; an explicitly confirmed factory reset removes settings, Wi-Fi and pairing while retaining firmware, assets and SD. Acceptance ACK and observed reboot are different evidence."),
    ("Character debug and preview boundaries", "37 / 38 inspect the rig; simulator data is visibly synthetic and never becomes a firmware sample.",
     [("37", "Rig view", "Anchors and controls", ""), ("38", "Expression channels", "Synthetic preview", ""), ("16", "Home", "One composed pose", "")],
     "Development entry -> rig / channels -> composed preview -> return to caller. Breathing and expression are pose updates, not routes.",
     "Energy, drowsiness, alertness and tension describe expressive channels. Canonical acquisition owns sensors; UI and animations do not read hardware directly. Windows audio and device capture have separate scopes. These schematics do not prove physical behavior."),
]


# value: Historical PDF text, normalized to printable English typography.
def ascii_text(value):
    replacements = {"\u2010": "-", "\u2011": "-", "\u2013": "-", "\u2014": "-", "\u2019": "'", "\u2018": "'", "\u201c": '"', "\u201d": '"', "\u2026": "...", "\u2192": "->", "\u2022": "*"}
    for old, new in replacements.items():
        value = value.replace(old, new)
    value = value.replace("buco di analisi iniziale", "initial analysis gap")
    return value


# surface: PDF canvas receiving the static character schematic.
# x: Center of the character.
# y: Center of the character.
# expression: Index of the historical approved cyan expression sprite.
def cody(surface, x, y, expression):
    native_width, native_height = SPRITES[expression].getSize()
    height = 120 * native_height / native_width
    surface.drawImage(SPRITES[expression], x - 60, y - height / 2, width=120, height=height, mask="auto")


# surface: PDF drawing surface.
# text: Plain explanatory copy.
# x: Left coordinate.
# y: Top coordinate.
# width: Available paragraph width.
# size: Font size in points.
# Raises ValueError if prose exceeds its dedicated layout band.
def paragraph(surface, text, x, y, width, size=10):
    style = ParagraphStyle("Atlas", fontName="CodyDoc", fontSize=size, leading=size * 1.42, textColor=MUTED)
    item = Paragraph(text.replace("&", "&amp;"), style)
    _, height = item.wrap(width, 140)
    if height > 115:
        raise ValueError("Atlas prose is too long: " + text[:80])
    item.drawOn(surface, x, y - height)


# surface: PDF drawing surface.
# item: Stable screen ID, title, body and action illustration.
# x: Left coordinate of its column.
def screen(surface, item, x):
    identity, title, body, actions = item
    center = x + 112
    surface.setFillColor(CYAN)
    surface.setFont("CodyDocBold", 11)
    surface.drawString(x, 450, identity + " / " + title)
    surface.setFillColor(colors.black)
    surface.setStrokeColor(MUTED)
    surface.circle(center, 333, 98, stroke=1, fill=1)
    expression = 6 if identity in ("35A", "35B") else 7 if identity == "33" else 5 if identity in ("23", "32", "42") else 2 if title in ("Fully charged", "You're all set", "You're online", "Reply sent") else 1 if identity in ("26", "27", "28", "29", "30") else 0
    if identity in ("28", "29"):
        # Planned low-cost listening artwork replaces Cody during audio capture.
        surface.setFillColor(CYAN)
        surface.roundRect(center - 9, 353, 18, 32, 9, stroke=0, fill=1)
        surface.setStrokeColor(CYAN)
        surface.setLineWidth(2)
        surface.arc(center - 15, 348, center + 15, 380, startAng=180, extent=180)
        surface.line(center, 348, center, 339)
        surface.line(center - 10, 339, center + 10, 339)
        for offset, height in ((-32, 13), (-23, 20), (23, 20), (32, 13)):
            surface.line(center + offset, 366 - height / 2, center + offset, 366 + height / 2)
    elif identity in ("06", "07"):
        # The QR is deliberately unusable for commissioning; preserve its quiet zone.
        qr = QrCodeWidget("https://example.invalid/illustrative-only")
        bounds = qr.getBounds()
        scale = 76 / (bounds[2] - bounds[0])
        drawing = Drawing(76, 76, transform=[scale, 0, 0, scale, 0, 0])
        drawing.add(qr)
        surface.setFillColor(colors.white)
        surface.rect(center - 38, 328, 76, 76, stroke=0, fill=1)
        renderPDF.draw(drawing, surface, center - 38, 328)
    else:
        cody(surface, center, 365, expression)
    if identity in ("33", "34"):
        cue = "low-battery" if identity == "33" else "joy" if title == "Fully charged" else "charging"
        path = ROOT / "assets/cody/personality-icons/generated" / (cue + ".png")
        surface.drawImage(ImageReader(str(path)), center + 36, 383, width=24, height=24, mask="auto")
    surface.setFillColor(colors.HexColor("#ADC7D5"))
    surface.setFont("CodyDoc", 10)
    surface.drawCentredString(center, 305, body)
    if actions:
        labels = actions.split(" / ")
        card_width = 72 if len(labels) > 1 else 132
        for index, label in enumerate(labels):
            left = center - 36 if index == 2 else center - 78 + index * 84 if len(labels) > 1 else center - 66
            bottom = 247 if index == 2 else 278 if len(labels) == 3 else 266
            palette = ("#5B263D", "#8A3A50") if label in ("Cancel", "Forget", "No") else ("#14463B", "#25725D") if label in ("Send", "Yes", "Keep", "Choice A") else ("#143348", "#235B73")
            surface.saveState()
            boundary = surface.beginPath()
            boundary.roundRect(left, bottom, card_width, 25, 11)
            surface.clipPath(boundary, stroke=0, fill=0)
            surface.linearGradient(left, bottom, left + card_width, bottom + 25, [colors.HexColor(palette[0]), colors.HexColor(palette[1])])
            surface.restoreState()
            surface.setFillColor(colors.HexColor("#DAEFF8"))
            surface.setStrokeColor(colors.HexColor("#DAEFF8"))
            surface.setLineWidth(1)
            if label in ("Cancel", "Forget", "No"):
                surface.line(left + 8, bottom + 9, left + 14, bottom + 15)
                surface.line(left + 8, bottom + 15, left + 14, bottom + 9)
            else:
                surface.line(left + 8, bottom + 12, left + 14, bottom + 12)
                surface.line(left + 11, bottom + 9, left + 14, bottom + 12)
                surface.line(left + 11, bottom + 15, left + 14, bottom + 12)
            surface.setFont("CodyDocBold", 9)
            surface.drawCentredString(left + card_width / 2 + 6, bottom + 8, label)
    if identity in ("06", "10", "27", "FORGET", "RETAINED"):
        surface.setStrokeColor(colors.HexColor("#9FBDD0"))
        surface.setLineWidth(1.5)
        surface.line(center - 78, 337, center - 82, 333)
        surface.line(center - 82, 333, center - 78, 329)
    surface.setFillColor(CYAN)
    surface.setFont("CodyDocBold", 9)
    surface.drawString(x, 218, "NOTES")
    surface.setStrokeColor(LINE)
    surface.setLineWidth(0.6)
    for y in (201, 183, 165):
        surface.line(x, y, x + 225, y)


# original: Original committed navigation PDF, before this re-authoring.
# Returns all historical records with original dates, page IDs and substantive text.
def history_records(original):
    records = []
    for index in range(12, 46):
        text = ascii_text(original[index].get_text())
        date = "03 October 2026" if index == 12 else "04 October 2026" if index <= 15 else "05 October 2026" if index <= 18 else "06 October 2026" if index <= 25 else "09 October 2026" if index <= 33 else "10 October 2026"
        kind = "Errata" if "Errata and corrections" in text else "Addendum" if "Addendum" in text or "ADDENDUM" in text else "Change"
        lines = [line.strip() for line in text.splitlines() if line.strip()]
        # Strip running furniture only; route labels and diagram text remain evidence.
        content = [line for line in lines if not line.startswith(("CODY /", "Copyright ", "Umberto Giacobbi |")) and line != str(index + 1)]
        title = next((line for line in content if len(line) > 15 and "2026" not in line), "Historical route decision")
        chunks = []
        for block in original[index].get_text("blocks"):
            lines = [ascii_text(line.strip()) for line in block[4].splitlines() if line.strip()]
            lines = [line for line in lines if not line.startswith(("CODY /", "Copyright ", "Umberto Giacobbi |")) and line != str(index + 1)]
            if not lines:
                continue
            diagram = len(lines) > 2 and sum(len(line) < 35 for line in lines) > len(lines) / 2
            chunks.append(("Original diagram labels: " + " / ".join(lines)) if diagram else " ".join(lines))
        # Keep related source blocks together so short diagram labels do not
        # turn a historical decision into dozens of isolated table paragraphs.
        grouped = tuple(" ".join(chunks[start:start + 4]) for start in range(0, len(chunks), 4))
        records.append(RevisionRecord(date, kind, title, grouped, affected=f"Original navigation page {index + 1}; original screen IDs retained below.", evidence="Historical decision; later current-flow corrections supersede earlier routes. Original diagram labels retained as text."))
    records.extend(latest_records(DESTINATION.name))
    records.append(RevisionRecord("11 October 2026", "Errata", "English throughout the document", ("Historical Italian text, headings, diagram captions and footers translated into English; original dates, screen IDs, technical values, evidence limits and artwork retained at that revision.", "That language review also covered the English consolidated connection atlas, the four-page A5 pairing flyer and the manga status atlas; those documents required no language correction."), affected="Original pages 1, 5-10, 29, 31, 34, 36, 38-46; previous final page 53.", evidence="Document language and visual review only; no firmware build, simulator or physical-device verification."))
    records.append(RevisionRecord("11 October 2026", "Change", "Continuous illustrated navigation and consolidated history", ("Main routes are rebuilt as illustrated flows with the common cyan Cody and writable NOTES. Restore planned audio screens 28-30, distinguish ordinary PR activity from a separate application, and integrate quiet battery notices after ten eligible Home seconds.", "All previous Addendum, Change and Errata pages are consolidated here with their original dates, page references and substantive text. Security remains a clearly labelled subsection of this final register."), affected="Original main pages 1-12; historical pages 13-53; screens 01-42, 28-30, 33-35.", evidence="Editorial schematics and source review. No test, build, simulator or physical verification."))
    return records


# records: Dated entries retaining original page/screen IDs and substantive details.
# start_page: Absolute page number for the first page of this final section.
# section_title: Register heading or its clearly labelled Security subsection.
# Raises ValueError for an unsupported intervention type or an oversized paragraph.
def compact_register(records, start_page, section_title="Revision register"):
    available = WIDTH - 84
    meta_width = 118
    cell_width = available - meta_width - 18
    security = section_title == "Security"
    body = ParagraphStyle("CompactBody", fontName="CodyDoc", fontSize=8.4 if security else 9, leading=10 if security else 12, textColor=INK, spaceAfter=2 if security else 4)
    title = ParagraphStyle("CompactTitle", parent=body, fontName="CodyDocBold", fontSize=9 if security else 10, leading=11 if security else 13)
    small = ParagraphStyle("CompactSmall", parent=body, fontSize=8, leading=10, textColor=MUTED)
    header = ParagraphStyle("CompactHeader", parent=body, fontName="CodyDocBold", fontSize=8, textColor=colors.white)
    rows = [[plain_paragraph("DATE / TYPE", header), plain_paragraph("DESCRIPTION / ORIGINAL PAGE AND SCREEN IDS", header)]]
    for record in records:
        if record.kind not in ACCENTS:
            raise ValueError("Unknown revision type: " + record.kind)
        meta = ParagraphStyle("CompactMeta", parent=small, fontName="CodyDocBold", textColor=colors.HexColor(ACCENTS[record.kind]))
        left = [plain_paragraph(record.date, small), Spacer(1, 6), plain_paragraph(record.kind.upper(), meta)]
        prefix = [plain_paragraph(record.title, title), plain_paragraph("Affected: " + record.affected, small)]
        parts = [plain_paragraph(text, body) for text in record.paragraphs]
        if record.evidence and not record.evidence.startswith("Historical decision;"):
            parts.append(plain_paragraph("Evidence / limits: " + record.evidence, small))
        chunk = list(prefix)
        used = sum(part.wrap(cell_width, 330)[1] + part.getSpaceAfter() for part in chunk)
        for part in parts:
            height = part.wrap(cell_width, 330)[1] + part.getSpaceAfter()
            if height > 330:
                raise ValueError("Historical paragraph exceeds a register row: " + record.title)
            if used + height > 360 and len(chunk) > len(prefix):
                rows.append([left, chunk])
                chunk = [plain_paragraph(record.title + " (continued)", title), plain_paragraph("Affected: " + record.affected, small)]
                used = sum(item.wrap(cell_width, 330)[1] + item.getSpaceAfter() for item in chunk)
            chunk.append(part)
            used += height
        rows.append([left, chunk])
    table = Table(rows, colWidths=[meta_width, available - meta_width], repeatRows=1, splitByRow=1)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), INK), ("BACKGROUND", (0, 1), (0, -1), colors.HexColor("#F4F7FB")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"), ("GRID", (0, 0), (-1, -1), 0.5, LINE),
        ("LEFTPADDING", (0, 0), (-1, -1), 9), ("RIGHTPADDING", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 8), ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
    ]))
    output = BytesIO()
    document = SimpleDocTemplate(output, pagesize=(WIDTH, HEIGHT), leftMargin=42, rightMargin=42, topMargin=88, bottomMargin=45)

    # surface: Canvas for the currently paginated register page.
    # state: Platypus page state providing the absolute footer page number.
    def frame(surface, state):
        surface.setFillColor(CYAN)
        surface.setFont("CodyDocBold", 9)
        surface.drawString(42, HEIGHT - 29, "CODY / NAVIGATION ATLAS / FINAL REVISION REGISTER")
        surface.setFillColor(INK)
        surface.setFont("CodyDocBold", 23)
        surface.drawString(42, HEIGHT - 59, section_title)
        surface.setFillColor(MUTED)
        surface.setFont("CodyDoc", 8)
        surface.drawString(42, HEIGHT - 75, "Historical dates, original IDs and scope retained. Current illustrated flow precedes this register.")
        surface.setStrokeColor(LINE)
        surface.line(42, 33, WIDTH - 42, 33)
        surface.drawString(42, 21, "Cody / confidential / document review does not verify firmware or physical behavior")
        surface.drawRightString(WIDTH - 42, 21, str(start_page + state.page - 1))

    document.build([table], onFirstPage=frame, onLaterPages=frame)
    return output.getvalue()


# Raises OSError for missing repository history, output paths or fonts.
# Raises subprocess.CalledProcessError if the protected historical PDF cannot be read.
# Raises ValueError if an authored paragraph or register row cannot fit.
def main():
    STAGING.mkdir(parents=True, exist_ok=True)
    baseline = subprocess.run(["git", "show", "1d46253f:docs/cody/cody-navigation-atlas-v1.pdf"], cwd=ROOT, check=True, capture_output=True).stdout
    original = pymupdf.open(stream=baseline, filetype="pdf")
    sheet = Image.open(BytesIO(original.extract_image(original[10].get_images()[0][0])["image"])).convert("RGBA")
    for index in range(8):
        column, row = index % 4, index // 4
        tile = sheet.crop((column * sheet.width // 4, row * sheet.height // 2, (column + 1) * sheet.width // 4, (row + 1) * sheet.height // 2))
        # Remove only the exterior black matte; keep black eyes and mouth intact.
        for corner in ((0, 0), (tile.width - 1, 0), (0, tile.height - 1), (tile.width - 1, tile.height - 1)):
            ImageDraw.floodfill(tile, corner, (0, 0, 0, 0), thresh=35)
        # Retain the connected character silhouette, excluding neighboring
        # sprites' isolated antialias fragments at the historic tile boundaries.
        # Color connectivity avoids a residual black matte bridge to another tile.
        red, green, blue, alpha = tile.split()
        colored = ImageChops.lighter(ImageChops.lighter(red, green), blue)
        connected = colored.point(lambda value: 255 if value > 70 else 0)
        ImageDraw.floodfill(connected, (tile.width // 2, tile.height // 2), 128)
        silhouette = connected.point(lambda value: 255 if value == 128 else 0)
        # Fill interior holes so black eyes and mouth stay opaque inside the cloud.
        for corner in ((0, 0), (tile.width - 1, 0), (0, tile.height - 1), (tile.width - 1, tile.height - 1)):
            ImageDraw.floodfill(silhouette, corner, 128)
        silhouette = silhouette.point(lambda value: 0 if value == 128 else 255)
        tile.putalpha(ImageChops.multiply(alpha, silhouette))
        SPRITES.append(ImageReader(tile.crop(tile.getchannel("A").getbbox())))
    register_fonts()
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=(WIDTH, HEIGHT))
    surface.setTitle("Cody / Navigation atlas")
    for number, (title, subtitle, screens, flow, details) in enumerate(PAGES, 1):
        surface.setFillColor(CYAN)
        surface.setFont("CodyDocBold", 10)
        surface.drawString(42, HEIGHT - 30, "CODY / NAVIGATION ATLAS")
        surface.setFillColor(INK)
        surface.setFont("CodyDocBold", 24)
        surface.drawString(42, HEIGHT - 66, title)
        paragraph(surface, subtitle, 42, HEIGHT - 83, WIDTH - 84, 11)
        for item, x in zip(screens, (42, 307, 572)):
            screen(surface, item, x)
        surface.setStrokeColor(CYAN)
        surface.setFillColor(CYAN)
        surface.setLineWidth(1)
        if number == 2:
            surface.setFont("CodyDocBold", 8)
            surface.drawCentredString(419, 483, "READ SAVED WI-FI / IP / PAIRING")
            surface.setFont("CodyDoc", 7)
            surface.drawCentredString(684, 483, "LAN + unpaired -> 40 / Yes or No")
            surface.line(154, 470, 684, 470)
            surface.line(419, 479, 419, 470)
            for target in (154, 419, 684):
                surface.line(target, 470, target, 461)
                surface.line(target - 3, 464, target, 461)
                surface.line(target + 3, 464, target, 461)
        elif number in (3, 4, 5, 6, 7, 8, 9, 11, 12):
            for start in (267, 532):
                surface.line(start, 333, start + 38, 333)
                surface.line(start + 33, 337, start + 38, 333)
                surface.line(start + 33, 329, start + 38, 333)
            if number == 5:
                surface.setFont("CodyDocBold", 7)
                surface.drawCentredString(286, 345, "Yes")
                surface.drawCentredString(154, 229, "No -> 16 / Home")
        paragraph(surface, flow, 42, 143, WIDTH - 84, 10)
        paragraph(surface, details, 42, 103, WIDTH - 84, 9)
        surface.setStrokeColor(LINE)
        surface.line(42, 34, WIDTH - 42, 34)
        surface.setFillColor(MUTED)
        surface.setFont("CodyDoc", 8)
        surface.drawString(42, 21, "11 October 2026 / static editorial schematics / confidential / refer to stable screen IDs")
        surface.drawRightString(WIDTH - 42, 21, str(number))
        surface.showPage()
    surface.save()
    document = pymupdf.open(stream=output.getvalue(), filetype="pdf")
    history = compact_register(history_records(original), start_page=len(document) + 1)
    with pymupdf.open(stream=history, filetype="pdf") as register:
        document.insert_pdf(register)
    security = compact_register(security_records(), start_page=len(document) + 1, section_title="Security")
    with pymupdf.open(stream=security, filetype="pdf") as register:
        document.insert_pdf(register)
    draft = STAGING / DESTINATION.name
    document.save(draft, garbage=4, deflate=True)
    DESTINATION.write_bytes(draft.read_bytes())
    for index, page in enumerate(document):
        page.get_pixmap(matrix=pymupdf.Matrix(1, 1)).save(STAGING / f"page-{index + 1:02}.png")
    # Contact sheets support inspection of every page while retaining full-size renders.
    for start in range(0, len(document), 12):
        contact = Image.new("RGB", (1280, 1040), "#c6d2dc")
        labels = ImageDraw.Draw(contact)
        for offset in range(min(12, len(document) - start)):
            image = Image.open(STAGING / f"page-{start + offset + 1:02}.png")
            image.thumbnail((310, 219))
            left = (offset % 4) * 320 + 5
            top = (offset // 4) * 340 + 22
            contact.paste(image, (left, top))
            labels.text((left, top - 18), f"Page {start + offset + 1}", fill="black")
        contact.save(STAGING / f"contact-{start + 1:02}.png")
    print(f"Navigation atlas: {len(document)} pages; illustrated flow 1-{len(PAGES)}; revision register follows, ending with Security.")


if __name__ == "__main__":
    main()
