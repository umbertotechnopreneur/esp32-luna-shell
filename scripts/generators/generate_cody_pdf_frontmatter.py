"""Publish coordinated covers, live contents, page labels and Obsidian mirrors."""

import argparse
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import shutil
import sys

import pymupdf
from PIL import Image, ImageDraw
from reportlab.lib import colors
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, render_revision_register

DOCS = ROOT / "docs/cody"
STAGING = ROOT / "artifacts/docs/frontmatter"
MIRROR = Path("C:/Users/umber/OneDrive/Obsidian/30_Engineering/Products/Cody Companion/atlas")
LOGO = ROOT / "simulator/windows/assets/branding/vibeware-logo.png"
PREFIX = "CodyFrontmatter/"
CATALOG = {
    "cody-ccp-connection-atlas-v1.pdf": ("CCP Communication Atlas", "Pairing, your PC helper and voice input", [(1,"The simple picture"),(2,"Pairing"),(3,"Voice input"),(4,"Changing chat while recording"),(5,"Connection and everyday use"),(16,"Protocol reference"),(26,"Audio transport and delivery"),(29,"Plugin tools and setup"),(36,"Audio API and support boundaries"),(37,"Source references")]),
    "cody-navigation-atlas-v1.pdf": ("Navigation Atlas", "Find your way through Cody", [(1,"Overview and first setup"),(5,"Connect Codex"),(7,"Home, information and Setup"),(9,"Questions and reply delivery"),(11,"Work status"),(12,"Voice journey"),(13,"Battery, standby and recovery"),(15,"Debug views")]),
    "cody-planning-atlas-v1.pdf": ("Planning Atlas", "Screen journeys, character and visual language", [(1,"Overview and screen inventory"),(3,"Circular layout"),(4,"Boot and setup"),(8,"Codex pairing and work"),(12,"Questions and replies"),(15,"Battery and sleep"),(17,"Character studies"),(20,"Color system"),(21,"Setup and information panels"),(23,"Voice design")]),
    "cody-manga-status-atlas-v1.pdf": ("Manga Status Atlas", "Expressions, materials and small moments", [(1,"Overview and current screens"),(3,"The fourteen expressive cues"),(5,"Pearl Mist"),(7,"Aurora Veil"),(9,"Cosmic Stardust"),(11,"Enchanted Mosaic"),(13,"Comparison, layout and decisions"),(16,"Idle moments"),(19,"Current behavior"),(25,"VibeWare boot identity"),(28,"Current screen revisions")]),
    "cody-ccp-pairing-quickstart-a5-v1.pdf": ("Get Connected", "Four practical steps to pair Cody", [(1,"Get ready"),(2,"On your PC"),(3,"Pair Cody"),(4,"Every day")]),
}

# Section identities are text anchors, not fixed page offsets. A revised source may
# insert or reorder pages without leaving the edition's contents pointing elsewhere.
ANCHORS = {
    "cody-ccp-connection-atlas-v1.pdf": [None, "Pairing is recognition, not a promise", "Speaking to Cody: one careful handoff", "If you change chat while recording", "One connection, three different facts", "Ports and packet envelopes", "Audio is a separate one-way binary channel", "CCP and the plugin: who does what?", "Audio input: public API and support boundaries", "Source crosswalk"],
    "cody-navigation-atlas-v1.pdf": [None, "Connect Codex / a separate decision", "Home and companion information", "Read a question, then choose", "PR status belongs to ordinary activity", "Voice input / retained planned journey", "Battery notices at a quiet moment", "Character debug and preview boundaries"],
    "cody-planning-atlas-v1.pdf": [None, "The circle is usable space", "Cody's current cold boot", "Pair Codex, then find the retained PC", "A question deserves its own attention", "Battery notices feel like Cody", "Inspect the composed expression", "The shared palette / all 256 slots", "Setup keeps intent and routes explicit", "Planned voice: wake, capture, send"],
    "cody-manga-status-atlas-v1.pdf": [None, "The fourteen cues / sensations", "Pearl Mist / 01", "Aurora Veil / 01", "Cosmic Stardust / 01", "Enchanted Mosaic / 01", "Compare the four materials", "Small moments while Cody waits", "A mouth that visibly breathes", "VibeWare first. Cody next.", "Addendum / boot and Wi-Fi"],
    "cody-ccp-pairing-quickstart-a5-v1.pdf": ["01 / GET READY", "02 / ON YOUR PC", "03 / PAIR CODY", "04 / EVERY DAY"],
}


# content: Current document body, without this publisher's front matter.
# name: Canonical catalog identity.
# Raises ValueError on absent or ambiguous headings; never guess an old page number.
def resolve_sections(content, name):
    headings = [page.get_text().splitlines()[:5] for page in content]
    sections = []
    for (_, label), anchor in zip(CATALOG[name][2], ANCHORS[name], strict=True):
        matches = [1] if anchor is None else [index + 1 for index, lines in enumerate(headings) if anchor in lines]
        if len(matches) != 1:
            raise ValueError(f"Section {label!r} requires one current heading {anchor!r}; found {matches}")
        sections.append((matches[0], label))
    return sections


# surface: ReportLab page surface.
# text: Plain caption to wrap.
# x: Left position in points.
# y: First baseline in points.
# width: Maximum available line width.
# size: Font size in points.
# font: Built-in PDF font identity.
def wrapped(surface, text, x, y, width, size, font="Helvetica"):
    surface.setFont(font, size)
    line = ""
    for word in text.split():
        candidate = (line + " " + word).strip()
        if surface.stringWidth(candidate, font, size) > width and line:
            surface.drawString(x, y, line)
            y -= size * 1.25
            line = word
        else:
            line = candidate
    surface.drawString(x, y, line)
    return y - size * 1.25


# title: Display name of this document.
# subtitle: Short purpose statement.
# size: Native page dimensions.
# entries: Section labels and final absolute page numbers.
def front_pages(title, subtitle, size, entries):
    width, height = size
    stream = BytesIO()
    surface = canvas.Canvas(stream, pagesize=size)
    margin = width * .08
    surface.setFillColor(colors.HexColor("#11233B"))
    surface.rect(0, 0, width, height, fill=1, stroke=0)
    surface.setStrokeColor(colors.HexColor("#224660"))
    for radius in (width * .25, width * .38, width * .51):
        surface.circle(width * .82, height * .68, radius, fill=0, stroke=1)
    surface.setFillColor(colors.HexColor("#58D5C9"))
    surface.roundRect(margin, height * .89, width * .44, 23, 11, fill=1, stroke=0)
    surface.setFillColor(colors.HexColor("#11233B"))
    surface.setFont("Helvetica-Bold", 8)
    surface.drawString(margin + 12, height * .89 + 8, "CONFIDENTIAL / INTERNAL USE ONLY")
    surface.drawImage(str(LOGO), margin, height * .67, width=width * .21, height=height * .14, preserveAspectRatio=True, mask="auto")
    surface.setFillColor(colors.HexColor("#58D5C9"))
    surface.setFont("Helvetica-Bold", 13)
    surface.drawString(margin, height * .57, "CODY / VIBEWARE")
    surface.setFillColor(colors.white)
    end = wrapped(surface, title, margin, height * .49, width - 2 * margin, min(38, width * .065), "Helvetica-Bold")
    surface.setFillColor(colors.HexColor("#C7D9E8"))
    wrapped(surface, subtitle, margin, end - 15, width - 2 * margin, 12)
    surface.setFont("Helvetica", 10)
    surface.drawString(margin, height * .17, "DRAFT / 12 October 2026")
    surface.drawString(margin, height * .125, "Copyright (c) 2026 Umberto Giacobbi")
    surface.drawString(margin, height * .095, "umbertogiacobbi.biz")
    surface.showPage()
    surface.setFillColor(colors.HexColor("#11233B"))
    surface.setFont("Helvetica-Bold", 26)
    surface.drawString(margin, height - 60, "Contents")
    surface.setFont("Helvetica", 9)
    surface.drawString(margin, height - 82, "Main sections / click a row to jump to its page")
    step = min(34, (height - 180) / max(1, len(entries)))
    for index, (label, number) in enumerate(entries):
        y = height - 118 - index * step
        surface.setFillColor(colors.HexColor("#11233B"))
        surface.setFont("Helvetica", min(11, width * .027))
        surface.drawString(margin, y, label)
        surface.setFont("Helvetica-Bold", 11)
        surface.drawRightString(width - margin, y, str(number))
        surface.setStrokeColor(colors.HexColor("#DDE6EF"))
        surface.line(margin, y - 9, width - margin, y - 9)
    surface.setFillColor(colors.HexColor("#64748B"))
    wrapped(surface, "Page numbers refer to this PDF. Earlier page references in historical notes retain their original edition numbering.", margin, 67, width - 2 * margin, 8)
    surface.showPage()
    surface.save()
    return stream.getvalue(), step


# path: Existing canonical PDF selected from the documented catalog.
# Raises ValueError if prior front matter cannot be safely separated.
# Raises OSError on authoring, mirror or rendering failure.
def publish(path):
    title, subtitle, sections = CATALOG[path.name]
    original = pymupdf.open(path)
    metadata = original.metadata
    previous = metadata.get("keywords", "")
    if previous.startswith(PREFIX):
        content_count = int(previous.split("/")[1])
        content = pymupdf.open()
        content.insert_pdf(original, from_page=2, to_page=content_count + 1)
    else:
        content = pymupdf.open()
        content.insert_pdf(original)
    count = len(content)
    size = tuple(content[0].rect)[2:]
    sections = resolve_sections(content, path.name)
    for label in ("Revision register", "Security"):
        for index, page in enumerate(content):
            if any(line == label or line.endswith(" / " + label) for line in page.get_text().splitlines()[:5]):
                sections = sections + [(index + 1, label)]
                break
    quick = "quickstart" in path.name
    # The communication atlas owns this dated entry in its canonical final
    # register. Other existing documents retain their publisher-owned record.
    if not quick and path.name != "cody-ccp-connection-atlas-v1.pdf":
        record = RevisionRecord("2026-10-12", "Change", "Coordinated cover and navigable contents", ("Add VibeWare cover artwork, author copyright and Draft / Confidential / Internal use only classification. Contents and bookmarks point to the final document pages; historical page and screen references retain their original identities.",), "All original pages; existing correction and Security sections preserved", "PDF authoring and visual review; no firmware execution.")
        register = pymupdf.open(stream=render_revision_register((record,), size, title, start_page=count + 3), filetype="pdf")
        content.insert_pdf(register)
        sections = sections + [(count + 1, "This edition / revision record")]
    entries = [(label, number + 2) for number, label in sections]
    front, step = front_pages(title, subtitle, size, entries)
    final = pymupdf.open(stream=front, filetype="pdf")
    final.insert_pdf(content)
    width, height = size
    margin = width * .08
    for index, (_, page_number) in enumerate(entries):
        y = 118 + index * step
        final[1].insert_link({"kind": pymupdf.LINK_GOTO, "from": pymupdf.Rect(margin, y - 14, width - margin, y + 10), "page": page_number - 1})
    final.set_toc([[1, "Cover", 1], [1, "Contents", 2]] + [[1, label, number] for label, number in entries])
    for index, page in enumerate(final):
        if index < 2:
            continue
        # The small edition label occupies the outer top margin, without rewriting history.
        page.draw_rect(pymupdf.Rect(0, 0, page.rect.width, 13), color=None, fill=(1, 1, 1), overlay=True)
        page.insert_text((10, 9), f"DRAFT / CONFIDENTIAL / INTERNAL USE ONLY     PDF PAGE {index + 1} / {len(final)}", fontsize=6, color=(.22, .30, .39))
        if any(line == "Security" or line.endswith(" / Security") for line in page.get_text().splitlines()[:5]):
            # An earlier edition painted over part of this heading. Replace only
            # its visual header band; the historical security entries stay intact.
            page.draw_rect(pymupdf.Rect(38, 39, page.rect.width - 38, 67), color=None, fill=(1, 1, 1), overlay=True)
            page.insert_text((42, 62), "Security", fontsize=23, fontname="hebo", color=(.09, .16, .24))
    final.set_metadata({"title": "Cody / " + title, "author": "Umberto Giacobbi", "subject": "Draft - Confidential - Internal use only", "keywords": PREFIX + str(count)})
    temporary = STAGING / path.name
    final.save(temporary, garbage=4, deflate=True)
    final.close()
    original.close()
    shutil.copyfile(temporary, path)
    shutil.copyfile(path, MIRROR / path.name)
    if sha256(path.read_bytes()).digest() != sha256((MIRROR / path.name).read_bytes()).digest():
        raise OSError("Obsidian mirror does not match " + path.name)
    document = pymupdf.open(path)
    thumbs = []
    for index, page in enumerate(document):
        pixmap = page.get_pixmap(matrix=pymupdf.Matrix(.6, .6), alpha=False)
        picture = Image.frombytes("RGB", (pixmap.width, pixmap.height), pixmap.samples)
        picture.thumbnail((400, 290))
        thumbs.append(picture)
        if index < 2 or index == len(document) - 1:
            page.get_pixmap(matrix=pymupdf.Matrix(1.2, 1.2)).save(STAGING / f"{path.stem}-{index + 1}.png")
    for start in range(0, len(thumbs), 12):
        sheet = Image.new("RGB", (1600, 960), "#CCD5DF")
        draw = ImageDraw.Draw(sheet)
        for offset, picture in enumerate(thumbs[start:start + 12]):
            x, y = offset % 4 * 400, offset // 4 * 320
            sheet.paste(picture, (x, y + 20))
            draw.text((x + 8, y + 3), str(start + offset + 1), fill="black")
        sheet.save(STAGING / f"{path.stem}-sheet-{start // 12 + 1}.png")
    print(f"{path.name}: {len(document)} pages; {len(entries)} sections; Obsidian SHA-256 {sha256(path.read_bytes()).hexdigest()}")


# No parameters; publish only canonical documentation PDFs, never graphic-source PDFs.
# Raises the original authoring error rather than reporting partial publication as success.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", choices=list(CATALOG))
    arguments = parser.parse_args()
    STAGING.mkdir(parents=True, exist_ok=True)
    MIRROR.mkdir(parents=True, exist_ok=True)
    for name in ([arguments.file] if arguments.file else CATALOG):
        publish(DOCS / name)


if __name__ == "__main__":
    main()
