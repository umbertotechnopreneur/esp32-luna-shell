"""Append the requested startup/setup correction to the five existing Cody PDFs."""

import argparse
from enum import Enum
from io import BytesIO
from pathlib import Path
from xml.sax.saxutils import escape

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph

ROOT = Path(__file__).resolve().parents[2]
REVISION = "09 October 2026"
REVISION_ID = "CODY-SETUP-20261009"
RECOVERY_REVISION_ID = "CODY-SETUP-RECOVERY-20261009"
INTERFACE_REVISION_ID = "CODY-UI-DECISION-20261009"
ACTIVITY_REVISION_ID = "CODY-CCP-ACTIVITY-20261010"


class RevisionKind(str, Enum):
    Startup = "startup"
    Recovery = "recovery"
    Interface = "interface"
    Activity = "activity"

INK = HexColor("#142737")
MUTED = HexColor("#536776")
BLUE = HexColor("#359FE7")
PALE = HexColor("#F1F6F9")
RULE = HexColor("#D6E1E8")
DOCUMENTS = {
    "cody-navigation-atlas-v1.pdf": (
        "NAVIGATION ATLAS", "Original pages 02, 04, 16 and 25; screens 04-16, 35B, 36. "
        "Earlier errata on pages 19, 23 and 26 remain unchanged."),
    "cody-planning-atlas-v1.pdf": (
        "PLANNING ATLAS", "Original pages 02 (inventory), 07 (preferences/PC setup) and 26 "
        "(A04 handoff); screens 04-16, 35B, 36. The inventory gains 40, 41 and 42."),
    "cody-ccp-pairing-quickstart-a5-v1.pdf": (
        "CCP QUICK START", "Original page 01 (ZIP installation concept) and page 02 "
        "(pairing entry and reconnect); screens 11-16. The original two-sided leaflet is retained."),
    "cody-ccp-service-atlas-v1.pdf": (
        "CCP SERVICE ATLAS", "Original pages 01 (scope), 06 (retained link) and 07 "
        "(rediscovery); screens 12-16. Existing protocol/security qualifications remain in force."),
    "cody-codex-connection-atlas-v1.pdf": (
        "CODEX CONNECTION ATLAS", "Original pages 02 (whole journey), 03 (prerequisites), "
        "04 (code), 07-08 (Idle/search), 11 (returning PC) and 13 (owners); screens 11-16."),
}


# fonts: Explicit Windows font directory containing the original atlas type family.
# Raises OSError for absent fonts; no font substitution changes the document silently.
def register_fonts(fonts):
    pdfmetrics.registerFont(TTFont("SetupBody", str(fonts / "segoeui.ttf")))
    pdfmetrics.registerFont(TTFont("SetupBold", str(fonts / "segoeuib.ttf")))


# pdf: Current revision canvas.
# text: Plain text; user/document text cannot inject paragraph markup.
# x: Left edge in points.
# top: Top edge in points, measured down from the page top.
# width: Bounded paragraph width.
# page_height: Original page height in points.
# size: Embedded body font size.
# bold: Selects the explicitly registered bold face.
# color: Text ink.
# Raises ValueError when a paragraph leaves the printable page.
def paragraph(pdf, text, x, top, width, page_height, size=11, bold=False, color=INK):
    style = ParagraphStyle("setup", fontName="SetupBold" if bold else "SetupBody",
                           fontSize=size, leading=size * 1.4, textColor=color)
    block = Paragraph(escape(text), style)
    _, height = block.wrap(width, page_height)
    if top + height > page_height - 52:
        raise ValueError(f"Revision paragraph overflows page: {text[:60]}")
    block.drawOn(pdf, x, page_height - top - height)
    return top + height


# pdf: Canvas using the original document dimensions.
# dimensions: Original width/height, including the A5 quick start.
# label: Exact atlas identity.
# title: Revision-page title.
# number: Physical page number, continuing the preserved originals.
# subtitle: Short explanation of the page's purpose.
# revision_id: Stable identity of the explicitly selected addendum.
# Raises ValueError for title/subtitle overflow.
def heading(pdf, dimensions, label, title, number, subtitle, revision_id=REVISION_ID):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    paragraph(pdf, "CODY / " + label + " / SETUP REVISION", margin, 22,
              width - 2 * margin, height, 8, True, MUTED)
    paragraph(pdf, title, margin, 58, width - 2 * margin, height,
              24 if width < 500 else 29, True)
    paragraph(pdf, subtitle, margin, 108, width - 2 * margin, height, 10, False, MUTED)
    pdf.setStrokeColor(RULE)
    pdf.line(margin, 47, width - margin, 47)
    pdf.setFillColor(MUTED)
    pdf.setFont("SetupBody", 7)
    pdf.drawString(margin, 32, "Umberto Giacobbi | hello@umbertogiacobbi.biz | confidential")
    pdf.drawString(margin, 20, revision_id + " | Physical review pending")
    pdf.drawRightString(width - margin, 20, str(number))


# pdf: Revision canvas; the diagram is illustrative and contains no actual device pixels.
# x: Center of the circular screen.
# top: Screen center distance from page top.
# radius: Diagram radius in points.
# page_height: Original page height.
# title: Printable screen caption.
# upper: First capsule label.
# lower: Second capsule label.
# back: Shows the separate left previous-route control only when valid.
def illustrative_screen(pdf, x, top, radius, page_height, title, upper, lower, back=False):
    y = page_height - top
    scale = radius / 233
    pdf.setFillColor(HexColor("#000000"))
    pdf.circle(x, y, radius, stroke=0, fill=1)
    pdf.setFillColor(HexColor("#FFFFFF"))
    for dx, dy, r in ((-44, -5, 33), (-12, 15, 43), (34, 9, 40), (63, -7, 27)):
        pdf.circle(x + dx * scale, y + (73 + dy) * scale, r * scale, stroke=0, fill=1)
    pdf.setFillColor(INK)
    for dx in (-24, 24):
        pdf.circle(x + dx * scale, y + 75 * scale, 5 * scale, stroke=0, fill=1)
    pdf.setFont("SetupBold", 26 * scale)
    pdf.setFillColor(HexColor("#FFFFFF"))
    pdf.drawCentredString(x, y - 94 * scale, title)
    for logical_top, text in ((346, upper), (390, lower)):
        pdf.setFillColor(BLUE)
        bottom = y + (233 - logical_top - 40) * scale
        pdf.roundRect(x - 120 * scale, bottom, 240 * scale, 40 * scale, 20 * scale, stroke=0, fill=1)
        pdf.setFillColor(INK)
        pdf.setFont("SetupBold", 18 * scale)
        pdf.drawCentredString(x, bottom + 14 * scale, text)
    if back:
        pdf.setStrokeColor(HexColor("#FFFFFF"))
        pdf.setLineWidth(3 * scale)
        left = x - 193 * scale
        pdf.line(left + 4 * scale, y + 8 * scale, left - 4 * scale, y)
        pdf.line(left - 4 * scale, y, left + 4 * scale, y - 8 * scale)


# pdf: Revision canvas.
# dimensions: Preserved source page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def startup_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Startup: two saved decisions", number,
            "Local Wi-Fi/IP first. Then the retained Codex pairing. Public Internet is not required.")
    text_width = width - 2 * margin if compact else 478
    top = 161
    cases = [
        ("1 / No saved Wi-Fi profile", "A valid absent-profile observation opens 04 > 05 > 06 > 07 > 09. "
         "Debug may show 39 before 09. Continue: 10 > 36 > 11 (skin), then the Codex decision."),
        ("2 / Saved Wi-Fi; waiting for the network", "42 / No connection keeps the profile while the Wi-Fi owner "
         "reconnects. Network setup reopens 04-09; Later returns to Idle. Invalid/stale data shows UNAVAILABLE."),
        ("3 / LAN ready; no saved Codex pairing", "40 / Connect Codex? Yes starts the code on 12. "
         "No returns to 16 / Idle without a settings write or CCP request. The next boot may ask again."),
        ("4 / LAN ready; saved Codex pairing", "Adopt the saved attempt and search on 14. Only authenticated "
         "READY enters 15. A retained identity alone never means the PC is connected."),
    ]
    size = 9.5 if compact else 11
    for title, body in cases:
        top = paragraph(pdf, title, margin, top, text_width, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 3, text_width, height, size) + (10 if compact else 15)
    if not compact:
        illustrative_screen(pdf, 660, 274, 88, height, "Connect Codex?", "Yes", "No")
        paragraph(pdf, "40 / Explicit Yes or No", 558, 381, 225, height, 12, True)
        paragraph(pdf, "Illustrative UI. Choice controls do not start pairing until the user acts.",
                  558, 407, 225, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def setup_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Reopen setup from Cody", number,
            "Hold the visible character still for 1.5 seconds. No button-based factory reset is added.")
    content_width = width - 2 * margin if compact else 470
    top = 163
    rows = [
        ("41 / Setup", "Connect Codex opens a fresh display-code attempt. Network setup reopens 04-09. "
         "Left Back returns to Idle. Opening or leaving the menu erases no Wi-Fi, skin or pairing."),
        ("Keep the gesture deliberate", "Available from stable Idle, PC search/connected and terminal CCP errors. "
         "Moving more than 12 px, a sample gap over 250 ms, rotation or a lifecycle interruption cancels the hold. "
         "Busy work and active provisioning are excluded; release before choosing."),
        ("PWR / existing policy", "Single: standby/wake; active Debug defaults to screenshot countdown. "
         "Long release: standby. Debug double within 500 ms, or a continuous 3-second hold: restart preserving data."),
        ("BOOT / existing policy", "Short: restore a dim/sleeping panel first, otherwise toggle fullscreen/status arcs. "
         "Hold 3 seconds: Debug restart; Release has no firmware reset action. Saved Wi-Fi and CCP survive restart."),
    ]
    size = 9.5 if compact else 11
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, content_width, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 3, content_width, height, size) + 11
    if not compact:
        illustrative_screen(pdf, 660, 273, 88, height, "Setup", "Connect Codex", "Network setup", True)
        paragraph(pdf, "41 / Reversible setup access", 558, 383, 225, height, 12, True)
        paragraph(pdf, "Illustrative UI. A true factory erase is a separate explicitly requested operation.",
                  558, 410, 225, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# number: Next physical page number.
# Raises ValueError for content overflow.
def install_page(pdf, dimensions, label, number):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "CCP: current local installation", number,
            "This checkout provides a local marketplace. The original ZIP leaflet remains a packaging concept.")
    top = 165
    rows = [
        ("1 / Register the checkout", "Use this repository's .agents/plugins/marketplace.json. The catalog is "
         "Cody local plugins; the package is CCP - Cody's Codex Plugin. Keep the checkout in its current location."),
        ("2 / Install, enable, new chat", "Open the repository installation link below in Codex desktop. Install "
         "and enable CCP, then start a new chat. Python 3 must be available as python. Codex starts the MCP script."),
        ("3 / Pair from Cody", "At startup, choose Yes on 40; alternatively hold Cody and select Connect Codex on 41. "
         "Keep the six-character code on 12 visible. In the CCP-enabled chat, ask to pair Cody using that exact code."),
        ("4 / Confirm and reconnect", "After authenticated completion, 13 > Let's go > 16 (10 active seconds) > "
         "14 > 15. On a later boot with a valid saved link, go straight to authenticated search on 14. "
         "Keep Codex/CCP awake on the same LAN; screen 15 certifies CCP presence, not chat/task transport."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, 10.5, True)
        top = paragraph(pdf, body, margin, top + 3, width - 2 * margin, height, 9.5) + 12
    url = ("codex://plugins/cody-codex-plugin?marketplacePath="
           "E%3A%2FESP32%20Luna%20Shell%2F.agents%2Fplugins%2Fmarketplace.json")
    pdf.setFillColor(BLUE)
    pdf.setFont("SetupBold", 10)
    y = height - top - 12
    pdf.drawString(margin, y, "Open CCP installation in Codex")
    pdf.linkURL(url, (margin, y - 3, margin + 240, y + 12), relative=0)
    pdf.showPage()


# pdf: Revision canvas.
# dimensions: Original page dimensions.
# label: Exact document identity.
# affected: Original physical page/screen IDs whose statements this revision supersedes.
# number: Final physical page number.
# original_count: Preserved original page count, including earlier errata.
# Raises ValueError for content overflow.
def errata_page(pdf, dimensions, label, affected, number, original_count):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "Errata and corrections", number,
            "Revision date: " + REVISION + " / " + REVISION_ID)
    top = 162
    rows = [
        ("Original analysis gap", "The initial analysis omitted startup with Wi-Fi already configured but no "
         "saved Codex pairing, and did not define a reversible user entry to setup. This was an original analysis "
         "gap (buco di analisi iniziale), now addressed in source."),
        ("Affected original pages and screen IDs", affected),
        ("Corrections", "Add 40 / optional Yes-No Codex choice, 41 / character-hold Setup, and 42 / reconnect wait. "
         "09 means local Wi-Fi/IP; 11 selects a skin. No/Later performs no persistence or new CCP request. "
         "Saved pairing returns to 14 and requires authenticated READY for 15. PWR/BOOT do not clear pairing."),
        ("Preservation", f"All {original_count} previous pages, earlier corrections, bookmarks and unrelated "
         "content are retained. This dated addendum supersedes only the cited startup/setup/install statements; "
         "the original numbering stays stable."),
        ("Evidence and limits", "Firmware source implementation and static integration review completed. "
         "PDF pages rendered and visually reviewed. No firmware tests, validation scans, configure/build/size, "
         "simulator execution, device pairing/input, flash or erase accompanied this revision. Runtime validation is pending."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, 10.5, True)
        top = paragraph(pdf, body, margin, top + 3, width - 2 * margin, height,
                        9 if width < 500 else 11) + 11
    pdf.showPage()


# pdf: Revision canvas, preserving the original atlas page dimensions.
# dimensions: Original width and height, including the A5 leaflet.
# label: Canonical document identity.
# number: Continued physical page number.
# Raises ValueError when the explanation leaves the printable page.
def recovery_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Setup after standby", number,
            "Hold Cody for 1.5 seconds to choose Network setup or Connect Codex.", RECOVERY_REVISION_ID)
    content_width = width - 2 * margin if compact else 470
    top = 163
    rows = [
        ("04-06 / Escape the network loop", "After inactivity, Welcome previously sent its first touch straight "
         "back to Let's pair. Welcome now opens 41 on a stationary tap release. A character hold also opens "
         "41 from timeout Welcome, Let's pair or the installation QR page when no operation is busy."),
        ("35B / Wake, then hold", "A finger already down during wake starts its 1.5-second hold only on a "
         "fresh acquired sample with the restored visible character. Hidden standby/wake time never counts. "
         "Movement over 12 px, report gaps over 250 ms or rotation cancel the hold. Release before choosing."),
        ("41 / Two explicit destinations", "Upper Connect Codex opens 12 and its six-character CCP code "
         "using the local Wi-Fi/IP connection. Lower Network setup explicitly opens 04-09. No Internet probe "
         "is required. Active provisioning, worker admission and pending cancellation block menu access."),
    ]
    size = 9.5 if compact else 11
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, content_width, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 3, content_width, height, size) + 12
    if not compact:
        illustrative_screen(pdf, 660, 273, 88, height, "Setup", "Connect Codex", "Network setup", True)
        paragraph(pdf, "41 / Same choices after wake", 558, 383, 225, height, 12, True)
        paragraph(pdf, "Illustrative UI. Existing credentials and retained CCP pairing are preserved.",
                  558, 410, 225, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Revision canvas; this is always the final appended page.
# dimensions: Preserved original page dimensions.
# label: Canonical atlas identity.
# number: Continued final physical page number.
# original_count: All preserved previous pages, including earlier errata.
# setup_page_number: Previous addendum page whose gesture scope is superseded.
# Raises ValueError when dated corrections overflow the page.
def recovery_errata_page(pdf, dimensions, label, number, original_count, setup_page_number):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "Errata and corrections", number,
            "Revision date: " + REVISION + " / recovery correction", RECOVERY_REVISION_ID)
    top = 162
    rows = [
        ("Original analysis gap", "The first analysis did not reconcile the legacy Welcome timeout/touch "
         "path with reopening setup after standby. This was an original analysis gap (buco di analisi iniziale)."),
        ("Affected pages and screens", f"Previous setup addendum: page {setup_page_number}. "
         "Affected screen IDs: 04, 05, 06, 12, 35B and 41. Earlier dated errata remain intact."),
        ("Corrections", "Timeout Welcome tap release and stationary character holds on resting 04-06 reopen "
         "41. A hold spanning wake starts only after visible restoration. Connect Codex goes to 12; "
         "Network setup goes to 04-09. Busy work and pending cleanup cannot be bypassed."),
        ("Evidence and limits", "A user-authorized 90-second passive serial capture confirmed LAN/DHCP, "
         "Welcome inactivity return and standby. No Connect Codex press occurred in that capture; the "
         "initial wrong-selection report remains unresolved. The correction has static source review; "
         "corrected physical behavior still needs observation."),
        ("Preservation", f"All {original_count} preceding pages, prior errata and unrelated content are retained. "
         "This addendum supersedes only the cited escape/gesture behavior. Saved credentials are not erased."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, 10.5, True)
        top = paragraph(pdf, body, margin, top + 3, width - 2 * margin, height,
                        9 if width < 500 else 11) + 11
    pdf.showPage()


# pdf: New revision canvas retaining the source document dimensions.
# dimensions: Original page width and height.
# label: Canonical document identity.
# number: Continued physical page number.
# Raises OSError for absent approved references and ValueError for layout overflow.
def interface_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Cody action language", number,
            "IMPORTANT UI DECISION / approved 09 October 2026 / binding for future screens",
            INTERFACE_REVISION_ID)
    image_size = 164 if compact else 218
    image_top = 160
    gap = (width - 2 * margin - 2 * image_size) / 3
    images = ROOT / "docs/Cody/ui-proposals/20261009"
    for offset, name in enumerate(("selected-multi-action.png", "selected-single-action.png")):
        x = margin + gap + offset * (image_size + gap)
        pdf.drawImage(str(images / name), x, height - image_top - image_size,
                      image_size, image_size, preserveAspectRatio=True, mask="auto")
    top = image_top + image_size + 12
    size = 9 if compact else 11
    rows = [
        ("Layout rule", "Two actions: rounded cards side by side. Three: the same pair plus one centered "
         "action below. One: a wide centered capsule with its icon left of the label."),
        ("Shared visual language", "Dark restrained gradients, thin semantic borders, white labels and action "
         "icons. Teal: affirmative/Codex; blue: network; graphite: No/Later/Options; amber: Retry/Cancel."),
        ("Circular geometry", "Keep Back separate at the left. Share rendering and touch bounds in every "
         "orientation. QR pages keep the quiet zone and use the compact single-action variant."),
    ]
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 2, width - 2 * margin, height, size) + 8
    pdf.showPage()


# pdf: New revision canvas.
# dimensions: Original page size, including A5.
# label: Canonical document identity.
# number: Continued physical page number.
# Raises ValueError for page overflow.
def interface_flow_page(pdf, dimensions, label, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    heading(pdf, dimensions, label, "Options and touch continuity", number,
            "Setup keeps Network setup and Connect Codex distinct. Options opens screen 10.",
            INTERFACE_REVISION_ID)
    rows = [
        ("41 / Setup", "Left card: Connect Codex. Right card: Network setup. Centered third action: Options. "
         "Options opens 10 / Make it yours, then 36 and 11 / skin. Done returns to Setup; Back retraces this "
         "local preferences path. No network or Codex association is erased or implicitly started."),
        ("40 / Connect Codex? and 42 / No connection", "Yes/No and Network setup/Later are side by side, "
         "with purpose-specific icons and colors. Their existing explicit backend admission stays intact. "
         "Screen IDs 05-15 and 36 share the action styling where an action is present."),
        ("Observed freeze / original cause", "An authorized 90-second reset/capture recorded Yes at 16497 ms, "
         "entry to Connect Codex at 16498 ms, then a CST9217 report with fifteen FF bytes and ACK=FF at "
         "17112 ms (expected AB). ESP_ERR_INVALID_RESPONSE stopped the renderer. LAN/IP was ready."),
        ("Scoped runtime correction", "The touch owner still rejects malformed reports and publishes the "
         "real error. After startup only, that acquisition error is optional for rendering. Discard queued "
         "gesture reports, cancel the gesture and require a real acquired release before another press. "
         "No release is invented. The same acquisition error can mean an I2C NACK; its origin is not inferred. "
         "Startup, other acquisition errors, locks and route failures remain terminal."),
        ("Evidence boundary", "Native source integration and approved mockups are documented here. "
         "The underlying cause of the FF report and corrected physical touch/standby behavior remain "
         "unverified. A successful build/flash alone does not establish those outcomes."),
    ]
    top = 159
    size = 8.7 if compact else 11
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 2, width - 2 * margin, height, size) + 10
    pdf.showPage()


# pdf: Revision canvas; this page is always appended last.
# dimensions: Original document page size.
# label: Canonical atlas identity.
# affected: Relevant original page/screen IDs.
# number: Continued physical page number.
# original_count: Number of preserved preceding pages, including older errata.
# Raises ValueError for overflowing dated corrections.
def interface_errata_page(pdf, dimensions, label, affected, number, original_count):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    heading(pdf, dimensions, label, "Errata and corrections", number,
            "Revision date: " + REVISION + " / important UI decision and touch correction",
            INTERFACE_REVISION_ID)
    rows = [
        ("Important design decision", "The user approved the two-column pair plus centered third action, "
         "and the wide single action with icon to the left. These are binding layouts from this revision "
         "onward. Gradients, icons and semantic colors are required across Cody screens."),
        ("Affected original material", affected + " Also affected: screens 05-15, 35B, 36 and 40-42. "
         "Earlier stacked-blue control illustrations are superseded only in their action layout/styling."),
        ("Original analysis gaps", "The initial analysis omitted Options re-entry and did not distinguish "
         "an optional malformed runtime touch report from renderer failure. This was a buco di analisi "
         "iniziale. Options now opens 10 and returns to Setup after preferences/skin."),
        ("Corrections and limits", "Yes/No become horizontal and semantic. The measured invalid touch "
         "report retains its original error while rendering continues with interrupted gestures. "
         "Hardware FF root cause and physical corrected behavior remain open."),
        ("Preservation", f"All {original_count} preceding pages, earlier errata and unrelated content remain "
         "unchanged. Approved images are design references, not physical device screenshots."),
    ]
    top = 159
    size = 9 if width < 500 else 11
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 2, width - 2 * margin, height, size) + 11
    pdf.showPage()


# pdf: Canvas retaining the atlas's existing page size and typography.
# dimensions: Original A4/A5 page dimensions.
# label: Exact atlas identity.
# number: Continued physical page number.
# Raises ValueError if a bounded workflow paragraph overflows its page.
def activity_page(pdf, dimensions, label, number):
    width, height = dimensions
    margin = 26 if width < 500 else 34
    size = 8.5 if width < 500 else 11
    heading(pdf, dimensions, label, "Follow one Codex chat", number,
            "10 October 2026 / CCP 0.3 / screens 16-20 and 32", ACTIVITY_REVISION_ID)
    box_width = (width - margin * 2 - 20) / 3
    for index, text in enumerate(("Codex hooks", "CCP runtime", "Cody snapshot")):
        left = margin + index * (box_width + 10)
        pdf.setFillColor(PALE)
        pdf.roundRect(left, height - 190, box_width, 35, 8, stroke=0, fill=1)
        pdf.setFont("SetupBold", size + 1)
        pdf.setFillColor(INK)
        pdf.drawCentredString(left + box_width / 2, height - 176, text)
    rows = [
        ("Install and trust", "Update CCP to 0.3. Review and trust its hooks in Codex. "
         "Installation alone does not trust them. The hidden PC runtime is shared by open chats; "
         "the saved device pairing remains intact."),
        ("Choose the chat", "Send a prompt, list observed sessions, then explicitly select one "
         "session ID and paired Cody. Supply a short ASCII project label. Another chat cannot "
         "replace that selection. Runtime restart requires selection again."),
        ("Read actual activity", "17: project active. 18: working, editing or running. "
         "19: response ready, not task success. 20A: paused or awaiting approval. "
         "20B: session ended, then 16 after five active seconds. 32: paired PC lost."),
        ("Freshness and scope", "The device receives authenticated snapshots every two seconds. "
         "Missing packets expire after eight seconds; active hook observations expire after "
         "120 seconds. UNAVAILABLE is explicit. Messages, answers, PR data, branch acquisition "
         "and voice transcription remain future work. Physical behavior is pending."),
    ]
    top = 207
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 2, width - 2 * margin, height, size) + 9
    pdf.showPage()


# pdf: Revision canvas; this dated errata page is always last.
# dimensions: Preserved original page dimensions.
# label: Canonical atlas identity.
# name: Source filename used only to select affected original page IDs.
# number: Continued physical page number.
# original_count: Number of preserved pages, including all earlier errata.
# Raises ValueError for layout overflow.
def activity_errata_page(pdf, dimensions, label, name, number, original_count):
    affected = {
        "cody-navigation-atlas-v1.pdf": "Original pages 5-6 and 8; screens 15-20, 26-27, 31-32.",
        "cody-planning-atlas-v1.pdf": "Original pages 9-10 and 25; screens 17-20, 21-24 and 32.",
        "cody-ccp-service-atlas-v1.pdf": "Original pages 1-2, 7-8 and 10; scope and runtime ownership.",
        "cody-codex-connection-atlas-v1.pdf": "Original pages 10-11 and 13; screen 15 and reconnect ownership.",
        "cody-ccp-pairing-quickstart-a5-v1.pdf": "Original pages 1-2 and setup addendum page 5; installation and screen 15.",
    }[name]
    width, height = dimensions
    margin = 26 if width < 500 else 34
    size = 8.5 if width < 500 else 11
    heading(pdf, dimensions, label, "Errata and corrections", number,
            "Revision date: 10 October 2026 / authenticated chat activity", ACTIVITY_REVISION_ID)
    rows = [
        ("Affected original material", affected),
        ("Analysis gap corrected", "Screen 15 originally proved PC presence only. The initial "
         "analysis omitted explicit chat selection, shared runtime lifetime and event freshness. "
         "This was a buco di analisi iniziale; it is now addressed in source."),
        ("Contract and UI", "CCP 0.3 adds metadata-only hooks, exact session selection and "
         "nonce-bound ACTIVITY messages. The canonical snapshot owns validity and errors. "
         "18U adds the unavailable state. Setup re-entry preserves the selected context."),
        ("Evidence and limits", "The user confirmed Connected on 10 October after real pairing. "
         "That evidence belongs to the previous firmware and CCP 0.2. New code is implemented; "
         "host checks, hook trust/autoload and physical 0.3 behavior are not established here. "
         "A response-ready event is never reported as task success."),
        ("Preservation", f"All {original_count} previous pages and earlier errata remain unchanged. "
         "The approved gradient/icon action layouts remain binding. These diagrams describe "
         "architecture, not captured device pixels."),
    ]
    top = 159
    for title, body in rows:
        top = paragraph(pdf, title, margin, top, width - 2 * margin, height, size + 1, True)
        top = paragraph(pdf, body, margin, top + 2, width - 2 * margin, height, size) + 9
    pdf.showPage()


# revision: Explicit revision identity; no automatic migration or duplicate append.
def selected_revision_id(revision):
    return {RevisionKind.Startup: REVISION_ID, RevisionKind.Recovery: RECOVERY_REVISION_ID,
            RevisionKind.Interface: INTERFACE_REVISION_ID, RevisionKind.Activity: ACTIVITY_REVISION_ID}[revision]


# name: One explicitly requested source PDF filename.
# label: Canonical atlas label.
# affected: Original affected page/screen IDs.
# staging: Repository-local artifact directory for preserved originals and authoring output.
# revision: Explicit typed revision; each can be appended exactly once.
# Raises OSError or ValueError for missing/ambiguous inputs; publishes only fully authored PDFs.
def revise(name, label, affected, staging, revision):
    source = ROOT / "docs/cody" / name
    reader = PdfReader(source)
    revision_id = selected_revision_id(revision)
    if any(revision_id in (page.extract_text() or "") for page in reader.pages):
        raise ValueError(f"Revision {revision_id} already exists in {name}; do not duplicate it")
    dimensions = tuple(float(value) for value in (reader.pages[0].mediabox.width, reader.pages[0].mediabox.height))
    original_count = len(reader.pages)
    staging.mkdir(parents=True, exist_ok=True)
    backup = staging / (source.stem + "-original.pdf")
    if backup.exists():
        raise ValueError(f"Preserved original already exists: {backup}")
    backup.write_bytes(source.read_bytes())
    buffer = BytesIO()
    pdf = canvas.Canvas(buffer, pagesize=dimensions, pageCompression=1)
    pdf.setAuthor("Umberto Giacobbi")
    if revision == RevisionKind.Activity:
        activity_page(pdf, dimensions, label, original_count + 1)
        activity_errata_page(pdf, dimensions, label, name, original_count + 2, original_count)
        added = 1
    elif revision == RevisionKind.Interface:
        interface_page(pdf, dimensions, label, original_count + 1)
        interface_flow_page(pdf, dimensions, label, original_count + 2)
        interface_errata_page(pdf, dimensions, label, affected, original_count + 3, original_count)
        added = 2
    elif revision == RevisionKind.Recovery:
        recovery_page(pdf, dimensions, label, original_count + 1)
        setup_page_number = original_count - (2 if "quickstart" in name else 1)
        recovery_errata_page(pdf, dimensions, label, original_count + 2, original_count, setup_page_number)
        added = 1
    else:
        startup_page(pdf, dimensions, label, original_count + 1)
        setup_page(pdf, dimensions, label, original_count + 2)
        added = 2
        if "quickstart" in name:
            install_page(pdf, dimensions, label, original_count + 3)
            added += 1
        errata_page(pdf, dimensions, label, affected, original_count + added + 1, original_count)
    pdf.save()
    buffer.seek(0)
    writer = PdfWriter()
    writer.clone_document_from_reader(reader)
    writer.append(PdfReader(buffer), import_outline=False)
    staged = staging / name
    with staged.open("wb") as output:
        writer.write(output)
    print(f"{name}: {original_count} -> {original_count + added + 1} pages; earlier content preserved")
    return source, staged


# argv: Literal authoring arguments forwarded by the sole public device.ps1 entrypoint.
# Raises OSError for unreadable documents/fonts and ValueError for duplicate/overflow revisions.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, default=Path("C:/Windows/Fonts"))
    parser.add_argument("--revision", choices=[value.value for value in RevisionKind], default=RevisionKind.Startup.value)
    parser.add_argument("--staging", type=Path)
    args = parser.parse_args(argv)
    revision = RevisionKind(args.revision)
    revision_id = selected_revision_id(revision)
    staging = (args.staging or ROOT / "artifacts/docs" / revision_id.lower()).resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF staging must stay under the repository artifacts directory")
    register_fonts(args.font_dir)
    prepared = [revise(name, label, affected, staging, revision)
                for name, (label, affected) in DOCUMENTS.items()]
    # Complete every requested revision before changing any of the original documents.
    for source, staged in prepared:
        source.write_bytes(staged.read_bytes())


if __name__ == "__main__":
    main()
