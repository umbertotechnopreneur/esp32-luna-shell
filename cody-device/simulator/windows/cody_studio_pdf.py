"""Inventory-linked PDF export of immutable Studio acquisitions, never a renderer."""

from __future__ import annotations

from collections import Counter
from datetime import date
from hashlib import sha256
from io import BytesIO
import os
from pathlib import Path
from uuid import uuid4
from xml.sax.saxutils import escape

from cody_native_runtime import ROOT
from cody_studio_catalog import load_catalog
from cody_studio_runner import load_capture

ARTIFACTS = (ROOT / "artifacts").resolve()
EXPORTS = ARTIFACTS / "simulator/studio/atlas/exports"


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
        for key in ("id", "inventory_id", "variant", "scenario_revision", "source", "symbol", "rendering_mode"):
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
    surface.setFillColorRGB(.13, .12, .16)
    surface.setFont("Helvetica-Bold", 9)
    surface.drawString(34, document.pagesize[1] - 29, "CODY / STUDIO NATIVE ATLAS")
    surface.setFont("Helvetica", 8)
    surface.setFillColorRGB(.37, .35, .40)
    surface.drawString(34, 22, "SIMULATED / native C/C++ frames / physical acceptance pending")
    surface.drawRightString(document.pagesize[0] - 34, 22, str(document.page))
    surface.restoreState()


# manifest: Validated immutable suite identity and acquired build provenance.
# records: Cases with snapshotted native PNG bytes or explicit absences.
# catalog: Shared source catalog, including entries outside the selected suite.
# Returns ReportLab flowables; imports the optional PDF package only for export.
def pdf_story(manifest, records, catalog):
    from reportlab.lib import colors
    from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
    from reportlab.platypus import Image, KeepTogether, PageBreak, Paragraph, Spacer, Table, TableStyle

    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle("AtlasTitle", fontName="Helvetica-Bold", fontSize=25, leading=30,
                              textColor=colors.HexColor("#24202b"), spaceAfter=18))
    styles.add(ParagraphStyle("AtlasHeading", fontName="Helvetica-Bold", fontSize=15, leading=19,
                              textColor=colors.HexColor("#ad4d10"), spaceAfter=9))
    styles.add(ParagraphStyle("AtlasBody", fontName="Helvetica", fontSize=9, leading=13,
                              textColor=colors.HexColor("#34303c"), spaceAfter=7, splitLongWords=True))
    styles.add(ParagraphStyle("AtlasSmall", parent=styles["AtlasBody"], fontSize=7.5, leading=10,
                              spaceAfter=4, wordWrap="CJK"))
    # Long repository paths and hashes wrap without inserted hyphens that
    # could be mistaken for part of a source or capture identity.
    body = lambda text: Paragraph(text, styles["AtlasBody"])
    small = lambda text: Paragraph(text, styles["AtlasSmall"])
    counts = Counter(case["acquisition"] for case, _, _ in records)
    captured = counts["captured"]
    title = "Cody / Native screen atlas"
    if captured != len(records) or manifest["status"] != "complete":
        title += " / INCOMPLETE"
    story = [Paragraph(title, styles["AtlasTitle"]),
             body(f"<b>Selected suite:</b> {pdf_text(manifest['suite'])}<br/>"
                  f"<b>Run:</b> {pdf_text(manifest['run_id'])}<br/>"
                  f"<b>Result:</b> {pdf_text(manifest['status'])}"),
             body(f"<b>{captured} acquired native frames / {len(records)} selected cases.</b> "
                  f"The current shared source catalog contains {len(catalog)} entries. "
                  "These denominators describe different scopes; this PDF does not claim complete inventory coverage."),
             body("Each device image is an unchanged completed native C/C++ frame with synthetic observations. "
                  "Acquisition, visual difference and human review are separate states. "
                  "This report does not establish physical-device acceptance or audible playback."),
             Spacer(1, 12), Paragraph("Reference inventory", styles["AtlasHeading"])]
    inventory = manifest["inventory"]
    for label, key in (("Document", "document"), ("Revision", "revision"), ("SHA-256", "sha256")):
        story.append(small(f"<b>{label}:</b> {pdf_text(inventory.get(key))}"))
    story += [small("Inventory IDs are local to that document. Unmapped entries keep their stable source-catalog identity."),
              Spacer(1, 12), Paragraph("Native build", styles["AtlasHeading"]),
              small(f"<b>Library SHA-256:</b> {pdf_text(manifest['build']['library_sha256'])}"),
              small(f"<b>Manifest SHA-256:</b> {pdf_text(manifest['build']['manifest_sha256'])}"),
              body("The atlas validates recorded capture identity and hashes. It retains the build which produced "
                   "the frames; exporting an earlier valid result does not relabel it as the current build."),
              PageBreak()]
    for offset in range(0, len(records), 2):
        for case, metadata, pixels in records[offset:offset + 2]:
            local_id = case.get("inventory_id") or "Inventory ID unmapped"
            heading = Paragraph(f"{pdf_text(local_id)} / {pdf_text(case['title'])}", styles["AtlasHeading"])
            details = [small(f"<b>Source identity:</b> {pdf_text(case['id'])}"),
                       small(f"<b>Acquisition:</b> {pdf_text(case['acquisition'])}<br/>"
                             f"<b>Variant:</b> {pdf_text(case.get('variant'))}<br/>"
                             f"<b>Rendering:</b> {pdf_text(case.get('rendering_mode'))}")]
            if metadata is not None:
                details += [small(f"<b>Scenario:</b> {pdf_text(metadata['scenario_id'])}<br/>"
                                  f"<b>Revision:</b> {pdf_text(metadata['scenario_revision'])}<br/>"
                                  f"<b>Completed instant:</b> {metadata['at_ms']} ms"),
                            small(f"<b>Source:</b> {pdf_text(metadata['source'])}<br/>"
                                  f"<b>Function:</b> {pdf_text(metadata['symbol'])}"),
                            small(f"<b>Capture:</b> {pdf_text(metadata['capture_id'])}<br/>"
                                  "<b>Human review:</b> separate per-capture record; acquisition alone is not approval")]
                frame = Image(BytesIO(pixels), width=202, height=202)
                frame.hAlign = "LEFT"
            else:
                frame = body("<b>FRAME UNAVAILABLE</b><br/>"
                             "No device screen is reconstructed for a missing acquisition.")
                details += [small(f"<b>Reason:</b> {pdf_text(case.get('reason') or 'No acquisition reason recorded')}"),
                            small(f"<b>Source:</b> {pdf_text(case.get('source'))}<br/>"
                                  f"<b>Function:</b> {pdf_text(case.get('symbol'))}")]
            row = Table([[frame, details]], colWidths=[217, 310], hAlign="LEFT")
            row.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
                                     ("LEFTPADDING", (0, 0), (-1, -1), 0),
                                     ("RIGHTPADDING", (0, 0), (-1, -1), 10),
                                     ("BOTTOMPADDING", (0, 0), (-1, -1), 0)]))
            story += [KeepTogether([heading, row, Spacer(1, 24)])]
        story.append(PageBreak())
    story += [Paragraph("Coverage and absences", styles["AtlasTitle"]),
              body("Coverage below is derived from the same maintained catalog used by Studio. "
                   "A supported contract is not evidence of an acquired frame. Entries outside the selected "
                   "suite have no acquisition in this run."),
              body("<b>Selected acquisition states:</b> " + "; ".join(
                  f"{pdf_text(key)}: {counts[key]}" for key in sorted(counts)))]
    availability = Counter(entry["availability"] for entry in catalog)
    story.append(body("<b>Current catalog availability:</b> " + "; ".join(
        f"{pdf_text(key)}: {availability[key]}" for key in sorted(availability))))
    selected = {case["id"] for case, _, _ in records}
    outside = [entry for entry in catalog if entry["id"] not in selected]
    story += [Spacer(1, 10), Paragraph(f"Outside this suite / {len(outside)} entries", styles["AtlasHeading"])]
    for entry in outside:
        reason = entry.get("reason") or "Not selected for this run"
        identity = entry.get("inventory_id") or "Inventory ID unmapped"
        story.append(small(f"<b>{pdf_text(identity)} / {pdf_text(entry['id'])}</b> / {pdf_text(entry['title'])}<br/>"
                           f"{pdf_text(entry['availability'])}: {pdf_text(reason)}"))
    story += [PageBreak(), Paragraph("Corrections register", styles["AtlasTitle"])]
    register = Table([[body(f"{date.today().isoformat()}<br/><b>Generated snapshot</b>"),
                       body("Native acquisitions and absences reflect the selected immutable run. "
                            "Original inventory IDs and revision are retained. "
                            f"Affected cases: {', '.join(pdf_text(case.get('inventory_id') or case['id']) for case, _, _ in records)}.")]],
                     colWidths=[130, 397], hAlign="LEFT")
    register.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
                                 ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    story.append(register)
    return story


# manifest_path: Explicit atlas manifest, or None for the preserved last-valid result.
# output_path: Optional new PDF destination beneath artifacts; defaults to a unique export.
# Raises validation/import/PDF/I/O errors without replacing any existing artifact.
def export_atlas(manifest_path=None, output_path=None):
    from reportlab.lib.pagesizes import A4
    from reportlab.platypus import SimpleDocTemplate

    manifest, records = export_records(manifest_path)
    destination = (Path(output_path) if output_path is not None else
                   EXPORTS / f"cody-native-atlas-{manifest['run_id']}-{uuid4().hex[:8]}.pdf").resolve()
    if not destination.is_relative_to(ARTIFACTS) or destination.suffix.lower() != ".pdf":
        raise ValueError("Atlas PDF must remain beneath artifacts/ with a .pdf extension")
    if destination.exists():
        raise FileExistsError(f"Existing atlas PDF is preserved: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.stem}-pending-{uuid4().hex}.pdf")
    document = SimpleDocTemplate(str(temporary), pagesize=A4, rightMargin=34, leftMargin=34,
                                 topMargin=54, bottomMargin=40, title="Cody Studio native screen atlas",
                                 author="Cody Studio")
    document.build(pdf_story(manifest, records, load_catalog()),
                   onFirstPage=decorate_page, onLaterPages=decorate_page)
    # Atomic exclusive publication preserves a PDF created concurrently. The
    # temporary and final files reside in the same artifact directory.
    os.link(temporary, destination)
    temporary.unlink()
    return destination
