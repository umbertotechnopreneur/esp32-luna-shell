<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Printable annotation films

**VibeWare · Cody Companion**

**Author:** Umberto Giacobbi · **Created:** 2026-11-14 · **Updated:** 2026-11-14 · **Revision:** 1.1

This is the project standard for transparent annotation films ("lucidi").
When the owner requests films for a document, produce a print-ready PDF using
the appropriate layouts below. Adapt the guides to the actual target document
each time; the screen inventory is the initial example, not a fixed template
for all future documents. Generate films on request, not for every PDF edit.

## Three layout families

| Family | Align the interior guides to | Intended use |
| --- | --- | --- |
| Treeview | Actual indentation levels, hierarchy start/end and whitespace between displayed lines | Hierarchical maps and nested lists |
| Table | Actual table edges, column boundaries, header boundary and variable-height row boundaries | Tables and inventories |
| Text | Actual text margins, interline spacing and paragraph breaks; extend a matching ruled note area into unused space where useful | Prose and paragraph-based pages |

Measure the final source PDF, including its page boxes, orientation, content
positions and renderer padding. Use the rendered text, table and row geometry;
nominal document margins alone are insufficient. Place horizontal guides in
the whitespace between lines rather than through glyphs. Keep guides faint
over dark table headers and other dense content. Use only useful structural
guides; do not impose a 1 cm interior grid when it conflicts with the content.

Treeview guides follow the actual indentations, not arbitrary evenly spaced columns.
Table guides follow column widths and each row's height, not a uniform row pitch.
Text guides follow the actual leading and paragraph spacing, not the tree or
table geometry. Preserve the source document and its content.

## Page selection and number of films

- Inspect the requested document and select the layout family for each relevant
  page or region. Mixed pages can combine the necessary guides in one film.
- Include the number of films actually needed. Do not always generate three
  pages, and do not generate one film per source page when their guide geometry
  is identical. Reuse one film only where its geometry and clearance match.
- Create additional variants for distinct tables, indentations, page sizes,
  orientations or text layouts. Use page-specific films where necessary.
- Label each film with its family and corresponding source page(s), using a
  compact label outside the annotation frame. Explain the film-to-source page
  mapping in the delivery message if it cannot fit the footer clearly.
- The delivered PDF contains only the films, in the stated order. Source pages,
  covers, contents, instructions and revision-register pages do not belong in
  the printable film PDF. Keep print instructions and explanatory previews in
  chat. Ordinary documentation PDF front matter remains applicable elsewhere.

## Frame, rulers and discreet guides

Use the same physical page size and orientation as the source. For the current
A4 portrait screen inventory, the established frame is **190 x 260 mm**:
left/right margins 10 mm, bottom margin 20 mm and top margin 17 mm.
Its bottom-left corner is (10 mm, 20 mm). Adapt clearance to the actual source
footer and header for other documents. Prefer whole-centimetre frame dimensions
where practical, without shrinking or rescaling the source document.

- Frame: blood red `#880808`, dashed, approximately 0.5 pt with a 3 pt / 2 pt
  dash/gap. Keep the interior transparent: no filled page or opaque backing.
- Rulers: four sides, 1 mm ticks, intermediate 5 mm ticks and numbered 1 cm
  positions. Both ticks and numbers stay **outside** the frame. In the normal
  reading view, horizontal values increase left to right and vertical values
  increase top to bottom, from the frame's top-left origin.
- Established tick lengths: 0.4 mm, 0.8 mm and 1.2 mm for millimetre, half-centimetre
  and centimetre marks; approximately 0.16-0.30 pt strokes. Keep corner labels
  legible and clear of adjacent rulers.
- Interior grid: very light gray, approximately `#E3E3E3` to `#E7E7E7`, about
  0.16 pt, dashed. The established dash/gap is approximately 1.2-1.5 pt / 2.4-2.5 pt.
- Add two or three useful structural references in subdued red `#C79595`,
  approximately 0.32-0.35 pt, with a 3 pt / 3 pt dash/gap. Choose meaningful
  margins, column limits or header boundaries for that layout.
- Leave the source footer, its page number and branding unobstructed. Reserve
  an exterior lower strip for the film's own VibeWare/Cody branding and label.
  Check that ruler labels, source footer and film branding do not overlap.

Use the project VibeWare logo asset, Segoe UI for text, Consolas for measurements,
and the document branding colors: teal `#007F83` and ink `#14283E`. Keep all
branding outside the red frame and within usable printing margins. Film text
is English, as for other project PDFs.

## Centre registration and color swatches

Include these registration and print-reference elements on every future film,
for all three layout families. Position them in the normal reading view before
mirroring the whole page for delivery.

- **Central cross:** centre it on the physical page, not on the annotation
  frame or content block. Use solid blood red `#880808`, approximately 0.6 pt,
  with horizontal and vertical strokes each 5 mm long (2.5 mm on each side
  of the centre). On A4 portrait the centre is (105 mm, 148.5 mm).
- **Vertical centreline:** at half the physical page width, use pale red
  `#D8B1B1`, approximately 0.3 pt, with a 3 pt / 3 pt dash/gap. Extend it only
  between the annotation frame's bottom and top. Do not add a full horizontal
  centreline. Draw the central cross above this line so it remains distinct.
- **Exterior color swatches:** include six filled squares, one for each ink
  reference below, outside the red frame and clear of rulers, source headings
  and branding. Use 3 x 3 mm squares with 1 mm gaps and a thin ink-colored
  outline (approximately 0.15 pt) so the light gray remains visible.

The established swatch order in the normal reading view, left to right, is:

| Swatch | Color | Purpose |
| --- | --- | --- |
| 1 | `#880808` | Blood-red frame and central cross |
| 2 | `#C79595` | Subdued structural guides |
| 3 | `#D8B1B1` | Pale vertical centreline |
| 4 | Actual interior-grid gray, normally `#E3E3E3` or `#E7E7E7` | Interior guides |
| 5 | `#007F83` | Teal branding |
| 6 | `#14283E` | Ink text and swatch outlines |

Centre the swatch row above the top ruler where space permits. For the current
A4 portrait layout, the row is 23 mm wide, starts at x = 93.5 mm and y = 288.5 mm,
and ends at y = 291.5 mm, measured from the bottom-left page corner. Adapt its
placement for other page geometries and source headers, preserving printer
clearance. Swatches must use the actual colors drawn in that film. Mirror the
swatch row with all other page elements; its left-to-right order reverses on
the print side and returns to normal when the film is turned over.

## Mirroring and printing

Deliver films **already mirrored horizontally**. Mirror the complete page once,
including all guides, ruler numbers, labels and branding. Work out alignment in
the normal reading orientation first, then apply a full-page reflection.

The owner prints on the printable side of transparent film, then turns the film
over to expose the non-printed surface for handwritten notes. The guides and
branding become readable in their intended positions after that physical flip.

State at delivery: **print at actual size / 100%; disable fit-to-page and any
additional printer mirroring**. Preserve exact page dimensions and scale.

## Preview and delivery

Render and visually inspect every distinct film. Check the PDF page dimensions,
guide positions, mirroring, header/footer clearance, central cross and vertical
centreline alignment, exterior swatch clearance and readability. Digital
review does not establish the printer's physical scaling or minimum margins.

Show an example of each distinct layout over its corresponding source page at
unchanged scale. For the mirrored workflow, pair the readable in-use view with
the mirrored print-side view. Any source content in the previews is for alignment
only and must not enter the delivered film PDF. Label the two views clearly.

Keep generated files under the project's `artifacts/` area and preserve them for
manual cleanup. Copy the final film PDF byte for byte, using the same filename,
to `C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.
Report the number, order, types and source-page mapping of the delivered films.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
