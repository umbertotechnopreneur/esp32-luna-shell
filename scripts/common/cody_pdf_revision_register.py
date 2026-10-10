"""Shared, paginated revision-register layout for Cody reference documents."""

from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Image, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle


INK = colors.HexColor("#18283E")
MUTED = colors.HexColor("#64748B")
LINE = colors.HexColor("#DEE6EF")
PAPER = colors.HexColor("#F4F7FB")
ACCENTS = {"Security": "#B35631", "Errata": "#A16C22", "Addendum": "#5262AA", "Change": "#147D86"}


@dataclass(frozen=True)
class RevisionRecord:
    date: str
    kind: str
    title: str
    paragraphs: tuple[str, ...]
    affected: str = ""
    evidence: str = ""
    image_path: str | None = None
    image_caption: str = ""


# Raises OSError if a selected font cannot be read.
def register_fonts():
    """Use the repository's Windows authoring fonts when available."""
    if "CodyDoc" in pdfmetrics.getRegisteredFontNames():
        return
    for name, file_name in (("CodyDoc", "segoeui.ttf"), ("CodyDocBold", "segoeuib.ttf")):
        path = Path("C:/Windows/Fonts") / file_name
        if path.exists():
            pdfmetrics.registerFont(TTFont(name, str(path)))
        else:
            pdfmetrics.registerFont(pdfmetrics.Font(name, "Helvetica-Bold" if name.endswith("Bold") else "Helvetica", "WinAnsiEncoding"))


# text: Plain text, escaped before ReportLab paragraph interpretation.
# style: Typography used for the paragraph.
def plain_paragraph(text, style):
    return Paragraph(escape(text).replace("\n", "<br/>"), style)


# records: Ordered dated entries; types must match the four canonical categories.
# page_size: PDF width and height in points.
# document_title: Running document name.
# start_page: Absolute page number of the first register page.
# section_title: Heading used throughout the final section.
# Raises ValueError for unsupported record categories or empty records.
# Raises OSError for unreadable supporting artwork.
def render_revision_register(records, page_size, document_title, start_page=1, section_title="Revision register"):
    """Return PDF bytes; dated rows stay intact and long records gain labelled continuations."""
    register_fonts()
    if not records:
        raise ValueError("A revision register requires at least one dated entry")
    width, height = page_size
    margin = 42
    available = width - margin * 2
    metadata_width = min(140, available * 0.23)
    body = ParagraphStyle("RegisterBody", fontName="CodyDoc", fontSize=10, leading=14, textColor=INK, spaceAfter=7)
    title = ParagraphStyle("RegisterTitle", parent=body, fontName="CodyDocBold", fontSize=12, leading=16)
    meta = ParagraphStyle("RegisterMeta", parent=body, fontSize=10, leading=14, textColor=MUTED)
    small = ParagraphStyle("RegisterSmall", parent=body, fontSize=9, leading=12, textColor=MUTED)
    header = ParagraphStyle("RegisterHeader", parent=body, fontName="CodyDocBold", fontSize=9, textColor=colors.white)
    rows = [[plain_paragraph("DATE / TYPE", header), plain_paragraph("REVISION DETAILS AND SUPPORTING MATERIAL", header)]]
    for record in records:
        if record.kind not in ACCENTS:
            raise ValueError(f"Unsupported revision type: {record.kind}")
        category = ParagraphStyle("RegisterCategory", parent=meta, fontName="CodyDocBold", textColor=colors.HexColor(ACCENTS[record.kind]))
        left = [plain_paragraph(record.date, meta), Spacer(1, 8), plain_paragraph(record.kind.upper(), category)]
        prefix = [plain_paragraph(record.title, title)]
        if record.affected:
            prefix.append(plain_paragraph("Affected: " + record.affected, small))
        content = [plain_paragraph(text, body) for text in record.paragraphs]
        if record.image_path:
            artwork = Image(record.image_path)
            scale = min((available - metadata_width - 28) / artwork.imageWidth, 138 / artwork.imageHeight)
            artwork.drawWidth = artwork.imageWidth * scale
            artwork.drawHeight = artwork.imageHeight * scale
            artwork.hAlign = "LEFT"
            content.extend((Spacer(1, 6), artwork))
            if record.image_caption:
                content.append(plain_paragraph(record.image_caption, small))
        if record.evidence:
            content.append(plain_paragraph("Evidence / limits: " + record.evidence, small))
        cell_width = available - metadata_width - 28
        # Leave room for the repeated column header and row padding. Each continuation
        # carries its own identity instead of stranding text with an empty date cell.
        maximum_height = height - 98 - 48 - 48 - 24
        chunk = list(prefix)
        chunk_height = sum(item.wrap(cell_width, maximum_height)[1] + item.getSpaceAfter() for item in chunk)
        while content:
            item = content.pop(0)
            item_height = item.wrap(cell_width, maximum_height)[1] + item.getSpaceAfter()
            if chunk_height + item_height > maximum_height and len(chunk) > len(prefix):
                rows.append([left, chunk])
                prefix = [plain_paragraph(record.title + " (continued)", title)]
                if record.affected:
                    prefix.append(plain_paragraph("Affected: " + record.affected, small))
                chunk = list(prefix)
                chunk_height = sum(part.wrap(cell_width, maximum_height)[1] + part.getSpaceAfter() for part in chunk)
            if chunk_height + item_height > maximum_height:
                pieces = item.split(cell_width, maximum_height - chunk_height - item.getSpaceAfter())
                if not pieces:
                    raise ValueError(f"Revision record cannot fit its supporting material: {record.title}")
                item = pieces[0]
                content[0:0] = pieces[1:]
                item_height = item.wrap(cell_width, maximum_height)[1] + item.getSpaceAfter()
            chunk.append(item)
            chunk_height += item_height
        rows.append([left, chunk])
    table = Table(rows, colWidths=[metadata_width, available - metadata_width], repeatRows=1, splitByRow=1, splitInRow=0)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), INK),
        ("BACKGROUND", (0, 1), (0, -1), PAPER),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.5, LINE),
        ("LEFTPADDING", (0, 0), (-1, -1), 14),
        ("RIGHTPADDING", (0, 0), (-1, -1), 14),
        ("TOPPADDING", (0, 0), (-1, -1), 12),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 12),
    ]))
    output = BytesIO()
    document = SimpleDocTemplate(output, pagesize=page_size, leftMargin=margin, rightMargin=margin, topMargin=98, bottomMargin=48)

    # canvas: PDF drawing surface for the register page.
    # doc: Platypus pagination state; its page counter supplies absolute numbering.
    def page_frame(canvas, doc):
        canvas.saveState()
        canvas.setFillColor(MUTED)
        canvas.setFont("CodyDocBold", 9)
        canvas.drawString(margin, height - 32, document_title.upper())
        canvas.setFillColor(INK)
        canvas.setFont("CodyDocBold", 23)
        canvas.drawString(margin, height - 64, section_title)
        canvas.setFillColor(MUTED)
        canvas.setFont("CodyDoc", 9)
        canvas.drawString(margin, height - 81, "Dated decisions, corrections and evidence. Historical entries retain their original scope.")
        canvas.setStrokeColor(LINE)
        canvas.line(margin, 35, width - margin, 35)
        canvas.setFont("CodyDoc", 8)
        canvas.drawString(margin, 21, "Cody / confidential / source review and device evidence are distinct")
        canvas.drawRightString(width - margin, 21, str(start_page + doc.page - 1))
        canvas.restoreState()

    document.build([table], onFirstPage=page_frame, onLaterPages=page_frame)
    return output.getvalue()
