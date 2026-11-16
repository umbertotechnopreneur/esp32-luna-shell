"""Inventory-linked PDF export of immutable Studio acquisitions, never a renderer."""

from __future__ import annotations

from hashlib import sha256
from io import BytesIO
import os
from pathlib import Path
from uuid import uuid4
from xml.sax.saxutils import escape

from cody_native_runtime import ROOT
from cody_studio_catalog import inventory_label, load_catalog
from cody_studio_runner import load_capture

ARTIFACTS = (ROOT / "artifacts").resolve()
EXPORTS = ARTIFACTS / "simulator/studio/atlas/exports"
NOTE_LINE_SPACING = 7 * 72 / 25.4
NOTE_LINE_BASE = 48
NOTE_LINE_HEIGHTS = tuple(NOTE_LINE_BASE + index * NOTE_LINE_SPACING for index in range(3))
# Leave a full writing row above the highest rule and a gap before page content.
CONTENT_BOTTOM_MARGIN = NOTE_LINE_HEIGHTS[-1] + NOTE_LINE_SPACING + 12


# No parameters: locate the same Nerd Font on Windows or Linux without host-specific paths.
# Raises FileNotFoundError when the requested font is not installed.
def register_atlas_fonts():
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    roots = [Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/Windows/Fonts",
             Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts",
             Path.home() / ".local/share/fonts",
             Path("/usr/local/share/fonts/JetBrainsMonoNerdFontMono"),
             Path("/usr/share/fonts/truetype/JetBrainsMonoNerdFontMono")]
    for name, suffix in (("AtlasMono", "Regular"), ("AtlasMonoBold", "Bold")):
        filename = f"JetBrainsMonoNerdFontMono-{suffix}.ttf"
        path = next((root / filename for root in roots if (root / filename).is_file()), None)
        if path is None:
            raise FileNotFoundError(f"Install JetBrainsMono Nerd Font Mono {suffix}: {filename}")
        pdfmetrics.registerFont(TTFont(name, str(path)))
    pdfmetrics.registerFontFamily("AtlasMono", normal="AtlasMono", bold="AtlasMonoBold", italic="AtlasMono", boldItalic="AtlasMonoBold")


# value: Arbitrary catalog metadata to display as escaped, printable PDF prose.
# Returns text only; source captions and native image bytes are never modified.
def pdf_text(value):
    return escape(str(value if value is not None else "Not mapped").encode("ascii", "replace").decode("ascii"))


# manifest_path: Validated atlas manifest beneath the repository artifact root.
# Raises original atlas/capture errors or ValueError for inconsistent acquisition metadata.
def export_records(manifest_path):
    from cody_studio_atlas import load_atlas

    manifest = load_atlas(manifest_path)
    records = []
    for case in manifest["cases"]:
        if case["acquisition"] != "captured":
            records.append((case, None, None))
            continue
        path = (ROOT / case["capture"]).resolve(strict=True)
        expected = case["metadata_sha256"]
        if sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Atlas capture metadata hash mismatch: {case['id']}")
        record = load_capture(path)
        metadata = record.metadata
        for key in ("id", "inventory_id", "inventory_family_id", "variant", "scenario_revision", "source", "symbol", "rendering_mode"):
            if metadata.get(key) != case.get(key):
                raise ValueError(f"Atlas case and captured {key} disagree: {case['id']}")
        if metadata["build"] != manifest["build"] or metadata.get("inventory") != manifest["inventory"]:
            raise ValueError(f"Atlas capture build or inventory revision disagrees: {case['id']}")
        # Snapshot validated image bytes so later filesystem changes cannot
        # substitute another image while ReportLab lays out the document.
        pixels = record.image_path.read_bytes()
        if (sha256(pixels).hexdigest() != metadata["png_sha256"] or
                sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError(f"Atlas capture changed during export: {case['id']}")
        records.append((case, metadata, pixels))
    return manifest, records


# surface: ReportLab canvas for the completed page.
# document: Active page geometry and page count from the document owner.
def decorate_page(surface, document):
    surface.saveState()
    surface.setStrokeColorRGB(.73, .72, .75)
    surface.setLineWidth(.45)
    for height in NOTE_LINE_HEIGHTS:
        surface.line(34, height, document.pagesize[0] - 34, height)
    surface.setFillColorRGB(.13, .12, .16)
    surface.setFont("AtlasMonoBold", 9)
    surface.drawString(34, document.pagesize[1] - 29, "CODY / STUDIO NATIVE ATLAS")
    surface.setFont("AtlasMono", 7)
    surface.setFillColorRGB(.37, .35, .40)
    surface.drawString(34, 22, "SIMULATED / native C/C++ frames / physical acceptance pending")
    surface.drawRightString(document.pagesize[0] - 34, 22, str(document.page))
    surface.restoreState()


# reason: The original owner-provided absence explanation.
# Returns a short printable excerpt; complete evidence remains in the immutable manifest.
# Raises no exceptions for optional or non-string reasons.
def concise_reason(reason):
    value = " ".join(str(reason or "No frame available").split())
    if len(value) <= 96:
        return value
    return value[:93].rsplit(" ", 1)[0] + "..."


# manifest: Validated immutable suite identity and acquired build provenance.
# records: Cases with snapshotted native PNG bytes or explicit absences.
# catalog: Validated shared source catalog; not duplicated as a text appendix.
# Returns a compact story with five fixed native-frame slots per page.
# Raises ReportLab errors for invalid content; full evidence stays in the manifest.
def pdf_story(manifest, records, catalog):
    from reportlab.lib import colors
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.platypus import Image, PageBreak, Paragraph, Spacer, Table, TableStyle

    if not records:
        raise ValueError("An atlas must contain at least one selected case")
    heading = ParagraphStyle("AtlasCaseTitle", fontName="AtlasMonoBold", fontSize=10,
                             leading=13, textColor=colors.HexColor("#ad4d10"), spaceAfter=7)
    detail = ParagraphStyle("AtlasCaseDetail", fontName="AtlasMono", fontSize=8,
                            leading=11, textColor=colors.HexColor("#34303c"),
                            splitLongWords=True, wordWrap="CJK")
    story = []
    # The same slots on each page preserve the picture-to-note relationship.
    for index, (case, metadata, pixels) in enumerate(records):
        if index and index % 5 == 0:
            story.append(PageBreak())
        title = f"{pdf_text(inventory_label(case))} / {pdf_text(case['title'])}"
        source = Path(case.get("source") or "").name or "Source unavailable"
        source_text = pdf_text(source)
        if case.get("symbol"):
            source_text += " / " + pdf_text(case["symbol"])
        lines = [pdf_text(case["id"]), source_text]
        if metadata is not None:
            frame = Image(BytesIO(pixels), width=104, height=104)
            frame.hAlign = "LEFT"
            if case["rendering_mode"] == "presenter-isolated":
                lines.append("Isolated")
            if case.get("variant") not in (None, "default"):
                lines.append("Variant: " + pdf_text(case["variant"]))
            if metadata.get("at_ms"):
                lines.append(f"Time: {metadata['at_ms']} ms")
        else:
            frame = Paragraph("<b>FRAME UNAVAILABLE</b>", detail)
            lines.append(pdf_text(case["acquisition"]))
            lines.append(pdf_text(concise_reason(case.get("reason"))))
        text = [Paragraph(title, heading), Paragraph("<br/>".join(lines), detail)]
        row = Table([[frame, text]], colWidths=[120, 407], rowHeights=[108], hAlign="LEFT")
        row.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0), ("RIGHTPADDING", (0, 0), (-1, -1), 8),
            ("TOPPADDING", (0, 0), (-1, -1), 0), ("BOTTOMPADDING", (0, 0), (-1, -1), 0)]))
        story.extend((row, Spacer(1, 10)))
    return story


# manifest_path: Explicit atlas manifest, or None for the preserved last-valid result.
# output_path: Optional new PDF destination beneath artifacts; defaults to a unique export.
# Raises validation/import/PDF/I/O errors without replacing any existing artifact.
def export_atlas(manifest_path=None, output_path=None):
    from reportlab.lib.pagesizes import A4
    from reportlab.platypus import SimpleDocTemplate

    register_atlas_fonts()
    manifest, records = export_records(manifest_path)
    destination = (Path(output_path) if output_path is not None else
                   EXPORTS / f"cody-native-atlas-{manifest['run_id']}-{uuid4().hex[:8]}.pdf").resolve()
    if not destination.is_relative_to(ARTIFACTS) or destination.suffix.lower() != ".pdf":
        raise ValueError("Atlas PDF must remain beneath artifacts/ with a .pdf extension")
    if destination.exists():
        raise FileExistsError(f"Existing atlas PDF is preserved: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.stem}-pending-{uuid4().hex}.pdf")
    inventory = manifest["inventory"]
    # Provenance remains in PDF properties and the immutable manifest, not a repeated appendix.
    subject = (f"Inventory: {inventory.get('document')}; revision: {inventory.get('revision')}; "
               f"inventory SHA-256: {inventory.get('sha256')}; run: {manifest['run_id']}; "
               f"native library SHA-256: {manifest['build']['library_sha256']}")
    document = SimpleDocTemplate(str(temporary), pagesize=A4, rightMargin=34, leftMargin=34,
                                 topMargin=54, bottomMargin=CONTENT_BOTTOM_MARGIN, title="Cody Studio native screen atlas",
                                 author="Cody Studio", subject=subject)
    document.build(pdf_story(manifest, records, load_catalog()),
                   onFirstPage=decorate_page, onLaterPages=decorate_page)
    # Atomic exclusive publication preserves a PDF created concurrently. The
    # temporary and final files reside in the same artifact directory.
    os.link(temporary, destination)
    temporary.unlink()
    return destination
