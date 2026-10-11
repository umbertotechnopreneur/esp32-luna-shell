"""Update current Cody PDF pages and append a dated, traceable screen addendum."""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path

from PIL import Image
from pypdf import PdfReader, PdfWriter
from pypdf.generic import NameObject
from reportlab.lib.colors import HexColor
from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas

from generate_cody_setup_revision import paragraph, register_fonts

ROOT = Path(__file__).resolve().parents[2]
REVISION = "CODY-SCREENS-20261010"
DATE = "10 October 2026"
INK = HexColor("#142737")
MUTED = HexColor("#536776")
TEAL = HexColor("#008E87")
WHITE = HexColor("#FFFFFF")


@dataclass(frozen=True)
class Page:
    title: str
    subtitle: str
    sections: tuple
    screens: tuple = ()


BOOT = Page("Cody's current cold boot", "Branding precedes screens 01-03; route selection uses saved state.", (
    ("Identity and timing", "Cody AI Companion and A distraction-free sidekick appear together, above the complete floppy artwork. The original 42-frame animation retains its 3.5-second cycle; startup work can extend wall time."),
    ("Layout and next route", "Title y=60; subtitle y=110; artwork (137,164), 192 x 219. After local startup, absent Wi-Fi opens Welcome 04; missing association/IP opens 42; a valid LAN without pairing opens 40; retained pairing searches its PC on 14."),
), ("boot",))

NETWORK = Page("Wi-Fi setup has its own journey", "Screens 04-09 and 42 / provisioning is separate from Codex pairing.", (
    ("Show QR, then Pair Cody", "05 opens the installation QR on 06 without starting radios. Pair Cody starts Bluetooth on 07. A session QR appears only at ReadyToScan, then retires when credentials or connection are in progress. Cancel waits for confirmed cleanup."),
    ("A real local connection", "09 offers Continue only with valid current Wi-Fi/IP. No connection on 42 offers Network setup or Later. CCP needs the local LAN; public Internet availability is a separate fact. Debug handoff 39 needs a fresh tap; Release continues to 09."),
), ("network", "network-wait"))

SETUP = Page("Setup keeps the saved PC explicit", "Screen 41, decision 40 and retained-pairing variants / no implicit replacement.", (
    ("Reopen choices", "Hold the visible character still for 1.5 seconds to open Setup. Connect Codex and Network setup share one row; Options is centered below. Release the opening contact before selecting. Back retraces the valid previous route."),
    ("Keep, replace or forget", "Already paired offers Keep pairing, Pair again and Forget. Only Pair again authorizes replacement; Forget opens a second confirmation. Pairing paused identifies an incomplete candidate. Pairing error preserves the storage cause and admits explicit removal only."),
), ("setup", "retained", "choice"))

PAIRING = Page("From the code to the retained PC", "Screens 12 -> 13 -> 14 -> 15 / association and presence are different facts.", (
    ("Prepare, then read", "12 shows Preparing secure code / Please wait / About 30 seconds until a code exists. The estimate is not measured progress. Use only all six characters visible on the intended Cody after discovery. The backend window is five minutes; screen inactivity is three minutes from code readiness."),
    ("Confirm and connect", "You're all set on 13 follows the two-sided persisted handshake. Let's go searches the retained PC immediately on 14, or reuses valid authenticated presence on 15. The earlier ten-second Idle detour is removed. READY establishes presence; genuine prompt hooks establish chat activity."),
), ("code", "all-set", "connected"))

PREFERENCES = Page("Make it yours: four working preferences", "Screen 10 -> Settings 36 -> Skin 11 / Options returns to its caller.", (
    ("Four compact rows", "Shake to wake, Auto-rotate and Sounds each have persistent On/Off values. Orientation shows Auto while auto-rotation is enabled; manual choices are 0 or 180 when it is Off. Sounds Off stops playback while keeping microphone and wake word active."),
    ("Save before confirming", "Next opens Settings: Personalization / Choose Cody's look. Continue opens Skin: Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic and Dreamwave, saved by stable ID. Saving retains the prior value; successful write/readback confirms the change. Save failed and Retry preserve the cause and requested value; no silent repair."),
), ("preferences", "settings", "skin"))

LANGUAGE = ("Controls appear together", "Two actions use rounded gradient cards side by side; three add one centered action below. Single actions use an icon-left capsule. Captions have no teletype: controls and touch become available together after the title and main body line. QR quiet zones and local circular chords remain protected.")
EVIDENCE = ("How to read the drawings", "These are static documentation schematics derived from current source geometry and approved action references, not simulator frames or physical captures. Numeric values are layout coordinates, not sensor measurements. Firmware, simulator and device validation are outside this document revision.")


# name: Exact document filename; each entry targets only affected current pages.
# Raises KeyError when an unknown document is requested.
def replacements(name):
    return {
        "cody-navigation-atlas-v1.pdf": {2: BOOT, 3: NETWORK, 4: SETUP, 12: PREFERENCES},
        "cody-planning-atlas-v1.pdf": {4: BOOT, 5: NETWORK, 6: Page("Finish Wi-Fi without inventing success", NETWORK.subtitle, NETWORK.sections), 7: PREFERENCES, 8: PAIRING},
        "cody-manga-status-atlas-v1.pdf": {2: Page("Identity before expressive cues", "Current boot and setup context / the fourteen cue identities remain stable.", (BOOT.sections[0], ("Cues respect the route", "Boot, onboarding, settings and small headers suppress manga symbols. Main/Idle and semantic Sleep keep their existing cue rules. Preferences and pairing do not fabricate device health or replace canonical snapshot data.")), ("boot",))},
    }[name]


# pdf: Destination canvas in 466 x 466 logical coordinates with top-down y.
# label: Complete ASCII caption, excluded from typewriter timing.
# bounds: Logical control rectangle within the screen's occupied chord.
# color: Semantic control color; rendering is a documentation schematic.
def control(pdf, label, bounds, color="#125F68"):
    x, y, width, height = bounds
    palette = {"No": "#513844", "Later": "#3B4551", "Forget": "#632E39", "Pair again": "#564531", "Options": "#3E405E"}
    color = palette.get(label, color)
    pdf.saveState()
    clip = pdf.beginPath()
    clip.roundRect(x, 466 - y - height, width, height, min(28, height / 2))
    pdf.clipPath(clip, stroke=0)
    pdf.linearGradient(x, 466 - y, x + width, 466 - y - height, (HexColor(color), HexColor("#101B25")))
    pdf.restoreState()
    pdf.setStrokeColor(HexColor("#4DA9AF"))
    pdf.roundRect(x, 466 - y - height, width, height, min(28, height / 2), stroke=1, fill=0)
    pdf.setFillColor(WHITE)
    pdf.setFont("SetupBold", 15 if width < 170 else 18)
    # A simple icon marks the source's icon-left action convention, without claiming pixel parity.
    pdf.setStrokeColor(WHITE)
    pdf.setLineWidth(2)
    center_y = 466 - y - height / 2
    pdf.lines([(x + 18, center_y, x + 28, center_y), (x + 24, center_y + 4, x + 28, center_y), (x + 24, center_y - 4, x + 28, center_y)])
    pdf.drawCentredString(x + width / 2 + 7, 466 - y - height / 2 - 5, label)


# pdf: Destination canvas with local logical screen coordinates.
# text: Complete bounded ASCII text used by the source UI.
# y: Logical top row; font approximation is disclosed on every illustrated page.
# size: Documentation font size in logical pixels.
def caption(pdf, text, y, size=23):
    pdf.setFillColor(WHITE)
    pdf.setFont("SetupBold", size)
    pdf.drawCentredString(233, 466 - y - size, text)


# pdf: Destination canvas with local logical screen coordinates.
# top: Logical center of the illustrative cloud character.
def character(pdf, top=125):
    pdf.setFillColor(HexColor("#DCECF2"))
    for x, dy, radius in ((193, 0, 30), (220, -14, 37), (253, -8, 34), (279, 5, 24)):
        pdf.circle(x, 466 - top - dy, radius, fill=1, stroke=0)
    pdf.setFillColor(INK)
    for x in (214, 253):
        pdf.circle(x, 466 - top, 4, fill=1, stroke=0)
    pdf.setStrokeColor(INK)
    pdf.setLineWidth(2)
    pdf.arc(225, 466 - top - 18, 246, 466 - top, startAng=200, extent=140)


# pdf: Destination canvas in original PDF dimensions.
# identity: Unique illustration key; no runtime/simulator code is executed.
# x: Screen left in PDF points.
# top: Screen top measured down from the PDF page top.
# diameter: PDF screen diameter in points.
# height: PDF page height in points.
# Raises ValueError for unknown screen identities or unavailable original artwork.
def screen(pdf, identity, x, top, diameter, height):
    pdf.saveState()
    pdf.translate(x, height - top - diameter)
    pdf.scale(diameter / 466, diameter / 466)
    pdf.setFillColor(HexColor("#000000"))
    pdf.circle(233, 233, 233, fill=1, stroke=0)
    if identity == "boot":
        caption(pdf, "Cody AI Companion", 60, 25)
        caption(pdf, "A distraction-free sidekick", 110, 16)
        with Image.open(ROOT / "assets/branding/VibeWare/source/vibeware-floppy-insert-transparent.gif") as artwork:
            artwork.seek(20)
            manifest = json.loads((ROOT / "assets/branding/VibeWare/generated/manifest.json").read_text())
            frame = artwork.convert("RGBA").crop(tuple(manifest["crop_bounds"]))
            pdf.drawImage(ImageReader(frame), 137, 83, 192, 219, mask="auto")
    elif identity == "preferences":
        character(pdf, 92)
        caption(pdf, "Make it yours", 146, 25)
        for y, label, value in ((202, "Shake to wake", "On"), (240, "Auto-rotate", "On"), (278, "Sounds", "On"), (316, "Orientation", "Auto")):
            pdf.setFillColor(HexColor("#182731"))
            pdf.roundRect(117, 466 - y - 34, 232, 34, 12, fill=1, stroke=0)
            pdf.setFillColor(WHITE)
            pdf.setFont("SetupBody", 15)
            pdf.drawString(132, 466 - y - 23, label)
            pdf.drawRightString(334, 466 - y - 23, value)
        control(pdf, "Next", (123, 390, 220, 44))
    elif identity == "skin":
        caption(pdf, "Cody's look", 78, 25)
        character(pdf, 172)
        for y, label in ((226, "Pearl Mist"), (270, "Aurora Veil"), (314, "Cosmic Stardust")):
            control(pdf, label, (123, y, 220, 38))
        control(pdf, "Done", (123, 390, 220, 44))
    elif identity == "network":
        character(pdf, 94)
        # Deliberately non-scannable diagram: no fabricated provisioning payload.
        pdf.setFillColor(WHITE)
        pdf.rect(113, 86, 240, 240, fill=1, stroke=0)
        pdf.setFillColor(INK)
        pdf.setFont("SetupBold", 21)
        pdf.drawCentredString(233, 218, "Installation QR")
        pdf.setFont("SetupBody", 14)
        pdf.drawCentredString(233, 195, "diagram only")
        control(pdf, "Pair Cody", (123, 390, 220, 44))
    else:
        character(pdf, 92 if identity == "settings" else 130)
        titles = {"setup": "Setup", "retained": "Already paired", "choice": "Connect Codex?", "network-wait": "No connection", "code": "Connect Codex", "all-set": "You're all set", "connected": "Connected", "settings": "Settings"}
        if identity not in titles:
            raise ValueError("Unknown screen identity: " + identity)
        caption(pdf, titles[identity], 210 if identity != "settings" else 146, 26)
        if identity in ("setup", "retained", "choice", "network-wait"):
            labels = {"setup": ("Connect Codex", "Network setup"), "retained": ("Keep pairing", "Pair again"), "choice": ("Yes", "No"), "network-wait": ("Network setup", "Later")}[identity]
            for bx, label in zip((73, 239), labels):
                control(pdf, label, (bx, 260, 154, 104))
            if identity in ("setup", "retained"):
                control(pdf, "Options" if identity == "setup" else "Forget", (133, 380, 200, 44))
        elif identity == "code":
            caption(pdf, "Preparing secure code", 300, 19)
            caption(pdf, "Please wait", 332, 18)
            caption(pdf, "About 30 seconds", 359, 16)
        elif identity == "settings":
            caption(pdf, "Personalization", 238, 20)
            caption(pdf, "Choose Cody's look", 268, 18)
            control(pdf, "Continue", (113, 352, 240, 72))
        elif identity == "all-set":
            control(pdf, "Let's go", (113, 352, 240, 72))
    if identity in ("preferences", "settings", "skin", "retained"):
        pdf.setFillColor(HexColor("#26313A"))
        pdf.circle(40, 233, 30, fill=1, stroke=0)
        pdf.setStrokeColor(WHITE)
        pdf.setLineWidth(3)
        pdf.lines([(46, 244, 35, 233), (35, 233, 46, 222)])
    pdf.restoreState()


# spec: Fully authored text and optional static screen keys.
# dimensions: Original PDF page size, retained exactly.
# label: Exact document identity for the header.
# number: Physical PDF page number, independent of screen identity.
# Raises ValueError for text overflow; OSError for unavailable authored assets.
def author_page(spec, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    stream = BytesIO()
    pdf = canvas.Canvas(stream, pagesize=dimensions, pageCompression=1)
    paragraph(pdf, "CODY / " + label + " / CURRENT SCREENS", margin, 22, width - margin * 2, height, 8, True, TEAL)
    end = paragraph(pdf, spec.title, margin, 48, width - margin * 2, height, 22 if compact else 26, True)
    end = paragraph(pdf, spec.subtitle, margin, end + 9, width - margin * 2, height, 9, False, MUTED)
    top = max(122, end + 20)
    if spec.screens and not compact:
        diameter = 198
        for index, identity in enumerate(spec.screens):
            x = margin + index * (diameter + 32)
            screen(pdf, identity, x, top, diameter, height)
            paragraph(pdf, identity.upper(), x, top + diameter + 7, diameter, height, 8, True, TEAL)
        top += diameter + 33
    columns = 1 if compact else min(2, len(spec.sections))
    gap = 22
    column_width = (width - 2 * margin - gap * (columns - 1)) / columns
    for index, (title, text) in enumerate(spec.sections):
        x = margin if compact else margin + (index % columns) * (column_width + gap)
        section_top = top if compact else top + (index // columns) * 175
        end = paragraph(pdf, title, x, section_top, column_width, height, 11, True)
        end = paragraph(pdf, text, x, end + 8, column_width, height, 9.3 if compact else 10)
        if compact:
            top = end + 17
    if spec.screens and not compact:
        paragraph(pdf, "STATIC SCHEMATICS / illustrative fonts and values / no device capture", margin, height - 70, width - 2 * margin, height, 7, False, MUTED)
    pdf.setStrokeColor(HexColor("#D6E1E8"))
    pdf.line(margin, 45, width - margin, 45)
    pdf.setFillColor(MUTED)
    pdf.setFont("SetupBody", 6.3 if compact else 7)
    pdf.drawString(margin, 30, "Umberto Giacobbi | hello@umbertogiacobbi.biz | confidential")
    pdf.drawString(margin, 18, REVISION + " | " + DATE + " | Source-based document revision")
    pdf.drawRightString(width - margin, 18, str(number))
    pdf.showPage()
    pdf.save()
    return PdfReader(BytesIO(stream.getvalue())).pages[0]


# name: Exact source filename.
# changed: Explicit current-page replacements, used in the final errata trace.
# Raises KeyError for an unknown document identity.
def addendum(name, changed):
    page_ids = ", ".join(str(number) for number in sorted(changed))
    return (
        Page("Addendum / boot and Wi-Fi", "Revision date: " + DATE + " / branding, 01-09 and 42.", BOOT.sections + NETWORK.sections),
        Page("Addendum / pairing and connection", "Revision date: " + DATE + " / screens 12-15, 40 and 41.", SETUP.sections + PAIRING.sections),
        Page("Addendum / settings and controls", "Revision date: " + DATE + " / screens 10, 11, 36 and 41.", PREFERENCES.sections + (LANGUAGE, EVIDENCE)),
        Page("Errata and corrections", "Revision date: " + DATE + " / " + REVISION, (
            ("Affected original pages", name + ": current pages " + page_ids + ". Original screen references: branding before 01-03; Wi-Fi 04-09/42; preferences 10/36/11; Codex 12-15/40/41. Page numbers and screen IDs are distinct."),
            ("Corrections recorded", "Replace the old title-free/twofold boot illustration with the current title, subtitle and 192 x 219 artwork. Replace outdated setup, code preparation, connection detour and settings descriptions with the source-based contracts above."),
            ("Historical record", "All earlier addenda and errata are preserved in their existing order, along with unrelated pages. Historical boot sizes and old next-route diagrams remain dated evidence; the revised current pages and this addendum supersede them only for these topics."),
            EVIDENCE,
        )),
    )


# argv: Literal authoring arguments forwarded by the sole public device CLI.
# Raises OSError or ValueError for unavailable inputs, duplicate revisions or unsafe staging.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staging", type=Path, default=ROOT / "artifacts/docs" / REVISION.lower())
    parser.add_argument("--redraft", action="store_true", help="Reauthor this recorded draft from its protected originals")
    args = parser.parse_args(argv)
    staging = args.staging.resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Document staging must remain under repository artifacts")
    staging.mkdir(parents=True, exist_ok=True)
    register_fonts(Path("C:/Windows/Fonts"))
    # This historical revision targets only its three original editable atlases.
    # The canonical communication atlas and four-page flyer have dedicated owners.
    names = ("cody-navigation-atlas-v1.pdf", "cody-planning-atlas-v1.pdf",
             "cody-manga-status-atlas-v1.pdf")
    originals = {}
    if args.redraft:
        manifest_bytes = (staging / "manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        records = manifest.get("documents", [])
        if manifest.get("revision") != REVISION or len(records) != len(names):
            raise ValueError("Redraft manifest identity or document count differs")
        by_name = {record["file"]: record for record in records}
        if set(by_name) != set(names):
            raise ValueError("Redraft manifest document identities differ")
        for name in names:
            current = (ROOT / "docs/cody" / name).read_bytes()
            original = (staging / (Path(name).stem + "-original.pdf")).read_bytes()
            if sha256(current).hexdigest() != by_name[name]["revised_sha256"]:
                raise ValueError("Current draft changed outside this revision: " + name)
            if sha256(original).hexdigest() != by_name[name]["original_sha256"]:
                raise ValueError("Protected original changed: " + name)
            originals[name] = original
        archive = staging / "draft-history" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        archive.mkdir(parents=True, exist_ok=False)
        (archive / "manifest.json").write_bytes(manifest_bytes)
        for name in names:
            (archive / name).write_bytes((ROOT / "docs/cody" / name).read_bytes())
    prepared = []
    for name in names:
        source = ROOT / "docs/cody" / name
        original = originals[name] if args.redraft else source.read_bytes()
        reader = PdfReader(BytesIO(original))
        if any(REVISION in (page.extract_text() or "") for page in reader.pages):
            raise ValueError("Document revision already exists: " + name)
        changes = replacements(name)
        backup = staging / (source.stem + "-original.pdf")
        if backup.exists() and backup.read_bytes() != original:
            raise ValueError("Preserved source differs: " + name)
        backup.write_bytes(original)
        writer = PdfWriter()
        # Keep every source reader alive: pypdf caches clones by reader identity.
        # Recycling a short-lived reader can otherwise reuse another page's resources.
        authored_pages = []
        # Clone the catalog as well as pages so previous bookmarks and metadata survive.
        writer.clone_document_from_reader(reader)
        label = name.removeprefix("cody-").removesuffix("-v1.pdf").replace("-", " ").upper()
        for number, spec in changes.items():
            dimensions = tuple(float(value) for value in (reader.pages[number - 1].mediabox.width, reader.pages[number - 1].mediabox.height))
            replacement = author_page(spec, dimensions, label, number)
            authored_pages.append(replacement)
            # Preserve page identity and its existing bookmark destinations.
            target = writer.pages[number - 1]
            for key in ("/Contents", "/Resources"):
                target[NameObject(key)] = replacement.raw_get(key).clone(writer)
            target.pop("/Annots", None)
        dimensions = (float(reader.pages[0].mediabox.width), float(reader.pages[0].mediabox.height))
        additions = addendum(name, changes)
        for spec in additions:
            addition = author_page(spec, dimensions, label, len(writer.pages) + 1)
            authored_pages.append(addition)
            writer.add_page(addition)
        writer.add_outline_item("Current screen revision / " + DATE, len(reader.pages))
        writer.add_outline_item("Errata and corrections / " + DATE, len(writer.pages) - 1)
        writer.add_metadata({"/Subject": REVISION, "/ModDate": "D:20261010000000+07'00'"})
        destination = staging / name
        with destination.open("wb") as output:
            writer.write(output)
        record = {"file": name, "original_pages": len(reader.pages), "changed_pages": sorted(changes), "added_pages": len(additions), "final_pages": len(writer.pages), "original_sha256": sha256(original).hexdigest(), "revised_sha256": sha256(destination.read_bytes()).hexdigest()}
        prepared.append((source, destination, record))
    # Publish only after all six documents are successfully authored; retain protected originals.
    for source, destination, record in prepared:
        source.write_bytes(destination.read_bytes())
        print(record["file"] + ": " + str(record["final_pages"]) + " pages; current pages " + str(record["changed_pages"]))
    (staging / "manifest.json").write_text(json.dumps({"revision": REVISION, "date": "2026-10-10", "documents": [record for _, _, record in prepared]}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
