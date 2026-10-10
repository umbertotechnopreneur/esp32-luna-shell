"""Translate historical Cody PDF text while preserving artwork and page identities."""

import argparse
from hashlib import sha256
from html import escape
import json
from pathlib import Path
import shutil
import sys

import pymupdf

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, render_revision_register

STAGING = ROOT / "artifacts/docs/cody-english-20261011"
MAPPINGS = ROOT / "assets/cody/docs/translations"
DOCUMENTS = {
    "navigation": "cody-navigation-atlas-v1.pdf",
    "planning": "cody-planning-atlas-v1.pdf",
    "service": "cody-ccp-service-atlas-v1.pdf",
    "legacy-connection": "cody-codex-connection-atlas-v1.pdf",
}


# value: Packed RGB color extracted from an original text span.
def css_color(value):
    return "#" + format(value, "06x")


# block: Original structured text block, including individual lines and spans.
# translation: English replacement for the complete extracted block.
# Raises ValueError if the block has no original text spans.
def replacement_parts(block, translation):
    lines = block["lines"]
    spans = [span for line in lines for span in line["spans"]]
    if not spans:
        raise ValueError("A translation requires original text spans")
    # ReportLab sometimes groups a distant footer page number with its label.
    # Keep the number at its own original position rather than reflowing it.
    last_text = "".join(span["text"] for span in lines[-1]["spans"])
    translated_lines = translation.rstrip("\n").split("\n")
    if len(lines) > 1 and last_text.isdigit() and translated_lines[-1] == last_text:
        body = dict(block, lines=lines[:-1])
        body["bbox"] = tuple(pymupdf.Rect([line["bbox"] for line in lines[:-1]][0]))
        rectangle = pymupdf.Rect(body["bbox"])
        for line in lines[1:-1]:
            rectangle |= pymupdf.Rect(line["bbox"])
        body["bbox"] = tuple(rectangle)
        number = dict(block, lines=[lines[-1]], bbox=lines[-1]["bbox"])
        return [(body, "\n".join(translated_lines[:-1])), (number, last_text)]
    return [(block, translation)]


# page: PDF page whose original text has already been removed.
# block: Original geometry and typography for this replacement.
# translation: English ASCII text, reflowed only within its original box.
# Raises ValueError if a replacement needs an unreadable font reduction.
def insert_replacement(page, block, translation):
    lines = block["lines"]
    spans = [span for line in lines for span in line["spans"]]
    style = max(spans, key=lambda span: len(span["text"]))
    size = style["size"]
    bold = "bold" in style["font"].lower()
    rectangle = pymupdf.Rect(block["bbox"])
    font_file = "segoeuib.ttf" if bold else "segoeui.ttf"
    text = translation.strip()
    if len(lines) == 1:
        # A one-line caption uses its original baseline. HTML line boxes add
        # excess height to narrow page numbers and small running headers.
        font_path = str(Path("C:/Windows/Fonts") / font_file)
        font = pymupdf.Font(fontfile=font_path)
        width = font.text_length(" ".join(text.splitlines()), fontsize=size)
        available = min(block.get("maximum_x", rectangle.x1 + 2),
                        rectangle.x0 + rectangle.width * 1.25 + 2) - rectangle.x0
        scale = min(1, available / width) if width else 1
        if scale < 0.83:
            raise ValueError(f"Caption does not fit page {page.number + 1}: {text[:100]}")
        page.insert_text((rectangle.x0, spans[0]["origin"][1]), " ".join(text.splitlines()),
                         fontsize=size * scale, fontname="CodyBold" if bold else "CodyRegular",
                         fontfile=font_path, color=pymupdf.sRGB_to_pdf(style["color"]))
        return {"page": page.number + 1, "font_scale": round(scale, 4), "text": text}
    rectangle.y0 -= 0.6
    rectangle.y1 += size * 0.3
    # Preserve paragraph baselines; a short single line has natural leading.
    leading = size * 1.25
    if len(lines) > 1:
        differences = [lines[index + 1]["spans"][0]["origin"][1] - lines[index]["spans"][0]["origin"][1]
                       for index in range(len(lines) - 1)]
        positive = [difference for difference in differences if difference > size * 0.8]
        if positive:
            leading = min(positive)
    css = (
        f"@font-face {{font-family: CodyOriginal; src: url('{font_file}');}}"
        f"* {{margin:0;padding:0;}} div {{font-family:CodyOriginal;font-size:{size}pt;"
        f"line-height:{leading}pt;color:{css_color(style['color'])};}}"
    )
    # Original hard wraps are prose layout, not semantic line breaks. Preserve
    # explicit numbered/list lines, otherwise let English fit the same column.
    html = "<div>" + escape(" ".join(text.splitlines())) + "</div>"
    spare, scale = page.insert_htmlbox(rectangle, html, css=css,
                                       archive=pymupdf.Archive("C:/Windows/Fonts"),
                                       scale_low=0.83)
    if spare < 0:
        raise ValueError(f"Translation does not fit page {page.number + 1}: {text[:100]}")
    return {"page": page.number + 1, "font_scale": round(scale, 4), "text": text}


# key: Canonical mapping/document identity.
# redraft: Allow re-authoring from the protected original after verifying current hash.
# Raises ValueError for stale mappings, duplicate identities or unexpected PDF changes.
# Raises OSError for missing source, mapping, fonts or output files.
def translate_document(key, redraft):
    name = DOCUMENTS[key]
    destination = ROOT / "docs/Cody" / name
    baseline = STAGING / name
    receipt = STAGING / (key + "-receipt.json")
    mapping_path = MAPPINGS / (key + "-en.json")
    mapping_hash = sha256(mapping_path.read_bytes()).hexdigest()
    records = json.loads(mapping_path.read_text(encoding="utf-8"))
    if receipt.exists():
        state = json.loads(receipt.read_text(encoding="utf-8"))
        if sha256(destination.read_bytes()).hexdigest() != state["output_sha256"]:
            raise ValueError("PDF changed after translation: " + name)
        if state.get("mapping_sha256", mapping_hash) != mapping_hash and not redraft:
            raise ValueError("Translation mapping changed; an explicit --redraft is required: " + key)
        if "baseline_sha256" in state and sha256(baseline.read_bytes()).hexdigest() != state["baseline_sha256"]:
            raise ValueError("Protected original changed: " + name)
        if not redraft:
            print("Already translated: " + name)
            return
    else:
        with pymupdf.open(destination) as current:
            if "English throughout the document" in current[-1].get_text():
                # A fresh checkout contains the committed English PDFs but
                # not local receipts. Skip only when no mapped original remains.
                has_original = any(record["source"] in [block[4] for block in current[record["page"] - 1].get_text("blocks")]
                                   for record in records)
                if not has_original and not redraft:
                    print("Committed English PDF: " + name)
                    return
                if not baseline.exists():
                    raise ValueError("Re-authoring requires the protected original: " + name)
        if not baseline.exists():
            shutil.copy2(destination, baseline)
    identities = {(record["page"], record["source"]) for record in records}
    if len(identities) != len(records):
        raise ValueError("Duplicate page/text translation identity: " + key)
    document = pymupdf.open(baseline)
    layout = []
    for page in document:
        selected = [record for record in records if record["page"] == page.number + 1]
        if not selected:
            continue
        blocks = page.get_text("dict")["blocks"]
        extracted = page.get_text("blocks")
        replacements = []
        for record in selected:
            if not record["translation"].isascii():
                raise ValueError("Translation must be English ASCII")
            matches = [item for item in extracted if item[4] == record["source"] and item[6] == 0]
            if not matches:
                raise ValueError(f"Source block missing on {key} page {page.number + 1}: {record['source'][:90]}")
            # Repeated diagram captions share one mapping; each retains its
            # own geometry. Duplicate mapping identities remain prohibited.
            for matched in matches:
                # Image inclusion differs between the two extraction formats,
                # so their numeric block indices are not shared identities.
                block = next(item for item in blocks if item["type"] == 0
                    and all(abs(item["bbox"][axis] - matched[axis]) < 0.01 for axis in range(4)))
                rectangle = pymupdf.Rect(block["bbox"])
                block["maximum_x"] = min([page.rect.width - 30] + [item["bbox"][0] - 5 for item in blocks
                    if item["type"] == 0 and item["bbox"][0] > rectangle.x1
                    and item["bbox"][1] < rectangle.y1 and item["bbox"][3] > rectangle.y0])
                replacements.extend(replacement_parts(block, record["translation"]))
                page.add_redact_annot(pymupdf.Rect(block["bbox"]), fill=None)
        # Text-only redaction retains diagrams, backgrounds, rules and artwork.
        page.apply_redactions(images=0, graphics=0, text=0)
        for block, translation in replacements:
            layout.append(insert_replacement(page, block, translation))
    affected = ", ".join(str(number) for number in sorted({record["page"] for record in records}))
    revision = RevisionRecord("11 October 2026", "Errata", "English throughout the document", (
        "Historical Italian text, headings, diagram captions and footers are translated into English. Original dates, screen IDs, technical values, evidence limits and artwork are preserved.",
        "The language review also covers the English consolidated connection atlas, the four-page A5 pairing flyer and the manga status atlas; those documents require no language correction.",
    ), affected="Original pages " + affected,
        evidence="Document language and visual review only; no firmware build, simulator or physical-device verification.")
    register = pymupdf.open(stream=render_revision_register((revision,), tuple(document[0].rect)[2:],
                             name, start_page=len(document) + 1,
                             section_title="Errata and corrections"), filetype="pdf")
    document.insert_pdf(register)
    temporary = STAGING / (key + "-translated.pdf")
    document.save(temporary, garbage=4, deflate=True)
    document.close()
    shutil.copy2(temporary, destination)
    receipt.write_text(json.dumps({"output_sha256": sha256(destination.read_bytes()).hexdigest(),
                                  "mapping_sha256": mapping_hash,
                                  "baseline_sha256": sha256(baseline.read_bytes()).hexdigest(),
                                  "translated_blocks": len(records), "layout": layout}, indent=2), encoding="utf-8")
    print(f"Translated {name}: {len(records)} blocks; revision register appended")


# Raises ValueError or OSError when a selected document cannot be safely translated.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document", choices=tuple(DOCUMENTS))
    parser.add_argument("--redraft", action="store_true")
    options = parser.parse_args()
    STAGING.mkdir(parents=True, exist_ok=True)
    for key in (options.document,) if options.document else DOCUMENTS:
        translate_document(key, options.redraft)


if __name__ == "__main__":
    main()
