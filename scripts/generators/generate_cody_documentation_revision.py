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

"""Preserve the Cody navigation atlas and append the source-grounded 6 October revision."""

import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw
from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "cody-device/simulator/windows"))
from scripts.generators.generate_cody_idle_atlas import ATLAS, W, H, text, paragraph
from scripts.common.cody_rig_contract import apply_mouth_breathing, contour_defaults
from scripts.common.cody_rig_types import FaceChannel
from scripts.common.cody_response import IdleSceneId, compose_response, make_response_state
from cody_simulator import CodyRenderer

OUTPUT = ROOT / "artifacts/pdf-review/cody-documentation-2026-10-06"
MARKER = "CURRENT BEHAVIOR / 06 OCT 2026"
NAMES = ("cody-navigation-atlas-v1.pdf",)
BLUE = "#359FE7"
INK = "#142737"
MUTED = "#536776"


# Raises OSError for an unavailable embedded font.
def register_fonts():
    fonts = {"Body": "segoeui.ttf", "BodyBold": "segoeuib.ttf",
             "Mono": "consola.ttf", "MonoBold": "consolab.ttf"}
    for name, filename in fonts.items():
        pdfmetrics.registerFont(TTFont(name, str(Path("C:/Windows/Fonts") / filename)))


# document: Destination canvas.
# title: English page heading.
# subtitle: Concise scope and original references.
# number: Absolute page number in the revised atlas.
# kind: Atlas identity.
def frame(document, title, subtitle, number, kind):
    document.setFillColor(HexColor("#FFFFFF"))
    document.rect(0, 0, W, H, fill=1, stroke=0)
    text(document, "CODY / " + kind + " / " + MARKER, 36, 29, 8, BLUE, "MonoBold")
    text(document, title, 36, 65, 24, INK, "BodyBold")
    text(document, subtitle, 36, 89, 10)
    document.setStrokeColor(HexColor("#D6E1E8"))
    document.line(36, H - 103, W - 36, H - 103)
    document.line(36, 43, W - 36, 43)
    text(document, "06 October 2026 | Source review; physical behavior unverified", 36, H - 27, 8, MUTED)
    text(document, "Prior pages retained. This appendix supersedes the cited behavior and values.", 36, H - 13, 7, MUTED)
    text(document, f"{number:02d}", W - 53, H - 13, 9, BLUE, "MonoBold")


# document: Destination canvas.
# heading: Short topic label.
# copy: Wrapped English explanation.
# x: Left page coordinate.
# y: Heading baseline from top.
# width: Text column width.
def block(document, heading, copy, x, y, width):
    text(document, heading, x, y, 13, INK, "BodyBold")
    return paragraph(document, copy, x, y + 22, width, 11) + 18


# renderer: Existing host renderer, without creating a UI or an audio owner.
# name: Stable illustration filename.
# preset: Authored expression key.
# breathing: Endpoint of the shared artistic breathing sine.
# Raises OSError or ValueError for invalid artwork or face channels.
def illustration(renderer, name, preset, breathing):
    values = list(renderer.rig["expression_presets"][preset])
    apply_mouth_breathing(values, breathing)
    transform = tuple(renderer.rig["default_transform"][key] for key in ("x", "y", "scale"))
    image, _ = renderer.render("rig", values, transform, debug=False)
    destination = OUTPUT / (name + ".png")
    image.save(destination)
    return destination, values


# renderer: Existing Windows renderer used only for document artwork.
# identity: Typed idle gesture to compose at its maximum artistic weight.
# Raises OSError or ValueError for invalid response or illustration data.
def gesture_illustration(renderer, identity):
    state = make_response_state(renderer.rig["response"])
    state.idle_scene = identity
    state.idle_scene_weight = 1
    state.yawn_weight = 1 if identity == IdleSceneId.YAWN else 0
    state.blink_elapsed_ms = 300
    transform = tuple(renderer.rig["default_transform"][key] for key in ("x", "y", "scale"))
    response = compose_response(renderer.rig, state, renderer.rig["expression_presets"]["calm"],
                                contour_defaults(renderer.rig), transform, 0)
    image, _ = renderer.render("rig", response.face, response.transform, debug=False,
                               contour=dict(response.contour, phase=0.0))
    destination = OUTPUT / (identity.name.lower() + ".png")
    image.save(destination)
    return destination, response.face


# document: Destination canvas.
# path: Native host-rendered document illustration.
# label: Caption beneath the image.
# x: Left page coordinate.
# y: Top page coordinate.
# size: Image extent in points.
def picture(document, path, label, x, y, size=170):
    document.drawImage(str(path), x, H - y - size, width=size, height=size, mask="auto")
    text(document, label, x, y + size + 18, 10, INK, "BodyBold")


# document: Destination canvas.
# start: First absolute appendix page number.
# poses: Rendered boot artwork paths.
def navigation_pages(document, start, poses):
    frame(document, "A shorter boot for a paired Cody", "N01 / Supersedes startup timing on pages 02 and 06; screens 01-05 and 16.", start, "NAVIGATION")
    for index, (name, label) in enumerate((("boot-sleep", "01 Sleeping / 2.0 s"), ("boot-waking", "02 Waking / 1.5 s"), ("boot-ready", "03 Ready / 2.0 s"))):
        picture(document, poses[name][0], label, 60 + index * 265, 122, 190)
    y = block(document, "Returning device: 03 -> 16", "A validated saved first-boot false goes straight to Idle and its clock routine. Entering Idle requests one random Affection or Joy for four seconds, using the same visible-symbol sound owner. Pairing and credential keys are preserved.", 36, 367, 365)
    block(document, "Service waits are additional", "The three phases take 5.5 active seconds altogether. Actual startup checkpoints gate progression; waiting for a service does not consume the next phase. Boot shows Cody without a website logo, Debug HUD or manga symbol.", 36, y, 365)
    y = block(document, "First setup: 03 -> 04 -> 05", "Hello 04 uses Happy and 'Hi, I'm Cody!' at (233,225,130): 70 ms per character, then one active second after the complete text reaches the panel. It proceeds to Pair 05. There is no second bitmap intro.", 439, 367, 367)
    block(document, "Boot differs from standby wake", "Sleep 35A and Wake 35B retain their separate power sequencing. A 400-ms eye closure plus 300-ms hold precedes standby; the existing panel fade is 800 ms. A sleeping expression alone does not request physical standby.", 439, y, 367)
    document.showPage()

    frame(document, "Idle keeps the route and the controls", "N02 / Updates pages 10-12, the 05 October idle appendix and screens 16 / 35A / 35B.", start + 1, "NAVIGATION")
    y = block(document, "Continuous expression and breath", "The stronger mouth breath composes with the selected front-facing expression. Idle accents use the same route, active clock and response controller. They never change navigation history, sensor timestamps, timeout preferences or device activity.", 36, 139, 365)
    y = block(document, "Eligible quiet time", "Only the normal Main/Idle view schedules Yawn, Whistle and Greeting, including the automatic clock title. Setup, Debug, explicit greetings, sleeping/working/stressed overrides, paused animation and current symbol/caress presentations are excluded.", 36, y, 365)
    block(document, "Interaction takes priority", "Eye responses, caresses and tap feedback remain available through their own owners. Real microphone activity, unrelated speaker playback, authored motion stress and touch release an active gesture. Standby can occur before a gesture becomes due.", 36, y, 365)
    y = block(document, "Canonical device facts", "LowBattery, Charging and NoWifi consume a valid, fresh canonical snapshot. Battery hysteresis enters at 15% and exits at 20%. Stale or disproven facts invalidate their current symbol; an artistic hold never preserves a false device status.", 439, 139, 367)
    y = block(document, "Circle, Back and all sizes", "Canvas 466 x 466, center (233,233), radius 233. The central safe square is [73,393) on both axes. Back occupies (10,203), 60 x 60, only with a previous route. Enlarged icons may overlap Cody but must avoid Back/status controls and fit the circular chord.", 439, y, 367)
    block(document, "Visibility precedes sound", "A prepared cue or pose does not prove display presentation. A newly visible retired frame admits the SFX. Hidden or blocked icons cannot emit a delayed appearance sound. Native errors remain visible in structured state and logs.", 439, y, 367)
    document.showPage()

    frame(document, "Cody Studio on Windows", "N03 / Desktop appendix: synthetic preview, audio boundary and branding assets.", start + 2, "NAVIGATION")
    logo = ROOT / "cody-device/simulator/windows/assets/branding/vibeware-logo.png"
    document.drawImage(str(logo), 40, H - 238, width=104, height=104, mask="auto")
    text(document, "VibeWare / Cody Studio", 164, 161, 18, INK, "MonoBold")
    text(document, "Face    Shape    Inputs    Inspect", 164, 193, 13, BLUE, "Mono")
    text(document, "Consolas UI / equal tab widths and padding", 164, 221, 10, MUTED, "Mono")
    y = block(document, "Fake driver; shared original sounds", "The PC driver links the existing Cody C++ personality, whistle and typewriter modules. Their original PCM reaches the Windows playback backend; boot uses the generated WAVs. Tap, newly visible symbols, whistle and greeting glyphs connect to that owner. There is one playback voice, no mixer.", 36, 283, 365)
    block(document, "Synthetic is explicit", "PC playback is never published into firmware snapshots. Native playback errors are retained and shown without stopping the preview. UI launch and a responding first frame have been observed; listening, complete idle cycles and physical device behavior remain unverified.", 36, y, 365)
    y = block(document, "Approved logo and useful links", "The existing VibeWare PNG is reused unchanged in the desktop header and window icon. These links open only on a click:", 439, 283, 367)
    links = (("GitHub / project", "https://github.com/umbertotechnopreneur/esp32-luna-shell"),
             ("Personal website", "https://umbertogiacobbi.biz"),
             ("VibeWare manifesto", "https://github.com/umbertotechnopreneur/VibeWare/blob/main/MANIFESTO.md"))
    for label, url in links:
        text(document, label, 439, y, 11, BLUE, "MonoBold")
        document.linkURL(url, (439, H - y - 3, 650, H - y + 12), relative=0)
        y += 28
    block(document, "Where the original asset lives", "Obsidian / 20_Execution / Projects / VibeWare / Branding / vibeware-logo.png. The repository copy, MIT license and provenance are in simulator/windows/assets/branding/. Desktop branding does not change the device boot artwork.", 439, y + 8, 367)
    document.showPage()


# document: Destination canvas.
# number: Absolute final page number.
# kind: Atlas identity.
# original_count: Number of retained pages, including previous errata.
def errata(document, number, kind, original_count):
    frame(document, "Errata and corrections", "Revision date: 06 October 2026 / Earlier pages and errata remain intact.", number, kind)
    items = [
        ("Mouth / Manga 03-04, 15 and 05 October idle appendix", "Replace the earlier 35% closing and 16-22% reopening guidance with 75% closing and 16-24% reopening. Width moves 18% toward its limits. Stressed peak opening is 0.24. Rest lip and tongue geometry now follow the revised rig."),
        ("Icons / Manga 03-04, 14; Navigation 10-12; Idle 16", "Correct Main/Sleep placement to +30 px right, +40 px down and scale to 3.6x (+20% over 3x). Both fades are 400 ms. Micro remains excluded. All fourteen cues retain an enabled, original appearance sound with bounded admission."),
        ("Interaction / Idle 16 and Sleep 35A", "Document moving caress recognition, Affection, varied Fear/Frustration eye responses, hysteresis and one fresh-press tap click. Single-voice playback and best-effort logging cannot block touch/rendering."),
        ("Idle / 05 October appendices; Navigation 10-12", "Correct Whistle width 0.42 to authored minimum 0.5. Retain the 20-40 s eligible wait, 3.2 s gesture and 40 s cooldown. Yawn/Greeting are silent; Whistle attempts one 300-ms sound. Stress does not schedule idle gestures."),
        ("Boot / Navigation 02, 06; screens 01-05, 16, 35A/35B", "Use 2.0 s Sleeping, 1.5 s Waking and 2.0 s Ready, excluding actual service waits. Paired returning devices skip setup and request random Affection/Joy in Idle. Standby wake remains a separate sequence."),
        ("Desktop / Navigation simulator appendix; Manga 15 handoff", "Record the PC fake audio driver, Consolas UI, equal tabs, approved VibeWare asset and project/site/manifesto links. Synthetic PC observations are separate from firmware and physical-device evidence."),
    ]
    y = 133
    for heading, copy in items:
        text(document, heading, 36, y, 10, INK, "BodyBold")
        y = paragraph(document, copy, 36, y + 17, W - 72, 9.5) + 12
    y = paragraph(document, f"Preserved pages 01-{original_count:02d}, including earlier errata. Firmware checkpoint de9b8b18 built/flashed on COM10; newer mouth/Whistle checkpoint f5ae5e2 has not been compiled or flashed. Two approved checks passed; four stopped/failed and have not been rerun. No physical visual/audio observation is claimed.", 36, y + 1, W - 72, 9, MUTED)
    block(document, "Canonical sources for this revision", "assets/cody/cody.yaml; assets/cody/personality.yaml; CODY_rig.cpp / CODY_response.cpp; docs/cody/runtime.md, behavior.md and personality-icons.md; simulator/windows/cody_simulator.py and cody_fake_audio.py. Clip durations come from the versioned personality audio manifest.", 36, y + 17, W - 72)
    document.showPage()


# destination: Staged complete PDF to review.
# first: First new page number.
# pdftoppm: Explicit installed Poppler executable.
# Raises OSError or CalledProcessError when PDF rendering fails.
def render_review(destination, first, pdftoppm):
    prefix = OUTPUT / (destination.stem + "-review")
    subprocess.run([pdftoppm, "-f", str(first), "-r", "105", "-png", str(destination), str(prefix)], check=True)
    images = sorted(OUTPUT.glob(prefix.name + "-*.png"))
    sheet = Image.new("RGB", (1000, ((len(images) + 1) // 2) * 375), "#DCE5EC")
    draw = ImageDraw.Draw(sheet)
    for index, path in enumerate(images):
        with Image.open(path) as image:
            image.thumbnail((488, 344))
            x, y = (index % 2) * 500 + 6, (index // 2) * 375 + 22
            sheet.paste(image, (x, y))
            draw.text((x, y - 17), path.stem, fill="#142737")
    sheet.save(OUTPUT / (destination.stem + "-contact.png"))


# Raises OSError, ValueError or a subprocess error for invalid sources or failed publication.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect", action="store_true", help="Read the original page headings without writing PDFs.")
    parser.add_argument("--publish", action="store_true", help="Publish previously reviewed staged PDFs, without regenerating them.")
    parser.add_argument("--pdftoppm", help="Installed Poppler executable for mandatory page review.")
    args = parser.parse_args()
    if args.inspect:
        for name in NAMES:
            reader = PdfReader(ATLAS / name)
            print(name, "pages", len(reader.pages))
            for index, page in enumerate(reader.pages):
                print(index + 1, page.extract_text()[:190].replace("\n", " | "))
        return
    if args.publish:
        manifest = json.loads((OUTPUT / "manifest.json").read_text(encoding="utf-8"))
        # Refuse to overwrite a source or staged document that changed during visual review.
        for item in manifest["documents"]:
            if sha256((ATLAS / item["file"]).read_bytes()).hexdigest() != item["original_sha256"]:
                raise ValueError(f"Original changed during review: {item['file']}")
            if sha256((OUTPUT / item["file"]).read_bytes()).hexdigest() != item["revised_sha256"]:
                raise ValueError(f"Staged PDF changed during review: {item['file']}")
        for item in manifest["documents"]:
            for destination in (ATLAS / item["file"], ROOT / "docs/cody" / item["file"]):
                shutil.copy2(OUTPUT / item["file"], destination)
                print("Published", destination)
        shutil.copy2(OUTPUT / "manifest.json", ATLAS / "source/documentation-2026-10-06-manifest.json")
        return
    if not args.pdftoppm:
        parser.error("--pdftoppm is required for document visual review")
    originals = {name: PdfReader(ATLAS / name) for name in NAMES}
    for name, reader in originals.items():
        if any(MARKER in page.extract_text() for page in reader.pages):
            raise ValueError(f"Revision already present: {name}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    register_fonts()
    renderer = CodyRenderer(jelly_study=True)
    poses = {}
    for preset in ("calm", "stressed"):
        for phase, breathing in (("exhale", -1), ("inhale", 1)):
            name = preset + "-" + phase
            poses[name] = illustration(renderer, name, preset, breathing)
    for identity in (IdleSceneId.YAWN, IdleSceneId.WHISTLE, IdleSceneId.GREETING):
        poses[identity.name.lower()] = gesture_illustration(renderer, identity)
    for name, image in renderer.boot.items():
        destination = OUTPUT / (name + ".png")
        image.save(destination)
        poses[name] = (destination, None)
    manifest = {"revision_date": "2026-10-06", "source_checkpoint": "f5ae5e2", "documents": []}
    for name in NAMES:
        source = ATLAS / name
        reader = originals[name]
        first = len(reader.pages) + 1
        backup = OUTPUT / ("original-" + name)
        if not backup.exists():
            shutil.copy2(source, backup)
        buffer = BytesIO()
        document = canvas.Canvas(buffer, pagesize=(W, H))
        navigation_pages(document, first, poses)
        added = 4
        kind = "NAVIGATION"
        errata(document, first + added - 1, kind, len(reader.pages))
        document.save()
        writer = PdfWriter()
        writer.clone_document_from_reader(reader)
        appendix = PdfReader(BytesIO(buffer.getvalue()))
        for page in appendix.pages:
            writer.add_page(page)
        writer.add_outline_item("Current behavior - 06 October 2026", first - 1)
        writer.add_outline_item("Errata and corrections - 06 October 2026", first + added - 2)
        writer.add_metadata({"/ModDate": "D:20261006000000+07'00'", "/Subject": MARKER})
        destination = OUTPUT / name
        with destination.open("wb") as stream:
            writer.write(stream)
        result = PdfReader(destination)
        if len(result.pages) != len(reader.pages) + added or "Errata and corrections" not in result.pages[-1].extract_text():
            raise ValueError(f"Incomplete document: {name}")
        # Preserve decoded original page streams byte-for-byte, including prior corrections.
        for before, after in zip(reader.pages, result.pages):
            if before.get_contents().get_data() != after.get_contents().get_data():
                raise ValueError(f"Original page stream changed: {name}")
        render_review(destination, first, args.pdftoppm)
        manifest["documents"].append({"file": name, "preserved_pages": len(reader.pages), "added_pages": added,
                                      "final_pages": len(result.pages), "original_sha256": sha256(source.read_bytes()).hexdigest(),
                                      "revised_sha256": sha256(destination.read_bytes()).hexdigest()})
        print("Staged for visual review", destination, "pages", len(result.pages))
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
