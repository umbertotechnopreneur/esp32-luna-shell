"""Append idle-scene design pages while retaining both existing Cody atlases."""

from io import BytesIO
from pathlib import Path
import json
import shutil

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[2]
ATLAS = Path(r"C:/Users/umber/OneDrive/Obsidian/30_Engineering/Products/Cody Companion/atlas")
OUTPUT = ROOT / "artifacts/pdf-review/cody-idle-2026-10-05"
SKIN = ROOT / "assets/cody/appearance/jelly-scanline/skins/cody_skin_azure_lilac_gradient_v1.png"
W, H = landscape(A4)
INK = "#142737"
MUTED = "#536776"
BLUE = "#359FE7"
PALE = "#F1F6F9"
WHITE = "#F4F6FA"
REVISION = "05 OCT 2026"
MARKER = "IDLE SCENES / 05 OCT 2026"


# document: Current ReportLab canvas.
# label: English document copy.
# x: Horizontal anchor in points.
# y: Top-down baseline in points.
# size: Font size.
# color: Hexadecimal foreground color.
# font: Embedded font name.
def text(document, label, x, y, size=10, color=INK, font="Body"):
    document.setFillColor(HexColor(color))
    document.setFont(font, size)
    document.drawString(x, H - y, label)


# document: Current canvas.
# label: Paragraph copy.
# x: Left edge in points.
# y: First top-down baseline.
# width: Available paragraph width.
# size: Font size.
# color: Foreground color.
def paragraph(document, label, x, y, width, size=10, color=MUTED):
    lines = []
    line = ""
    for word in label.split():
        candidate = (line + " " + word).strip()
        if line and pdfmetrics.stringWidth(candidate, "Body", size) > width:
            lines.append(line)
            line = word
        else:
            line = candidate
    lines.append(line)
    for index, line in enumerate(lines):
        text(document, line, x, y + index * size * 1.4, size, color)
    return y + len(lines) * size * 1.4


# document: Current canvas.
# title: Page title.
# subtitle: Short description below the title.
# number: Final page number in the destination PDF.
# kind: Manga status or navigation label.
def page_frame(document, title, subtitle, number, kind):
    document.setFillColor(HexColor("#FFFFFF"))
    document.rect(0, 0, W, H, fill=1, stroke=0)
    text(document, "CODY / " + kind + " / " + MARKER, 36, 29, 8, BLUE, "MonoBold")
    text(document, title, 36, 65, 24, INK, "BodyBold")
    text(document, subtitle, 36, 89, 10)
    document.setStrokeColor(HexColor("#D6E1E8"))
    document.line(36, H - 103, W - 36, H - 103)
    document.line(36, 43, W - 36, 43)
    text(document, "Umberto Giacobbi | hello@umbertogiacobbi.biz", 36, H - 27, 8, MUTED)
    text(document, "Copyright umbertogiacobbi.biz 2026 - confidential", 36, H - 13, 7, MUTED)
    text(document, "DESIGN / STATIC SOURCE REVIEW ONLY", W - 319, H - 27, 8, MUTED, "Mono")
    text(document, f"{number:02d}", W - 53, H - 13, 9, BLUE, "MonoBold")


# document: Current canvas.
# x: Left edge in page coordinates.
# y: Top edge in page coordinates.
# diameter: Printed circular display diameter.
# mouth_open: Normalized artistic mouth opening.
# mouth_width: Normalized width relative to the Calm face.
# eye_open: Shared normalized eyelid opening.
# wink: Right-eye closing contribution for the greeting.
# stressed: Select the stressed eye/brow character.
def cody_frame(document, x, y, diameter, mouth_open=.04, mouth_width=.72,
               eye_open=1, wink=0, stressed=False):
    document.saveState()
    document.translate(x, H - y - diameter)
    document.scale(diameter / 466, diameter / 466)
    document.setFillColor(HexColor("#04101A"))
    document.circle(233, 233, 233, fill=1, stroke=0)
    # The original PNG remains unchanged. Native PDF clipping presents its same
    # authored crop; separate vector face marks are illustrative, not a simulation.
    left, top, right, bottom = 28, 206, 1229, 1082
    factor = 340 / (right - left)
    body_height = (bottom - top) * factor
    body_x, body_y = 63, 466 - 110 - body_height
    clip = document.beginPath()
    clip.rect(body_x, body_y, 340, body_height)
    document.clipPath(clip, stroke=0, fill=0)
    document.drawImage(str(SKIN), body_x - left * factor,
                       body_y - (1280 - bottom) * factor,
                       width=1280 * factor, height=1280 * factor, mask="auto")
    document.restoreState()
    document.saveState()
    document.translate(x, H - y - diameter)
    document.scale(diameter / 466, diameter / 466)
    document.setFillColor(HexColor("#151E30"))
    document.setStrokeColor(HexColor("#151E30"))
    for eye_x, closure in ((184, eye_open), (282, eye_open * (1 - wink))):
        if closure < .3:
            document.setLineWidth(5)
            document.line(eye_x - 12, 245, eye_x + 12, 245)
        else:
            document.ellipse(eye_x - 13, 245 - 23 * closure,
                             eye_x + 13, 245 + 23 * closure, fill=1, stroke=0)
            document.setFillColor(HexColor("#FFFFFF"))
            document.circle(eye_x - 3, 248, 3, fill=1, stroke=0)
            document.setFillColor(HexColor("#151E30"))
    if stressed:
        document.setLineWidth(5)
        document.line(165, 283, 196, 272)
        document.line(270, 272, 301, 283)
    document.ellipse(228, 224, 238, 234, fill=1, stroke=0)
    document.setFillColor(HexColor("#D58BA1"))
    for cheek_x in (157, 309):
        document.ellipse(cheek_x - 14, 210, cheek_x + 14, 218, fill=1, stroke=0)
    half_width = 24 * mouth_width / .72
    mouth_height = 2 + 29 * mouth_open
    document.setFillColor(HexColor("#151E30"))
    document.ellipse(233 - half_width, 205 - mouth_height,
                     233 + half_width, 205 + mouth_height, fill=1, stroke=0)
    if mouth_open >= .28:
        document.setFillColor(HexColor("#EF95AB"))
        document.ellipse(233 - half_width * .62, 207 - mouth_height,
                         233 + half_width * .62, 204, fill=1, stroke=0)
    document.restoreState()


# document: Current canvas.
# start: Page number assigned to the first appended page.
def manga_pages(document, start):
    page_frame(document, "Small moments while Cody waits", "Four presentations within screen 16 / Idle. Cody remains front-facing.", start, "MANGA STATUS")
    rows = [
        ("IS01 / Breathing", "One continuous breathing clock. A visible mouth pulse remains under stress.",
         ((.026, .72, 1, 0), (.235, .76, 1, 0), (.026, .72, 1, 0)), ("Exhale", "Inhale", "Exhale")),
        ("IS02 / Yawn", "Eyes soften and the mouth opens widely, then returns to the base face. Silent.",
         ((.04, .72, 1, 0), (.86, .72, .25, 0), (.04, .72, 1, 0)), ("Rest", "Yawn peak", "Return")),
        ("IS03 / Whistle", "Small pursed mouth. One original 300-ms two-note whistle at the visible peak.",
         ((.04, .72, 1, 0), (.24, .42, 1, 0), (.04, .72, 1, 0)), ("Prepare", "Purse + sound", "Return")),
        ("IS04 / Greeting", "A friendly smile, right-eye wink and cheek lift. A quiet hello with no new route.",
         ((.04, .72, 1, 0), (.65, 1.1, 1, .65), (.04, .72, 1, 0)), ("Notice", "Hello / wink", "Return")),
    ]
    for row, (name, copy, frames, captions) in enumerate(rows):
        top_y = 117 + row * 104
        text(document, name, 36, top_y + 18, 11, BLUE, "MonoBold")
        paragraph(document, copy, 36, top_y + 40, 205, 9.5)
        for column, (frame, caption) in enumerate(zip(frames, captions)):
            frame_x = 272 + column * 180
            cody_frame(document, frame_x, top_y, 82, *frame)
            text(document, caption, frame_x, top_y + 96, 9)
    document.showPage()

    page_frame(document, "Keep the breath readable under stress", "Stronger opening and closing; preserve the selected expression and the existing rhythm.", start + 1, "MANGA STATUS")
    cody_frame(document, 46, 128, 150, 0, .5, .48, stressed=True)
    cody_frame(document, 234, 128, 150, .22, .55, .48, stressed=True)
    text(document, "Stressed / exhale", 46, 301, 10, INK, "MonoBold")
    text(document, "Stressed / inhale", 234, 301, 10, INK, "MonoBold")
    paragraph(document, "The stressed preset begins with a closed mouth. The revised pulse opens it to 0.22 on inhalation, even with no motion-derived stress contribution. These are illustrative channel extremes, not rendered firmware frames.", 46, 333, 343, 11)
    rules = [
        ("Visible breathing", "Exhalation closes 35% of the current opening; inhalation uses 16-22% of the remaining range. Closed expressions receive the extra boost. Width pulses by 4.5%."),
        ("A quiet random rhythm", "Wait 20-40 active seconds in eligible Idle. After a scene, cool down for 40 seconds and then wait again. Never repeat the last selected gesture."),
        ("One character, one route", "Each gesture lasts the authored 3.2 seconds. Body placement, skin, circular clipping and route history remain owned by the existing renderer."),
        ("Device facts take priority", "Current device cues and interactions suppress idle gestures. The three gestures are artistic accents and add no fabricated measurements or C01-C14 status meanings."),
    ]
    y = 132
    for heading, copy in rules:
        text(document, heading, 429, y, 12, BLUE, "BodyBold")
        y = paragraph(document, copy, 429, y + 24, 365, 10.5) + 25
    paragraph(document, "Canvas 466 x 466; center (233,233), radius 233. Safe square [73,393) x [73,393). IS01-IS04 are document labels. Runtime gesture IDs are typed identities, not screen numbers. No additional controls or touch targets.", 46, 514, W - 92, 10)
    document.showPage()


# document: Current canvas.
# x: Left edge in points.
# y: Top edge in points.
# width: Panel width.
# height: Panel height.
# heading: Panel title.
# copy: Optional description.
def box(document, x, y, width, height, heading, copy=""):
    document.setFillColor(HexColor(PALE))
    document.roundRect(x, H - y - height, width, height, 10, fill=1, stroke=0)
    text(document, heading, x + 14, y + 25, 11, BLUE, "MonoBold")
    if copy:
        paragraph(document, copy, x + 14, y + 49, width - 28, 10)


# document: Current canvas.
# start: First appended page number.
# Exception: Propagates ReportLab rendering failures.
def navigation_pages(document, start):
    page_frame(document, "Idle moments without leaving Idle", "A05 supplements base pages 02, 06, 10 and 11. Screen 16 retains its identity.", start, "NAVIGATION")
    box(document, 310, 120, 221, 76, "16 / Idle", "Breathing + current base expression")
    text(document, "20-40 active seconds / eligible and quiet", 267, 226, 11, MUTED)
    box(document, 265, 246, 311, 76, "Random selection", "Choose one of the other gestures; no immediate repeat")
    panels = [
        (36, "IS02 / Yawn", "Soft eyelids; broad mouth. Silent."),
        (302, "IS03 / Whistle", "Pursed mouth; one optional 300-ms sound."),
        (568, "IS04 / Greeting", "Friendly smile, wink and cheek lift. Silent."),
    ]
    document.setStrokeColor(HexColor(BLUE))
    document.setLineWidth(1.5)
    document.line(420, H - 196, 420, H - 211)
    document.line(420, H - 230, 420, H - 246)
    for x, title, copy in panels:
        document.line(420, H - 322, x + 119, H - 351)
        box(document, x, 351, 238, 88, title, copy)
    paragraph(document, "3.2 seconds -> return to the same Idle expression -> 40-second cooldown -> new 20-40-second wait. The automatic clock greeting may stay visible behind these accents. Onboarding Hello 04 remains a separate route.", 36, 479, W - 72, 11)
    document.showPage()

    page_frame(document, "Interrupt promptly and protect real work", "Transient gesture policy; no new settings writes and no extension of device inactivity.", start + 1, "NAVIGATION")
    rows = [
        ("Normal Idle", "Allow the three gestures only while the normal Main view is running, with a resting expression and no current personality symbol or caress."),
        ("Touch or activity", "Cancel the gesture as soon as the route/model consumes the real interaction. New motion-derived stress, microphone activity or other playback also takes priority. The Sophia trial only wakes software standby."),
        ("Settings / setup / work", "Suppress scheduling outside screen 16. Never navigate away from a question, pairing attempt, working state, error, settings or Debug route to show a gesture."),
        ("Pause / Sleep / Wake", "Do not admit gestures while paused or during lifecycle transitions. Active-time clocks exclude parked time; no backlog or catch-up scene is created."),
        ("Whistle admission", "Only a completed visible pursed-mouth frame may request sound. Quiet hours, volume zero, disabled sounds, busy audio and cooldown suppress it; do not retry later."),
        ("Audio ownership", "The audio owner publishes the actual cue identity and successful PCM level in the canonical snapshot. Cody's own whistle keeps its mouth pursed; unrelated playback interrupts."),
    ]
    y = 132
    for heading, copy in rows:
        text(document, heading, 36, y, 11, BLUE, "MonoBold")
        end = paragraph(document, copy, 244, y, 554, 10.5)
        y = max(y + 63, end + 17)
    paragraph(document, "Source implementation and the Windows visual model were updated. Firmware build, tests, simulator execution and physical appearance/acoustics remain unverified and require separate authorization.", 36, 516, W - 72, 10)
    document.showPage()


# document: Current canvas.
# page_number: Final page number.
# kind: Atlas section label.
# original_pages: Number of pages preserved before this revision.
def errata_page(document, page_number, kind, original_pages):
    page_frame(document, "Errata and corrections", "Revision date: 05 October 2026. Previous pages and addenda remain intact.", page_number, kind)
    changes = [
        ("Breathing visibility", "Clarifies a visible mouth pulse under stress: 35% closing, 16-22% reopening and a 4.5% width pulse. Affects screen 16 / Idle and the Stressed expression."),
        ("Three idle scenes", "Adds IS02 Yawn, IS03 Whistle and IS04 Greeting alongside IS01 Breathing. These are transient screen-16 accents, not new routes or status measurements."),
        ("Random scheduling", "Replaces the fixed idle-yawn trigger with a random 20-40-second eligible wait, a 3.2-second gesture and a 40-second cooldown. Excludes immediate repeats."),
        ("Audio and interruption", "Adds one original 300-ms whistle via the existing optional sound owner. Documents cancellation, quiet/volume/busy suppression and canonical playback identity."),
        ("Affected original references", "Manga pages 03-04 (cue language), 14 (circular placement), 15 (handoff/motion); Navigation pages 02, 06, 10, 11-12. Screens 04, 16, 35A/35B, 36-38 retain their identities and navigation."),
        ("Preservation and evidence", f"Original pages 1-{original_pages}, including all earlier addenda/errata, are preserved. This revision appends two design pages and this errata page. Runtime behavior and acoustics have static source review only."),
    ]
    y = 137
    for heading, copy in changes:
        text(document, heading, 36, y, 12, BLUE, "BodyBold")
        end = paragraph(document, copy, 263, y, 534, 10.5)
        y = max(y + 63, end + 17)
    document.showPage()


# No parameters: append only, retain backups and mirror the finished atlases into docs/Cody.
# Raises file, PDF or rendering-library errors; existing documents are replaced atomically.
def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    font_dir = Path.home() / "AppData/Local/Microsoft/Windows/Fonts"
    for name, file in (("Mono", font_dir / "JetBrainsMonoNerdFontMono-Regular.ttf"),
                       ("MonoBold", font_dir / "JetBrainsMonoNerdFontMono-Bold.ttf"),
                       ("Body", Path(r"C:/Windows/Fonts/segoeui.ttf")),
                       ("BodyBold", Path(r"C:/Windows/Fonts/segoeuib.ttf"))):
        pdfmetrics.registerFont(TTFont(name, str(file)))
    results = []
    for name, kind, make_pages in (("cody-manga-status-atlas-v1.pdf", "MANGA STATUS", manga_pages),
                                   ("cody-navigation-atlas-v1.pdf", "NAVIGATION", navigation_pages)):
        target = ATLAS / name
        original = PdfReader(target)
        if any(MARKER in (page.extract_text() or "") for page in original.pages):
            raise ValueError(f"This revision already exists in {name}; edit the revision explicitly")
        base_count = len(original.pages)
        backup = OUTPUT / (target.stem + "-before-idle.pdf")
        if not backup.exists():
            shutil.copy2(target, backup)
        appendix_stream = BytesIO()
        document = canvas.Canvas(appendix_stream, pagesize=(W, H))
        document.setTitle("Cody idle scenes - " + kind)
        document.setAuthor("Umberto Giacobbi")
        make_pages(document, base_count + 1)
        errata_page(document, base_count + 3, kind, base_count)
        document.save()
        appendix_stream.seek(0)
        writer = PdfWriter()
        writer.clone_document_from_reader(original)
        for page in PdfReader(appendix_stream).pages:
            writer.add_page(page)
        intermediate = OUTPUT / name
        with intermediate.open("wb") as destination:
            writer.write(destination)
        # Keep the cloud-sync replacement local to its destination filesystem.
        staging = ATLAS / (name + ".writing")
        shutil.copy2(intermediate, staging)
        staging.replace(target)
        shutil.copy2(target, ROOT / "docs/Cody" / name)
        result = {"path": str(target), "original_pages": base_count,
                  "appended_pages": [base_count + 1, base_count + 2, base_count + 3]}
        results.append(result)
        print(json.dumps(result))
    (ATLAS / "source/idle-scenes-2026-10-05-manifest.json").write_text(
        json.dumps(results, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
