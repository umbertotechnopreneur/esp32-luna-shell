"""Author the five current Cody references without accumulating revision history."""

import argparse
from io import BytesIO
from pathlib import Path
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape

import pymupdf
from PIL import Image, ImageDraw
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, A5, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, register_fonts, render_revision_register
import generate_cody_pairing_flyer as flyer

DOCS = ROOT / "docs/cody"
REVIEW = ROOT / "artifacts/docs/current"
MIRROR = Path("C:/Users/umber/OneDrive/Obsidian/30_Engineering/Products/Cody Companion/atlas")
DOCUMENT_NAMES = (
    "cody-ccp-connection-atlas-v1.pdf", "cody-navigation-atlas-v1.pdf",
    "cody-planning-atlas-v1.pdf",
    "cody-ccp-pairing-quickstart-a5-v1.pdf", "device-navigation-map-a4.pdf",
)
SIZE = landscape(A4)
W, H = SIZE
PREVIOUS_EDITION = "09 November 2026"
DATE = "12 November 2026"
INK = colors.HexColor("#14283E")
MUTED = colors.HexColor("#506579")
TEAL = colors.HexColor("#007F83")
PALE = colors.HexColor("#EFF5F8")
LINE = colors.HexColor("#D6E2EA")
WHITE = colors.white

CURRENT_SCOPE = (
    "Current repository source is the reference. Firmware execution, physical behavior and the private cloud service require their own verification. "
    "After VibeWare, Home can show Counter mode or the interactive Muse avatar. Cloud firmware integration is deferred."
)
SOURCE_NOTE = "Sources: docs/cody/navigation.md, layout.md, runtime.md; cody-desktop/plugins/cody-codex-plugin/README.md and protocol/CCP1.md."


# text: Literal prose; markup is escaped before layout.
# width: Available paragraph width in points.
# size: Font size in points.
# color: Foreground color.
# bold: Select the shared bold typeface.
def paragraph(text, width, size=11, color=INK, bold=False):
    style = ParagraphStyle("copy", fontName="CodyDocBold" if bold else "CodyDoc",
                           fontSize=size, leading=size * 1.35, textColor=color)
    block = Paragraph(escape(text), style)
    height = block.wrap(width, H)[1]
    return block, height


# surface: Destination canvas.
# text: Complete literal prose.
# x: Left edge in points.
# top: Top edge measured down from the page top.
# width: Available text width.
# size: Font size in points.
# color: Foreground color.
# bold: Select bold typography.
def copy(surface, text, x, top, width, size=11, color=INK, bold=False):
    block, height = paragraph(text, width, size, color, bold)
    block.drawOn(surface, x, H - top - height)
    return top + height


# title: Current document heading.
# subtitle: Bounded scope and interpretation.
# label: Document identity for headers and footers.
# Raises ValueError when the subtitle cannot fit the reserved header.
def page_start(title, subtitle, label):
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=SIZE, pageCompression=1)
    surface.setFillColor(TEAL)
    surface.rect(0, H - 10, W, 10, fill=1, stroke=0)
    copy(surface, "CODY / " + label.upper(), 38, 27, W - 76, 8.5, TEAL, True)
    copy(surface, title, 38, 51, W - 76, 25, INK, True)
    end = copy(surface, subtitle, 38, 92, W - 76, 10.5, MUTED)
    if end > 135:
        raise ValueError("Page subtitle exceeds its reserved space: " + title)
    surface.setStrokeColor(LINE)
    surface.line(38, 37, W - 38, 37)
    copy(surface, "Cody / VibeWare / " + DATE + " / Source reference; physical verification is separate", 38, H - 27, W - 76, 7.5, MUTED)
    return output, surface


# output: In-memory PDF bytes owner.
# surface: Finished canvas with one authored page.
def page_finish(output, surface):
    surface.showPage()
    surface.save()
    return output.getvalue()


# title: Main page heading.
# subtitle: Current scope.
# items: Two to six independent label/body pairs.
# label: Document identity.
# note: Optional source or evidence note below the card grid.
# Raises ValueError for an invalid card count or overflowing content.
def cards_page(title, subtitle, items, label, note=SOURCE_NOTE):
    if not 2 <= len(items) <= 6:
        raise ValueError("A reference grid needs two to six cards")
    output, surface = page_start(title, subtitle, label)
    columns = 2 if len(items) <= 4 else 3
    rows = (len(items) + columns - 1) // columns
    gap = 17
    width = (W - 76 - gap * (columns - 1)) / columns
    height = 168 if rows == 2 else 320
    for index, (heading, body) in enumerate(items):
        x = 38 + (index % columns) * (width + gap)
        top = 150 + (index // columns) * (height + 15)
        surface.setFillColor(PALE)
        surface.roundRect(x, H - top - height, width, height, 10, fill=1, stroke=0)
        end = copy(surface, heading, x + 15, top + 13, width - 30, 13, TEAL, True)
        end = copy(surface, body, x + 15, end + 10, width - 30, 10.5)
        if end > top + height - 10:
            raise ValueError("Card text exceeds its bounds: " + heading)
    copy(surface, note, 38, 520, W - 76, 8, MUTED)
    return page_finish(output, surface)


# title: Document identity.
# subtitle: Purpose and current scope.
def cover(title, subtitle):
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=SIZE)
    surface.setFillColor(INK)
    surface.rect(0, 0, W, H, fill=1, stroke=0)
    surface.setStrokeColor(colors.HexColor("#24465B"))
    for radius in (130, 210, 290):
        surface.circle(W - 95, H - 120, radius, fill=0, stroke=1)
    copy(surface, "CODY / VIBEWARE", 55, 58, W - 110, 14, colors.HexColor("#65DCCE"), True)
    logo = ROOT / "cody-device/simulator/windows/assets/branding/vibeware-logo.png"
    surface.drawImage(str(logo), 55, 315, width=155, height=95, preserveAspectRatio=True, mask="auto")
    copy(surface, title, 55, 292, W - 110, 34, WHITE, True)
    copy(surface, subtitle, 55, 349, W - 150, 14, colors.HexColor("#D6E4EF"))
    copy(surface, "DEVELOPMENT REFERENCE / " + DATE, 55, 445, W - 110, 10, colors.HexColor("#65DCCE"), True)
    copy(surface, "Umberto Giacobbi / umbertogiacobbi.biz / Copyright (c) 2026", 55, 486, W - 110, 10, WHITE)
    copy(surface, "Current behavior and limitations. Original correction dates and screen IDs remain in the final register.", 55, 517, W - 110, 9, colors.HexColor("#D6E4EF"))
    return page_finish(output, surface)


# entries: Final headings paired with one-based PDF page numbers.
# label: Current document identity.
# Raises ValueError when the section list cannot fit the contents page.
def contents(entries, label):
    output, surface = page_start("Contents", "Use screen IDs for product identity; PDF page numbers are navigation only.", label)
    for index, (title, number) in enumerate(entries):
        top = 150 + index * 27
        if top > 505:
            raise ValueError("Contents requires another page")
        copy(surface, title, 40, top, W - 120, 11)
        copy(surface, str(number), W - 70, top, 30, 11, TEAL, True)
        surface.setStrokeColor(LINE)
        surface.line(40, H - top - 22, W - 40, H - top - 22)
    return page_finish(output, surface)


# document: PDF document receiving new pages.
# data: Complete PDF bytes to append.
def append_pdf(document, data):
    with pymupdf.open(stream=data, filetype="pdf") as addition:
        document.insert_pdf(addition)


# label: Document identity.
# title: Reader-facing title.
# subtitle: Current document scope.
# chapters: Ordered heading/PDF-byte pairs.
# records: Retained dated corrections, without build or code history.
# security: Security corrections and active limitations.
def atlas(label, title, subtitle, chapters, records, security=()):
    document = pymupdf.open()
    append_pdf(document, cover(title, subtitle))
    document.new_page(width=W, height=H)
    entries = []
    for heading, data in chapters:
        entries.append((heading, len(document) + 1))
        append_pdf(document, data)
    main_page_count = len(document)
    if records:
        entries.append(("Corrections register", len(document) + 1))
        append_pdf(document, render_revision_register(records, SIZE, "Cody / " + label,
                   start_page=len(document) + 1, section_title="Corrections register"))
    if security:
        entries.append(("Security", len(document) + 1))
        append_pdf(document, render_revision_register(security, SIZE, "Cody / " + label,
                   start_page=len(document) + 1, section_title="Security"))
    document.delete_page(1)
    with pymupdf.open(stream=contents(entries, label), filetype="pdf") as index:
        document.insert_pdf(index, start_at=1)
    document.set_toc([[1, "Cover", 1], [1, "Contents", 2]] + [[1, heading, number] for heading, number in entries])
    for index, (heading, number) in enumerate(entries):
        document[1].insert_link({"kind": pymupdf.LINK_GOTO,
            "from": pymupdf.Rect(38, 148 + index * 27, W - 38, 174 + index * 27), "page": number - 1})
    for index, page in enumerate(document):
        if 0 < index < main_page_count:
            page.insert_text((W - 64, H - 16), f"{index + 1} / {len(document)}", fontsize=7, color=(.31, .40, .47))
    document.set_metadata({"title": "Cody / " + title, "author": "Umberto Giacobbi",
        "subject": "Current development reference; cloud integration pending", "keywords": "CodyCurrentReference/1"})
    result = document.tobytes(garbage=4, deflate=True)
    document.close()
    return result


# label: Atlas identity.
def cloud_map(label):
    output, surface = page_start("Cloud relay: three separate identities", "Architecture under implementation. No live service or cloud-capable firmware is claimed.", label)
    nodes = [
        ("Owner", "Signs in through the configured identity provider. Claims the intended device and grants one client access."),
        ("Dot / client", "Uses a scoped access token or a separately issued connection key. The server resolves its allowed devices."),
        ("Cody", "Initiates HTTPS polling. A separate revocable token is bound to its P-256 key through DPoP proof."),
    ]
    width = (W - 112) / 3
    for index, (heading, text) in enumerate(nodes):
        x = 38 + index * (width + 18)
        surface.setFillColor(PALE)
        surface.roundRect(x, H - 150 - 132, width, 132, 10, fill=1, stroke=0)
        end = copy(surface, heading, x + 14, 165, width - 28, 15, TEAL, True)
        copy(surface, text, x + 14, end + 9, width - 28, 10.5)
        surface.setStrokeColor(TEAL)
        surface.line(x + width / 2, H - 285, x + width / 2, H - 322)
    surface.setFillColor(INK)
    surface.roundRect(38, H - 322 - 80, W - 76, 80, 10, fill=1, stroke=0)
    copy(surface, "Private relay / identity + explicit grants + bounded delivery", 55, 337, W - 110, 16, WHITE, True)
    copy(surface, "A device ID is an address, not permission. Every send, poll and acknowledgement checks the authenticated principal and its live grant.", 55, 365, W - 110, 11, WHITE)
    copy(surface, "One relay instance / state, cookie protection keys and bounded logs on an OS-mounted SMB/Samba share", 38, 430, W - 76, 12, TEAL, True)
    copy(surface, "ASP.NET Core + Aspire AppHost/dashboard + OpenTelemetry; HTTPS polling first. Aspire uses its ordinary local tooling and temporary files. Relay storage uses the mounted share without Azure SDKs or provider APIs. Builds, deployment, mount behavior and firmware/Dot integration remain unverified.", 38, 466, W - 76, 11, MUTED)
    return page_finish(output, surface)


# label: Atlas identity.
def geometry_page(label):
    output, surface = page_start("The circle and the complete touch target", "Shared source geometry for the current 466 x 466 board; schematics are not measured bezel or touch calibration.", label)
    scale = .68
    cx, cy = 205, 277
    surface.setFillColor(INK)
    surface.circle(cx, cy, 233 * scale, fill=1, stroke=0)
    surface.setStrokeColor(colors.HexColor("#65DCCE"))
    surface.rect(cx - 160 * scale, cy - 160 * scale, 320 * scale, 320 * scale, fill=0, stroke=1)
    surface.setFillColor(colors.HexColor("#6D8AA0"))
    surface.circle(cx - 193 * scale, cy, 30 * scale, fill=1, stroke=0)
    surface.setStrokeColor(WHITE)
    surface.setLineWidth(2)
    surface.lines([(cx - 190 * scale, cy + 9, cx - 200 * scale, cy), (cx - 200 * scale, cy, cx - 190 * scale, cy - 9)])
    copy(surface, "[73,393) safe square", 118, 252, 180, 11, WHITE, True)
    topics = [
        ("466 x 466", "Center (233,233), radius 233. The baseline square is 320 x 320 at (73,73). Expand only within every occupied row's chord."),
        ("Back has its own space", "Left circle at (10,203), 60 x 60. One chevron, no caption, complete touch target, only with a previous route."),
        ("Rendering and touch agree", "Use the same logical 0/90/180/270-degree transform. Never mirror text or rotate touch independently."),
        ("Controls become complete", "Text actions have no filled cards or button borders; existing icons precede their captions. Reveal controls and touch together after title/body; preserve QR quiet zones."),
    ]
    top = 150
    for heading, text in topics:
        top = copy(surface, heading, 405, top, W - 443, 13, TEAL, True)
        top = copy(surface, text, 405, top + 5, W - 443, 10.5) + 14
    copy(surface, "Sources: display-geometry.md and docs/cody/layout.md. Artwork and complete hit bounds use the same circular admission.", 38, 520, W - 76, 8, MUTED)
    return page_finish(output, surface)


# label: Atlas identity for the stable screen-reference inventory.
def identity_page(label):
    output, surface = page_start("Original screen IDs remain stable", "Identity reference / current labels and availability follow the source owners, not these original document names.", label)
    rows = [
        ("01-03", "Character boot", "VibeWare precedes Muse; the first-install birth introduction has its own pending state."),
        ("04-09 / 06-bis", "Network setup", "Installation QR, active provisioning QR, failure and valid Wi-Fi/IP."),
        ("10 / 36 / 11", "Options / Settings / Skin", "Device-first Options are current; Skin remains character design scope."),
        ("12-15", "Code / saved / searching / connected", "Local CCP pairing and current authentication are distinct states."),
        ("16 / 17 / 18 / 18U", "Idle / project / work / unavailable", "Muse renders on the device; activity needs exact source identity and freshness."),
        ("19 / 20A / 20B", "Reply ready / paused / chat ended", "A reply is not proof of success; no background event steals context."),
        ("21-24 / 25", "PR observations / retired log route", "Read-only status; exact head and complete check list; 25 remains retired."),
        ("26 / 27", "Notice / question / review", "Numbered choices, complete detail review, then explicit Send."),
        ("28 / 29 / 30", "Voice capture / transport / review", "Voice is profile-dependent; original transcript review 30 remains a future mode."),
        ("31 / 32", "Reply delivered / connection lost", "Recipient acknowledgement and actual link state; never infer success."),
        ("33 / 34", "Battery / charging notices", "Use current measured thresholds and health; no synthetic battery facts."),
        ("35A / 35B", "Sleep / Wake", "Power owner retains route; an artistic pose alone is not standby."),
        ("37 / 38 / 39", "Rig / diagnostics / provisioning debug", "Rig is Studio; diagnostic entry shares Debug/Release, with no facial-channel UI."),
        ("40 / 41 / 42", "Connection decision / Setup / recovery", "Original route identities retained; BOOT Options is the current user entry."),
    ]
    top = 148
    for index, (identity, name, meaning) in enumerate(rows):
        if index % 2 == 0:
            surface.setFillColor(PALE)
            surface.rect(38, H - top - 24, W - 76, 24, fill=1, stroke=0)
        copy(surface, identity, 45, top + 5, 135, 8.5, TEAL, True)
        copy(surface, name, 185, top + 5, 210, 8.5, INK, True)
        copy(surface, meaning, 400, top + 5, W - 445, 8, MUTED)
        top += 25
    copy(surface, "C01-C14 (cues) and IS01-IS04 (idle scenes) are document labels, not new routes. No cloud screen IDs are assigned before firmware integration.", 38, 520, W - 76, 8.5, MUTED)
    return page_finish(output, surface)


# date: Original correction date or the date of this explicit consolidation.
# kind: One canonical correction category.
# title: Concise correction identity.
# text: Substantive correction, excluding code/build chronology.
# affected: Original page and screen references.
# evidence: Preserved observation or limitation.
def record(date, kind, title, text, affected, evidence="Source/design evidence only; physical behavior was not established by this correction."):
    return RevisionRecord(date, kind, title, (text,), affected, evidence)


# No parameters: returns current corrections shared by the navigation and planning references.
def route_records():
    return [
        record("03 October 2026", "Addendum", "A01 / Installation and device QR are different",
            "An installation QR opens the provisioning app listing. A separate session QR belongs to the active provisioning attempt. Credentials and Wi-Fi/IP are distinct states. Cancel and failure wait for actual cleanup; retry starts a new attempt.",
            "Navigation originals 03, 12-13; Planning originals 04 and 23; screens 05-09 and original 06-bis."),
        record("04 October 2026", "Addendum", "A02-A04 / Stable routes, read-only PRs and real pairing",
            "The Studio skin selector keeps screen 11 and its caller across first setup and Settings. PR screens 21-24 are read-only; 25 is retired. Counts require a complete current check list for the exact head. First-boot persistence and pairing success require actual owner results; Connected cannot be inferred from a route change.",
            "Navigation originals 14-16; Planning originals 24-26; screens 04-16, 21-25, 36 and 39."),
        record("05 October 2026", "Errata", "Idle presentation is a design scope",
            "Original Idle 16 gained IS01-IS04 artistic scenes with a 20-40-second eligible wait, 3.2-second gesture and 40-second cooldown. The 35% closing, 16-22% reopening and 4.5% width pulse were subsequently corrected. These labels describe character design, not device measurements or current dashboard routes.",
            "Navigation original 19; Manga originals 03-04, 14-18; screens 04, 16, 35A/35B, 36-38."),
        record("06 October 2026", "Errata", "Character values and VibeWare introduction",
            "The retained character correction used 75% mouth closing, 16-24% reopening and 18% width motion; Stressed peak opening 0.24; cue offset +30/+40 px, 3.6x scale and 400-ms fades; Whistle width 0.5 replacing 0.42. The owner-supplied VibeWare floppy precedes content on cold boot/software reset. Former character boot phases and twofold artwork geometry are superseded by current device-first behavior.",
            "Navigation originals 23 and 26; Manga originals 19-27; screens 01-05, 16, 35A/35B.",
            "Windows rig illustrations and source review; no physical visual/audio observation was established. Build/check chronology is omitted."),
        record("09 October 2026", "Errata", "Saved state and reversible setup",
            "The original analysis omitted a configured network without a Codex association and a reversible setup entry. Screens 40, 41 and 42 identified those distinct routes. Saved pairing, network recovery and replacement consent remain separate. The current device-first entry is BOOT > Options > Connections, replacing the character-hold instruction.",
            "Navigation originals 02, 04, 16, 25, 28-29; Planning originals 27-29; screens 04-16, 35B, 36, 40-42."),
        record("09 October 2026", "Errata", "Standby recovery and malformed touch evidence",
            "A 90-second passive capture observed LAN/DHCP, Welcome inactivity return and standby, but no Connect Codex press. A separate capture recorded Yes at 16497 ms, Connect Codex at 16498 ms, then fifteen FF report bytes and ACK=FF at 17112 ms; expected ACK was AB. ESP_ERR_INVALID_RESPONSE stopped rendering. The correction separates an optional runtime acquisition error from rendering, interrupts gestures and requires a real release.",
            "Navigation originals 28, 31-34; Planning originals 30-34; Service 14-18 / Connection 18-22; screens 04-06, 12, 35B, 41.",
            "These are retained measured failures. The FF hardware cause and corrected physical behavior were not established by those captures."),
        record("09 October 2026", "Errata", "Action layout and touch admission",
            "The approved design uses two side-by-side rounded gradient cards, a centered third action, or one wide capsule. Earlier stacked-blue examples were superseded. Current captions are text-only; the older icon-left prescription is no longer the action contract. Question choices now use numbered single-line rows with full review before Send.",
            "Navigation/Planning original 34; screens 05-15, 26-27, 35B, 36, 40-42; selected-multi-action.png and selected-single-action.png.",
            "Approved design references are not device captures."),
        record("10 October 2026", "Errata", "Automatic chat context and freshness",
            "CCP follows the latest authorized UserPromptSubmit with exact chat/turn identity. Manual Choose/Pick a chat is removed. Background events cannot select context; a tab click is not a documented focus event. Pairing, authenticated presence, app presence and real work remain distinct. Eight seconds without packets expires device state; active work becomes unavailable after 120 seconds without another hook.",
            "Navigation originals 35-38; Planning originals 35-38; screens 15-20, 18U, 26-27, 31-32.",
            "The owner confirmed Connected on 10 October for the preceding pairing implementation. That observation does not prove later hooks or presentation."),
        record("10 October 2026", "Errata", "Retain, replace, cancel and forget",
            "Separate pending and active records on both peers. Persist before confirmation and promote only after authenticated ACTIVE/COMMITTED. Cancel preserves the old operational link; a pending PC candidate requires a new visible code and explicit replacement. Forget removes Cody's CCP records, not the independent PC record. Temporary disconnection does not authorize re-pairing.",
            "Navigation/Planning originals 39-42, 48; screens 12-16, 32, 35B, 40-42."),
        record("10 October 2026", "Errata", "Reply delivery and command outcomes",
            "Receipt is not recipient delivery. An immutable question ticket expires after 180 seconds including delivery acknowledgement. Timeout, offline, Back and Cancel never select an answer. Reply sent requires recipient acknowledgement. Restart retains settings; factory reset requires explicit confirmation and verified deletion. A command ACK proves admission, not reboot completion.",
            "Navigation/Planning originals 43-46; Service 25-28 / Connection 29-32; screens 21-27, 31-32."),
        record("10 October 2026", "Errata", "Boot artwork and current controls",
            "The complete 42-frame VibeWare artwork retains its authored 3.5-second dwell. The 10 October correction replaced the earlier title-free/twofold composition with a title, subtitle and 192 x 219 artwork at (137,164). It also removed the connection detour, clarified the five-minute pairing window and required successful settings write/readback. Current device-first routes are now integrated in the main body.",
            "Navigation/Planning originals 47-50; Manga original 02 and 28-31; screens 01-15, 36, 39-42.",
            "Coordinates are source layout values. Schematics are neither simulator frames nor device captures."),
        record("11 October 2026", "Errata", "English throughout",
            "Translate headings, diagrams, captions, prose and footers into English while preserving original correction dates, screen IDs, technical values and evidence limits.",
            "Navigation originals 01, 05-10, 29, 31, 34, 36, 38-46 and 53; Planning originals 01-02, 09-14, 16, 21-22, 39-46 and 53; Service 33 / Connection 37.",
            "Document language and layout review only."),
        record("12 October 2026", "Change", "Navigation within the documents",
            "Coordinated document identity, navigable contents and bookmarks retain original screen references when pagination changes. The quickstart keeps its own four instructional A5 pages.",
            "All atlas pages; Manga prior final register.", "Document authoring only."),
        record(PREVIOUS_EDITION, "Errata", "Current device and private cloud boundaries",
            "Replace character-led current routes with the device-first dashboard, BOOT Options, current carousel order, Timer, Quick guide and explicit reset screens. Preserve character artwork as Studio/design material. Consolidate corrections into this register and remove accumulated build/code history. The private cloud service is under implementation; cloud firmware and Dot integration are pending.",
            "Current Navigation/Planning bodies; former Navigation pages 03-19 and Planning pages 03-27; map and A5 instructions.",
            "Read-only source review and PDF authoring. No firmware build, test, simulator, device operation or cloud deployment accompanies this edition."),
    ]


# include_cloud: Include the new cloud boundary in communication/planning documents.
def security_records(include_cloud=True):
    records = [
        record("10 October 2026", "Security", "Fresh proof before presence or current data",
            "FIND alone cannot establish presence, redirect notifications or release current data. READY binds a fresh challenge to the observed device endpoint; COMMITTED proves the matching response. Distinct active/pending records, strict field bounds, nonces and one-use challenges preserve explicit admission. Firmware/plugin changes must agree; no legacy handshake fallback is claimed.",
            "CCP FIND / READY / COMMITTED; original screens 12-15, 18U, 26-27, 31-32.",
            "Source-based correction; neither runtime adversarial behavior nor physical recovery is certified."),
        record("10 October 2026", "Security", "Bound work and keep residual risks explicit",
            "Global/per-source ingress budgets and bounded receive slices precede expensive work. Discovery and pairing have finite packet/offer/work budgets. Broker network diagnostics are bounded; startup-only runtime.log retention remains separate. Six-character CCP transcripts permit offline guessing, and ordinary HMAC payloads are readable. Standard PAKE/high-entropy QR and authenticated encryption remain deferred. Optional plaintext device HTTP is a separate review scope.",
            "Firmware UDP owner; PC discovery/pairing; ACTIVITY / TECH / EVENT; optional HTTP.",
            "HMAC authenticates but does not provide confidentiality. No compromised-PC protection or production-security certification is claimed."),
        record("07 November 2026", "Security", "Account projections stay bounded and authenticated",
            "The current endpoint, nonce and authenticated association bind account projections. Credentials, account identity email and message contents stay on the host. Source age, unavailable samples and separate account generations prevent stale or mixed-account graphs being shown as current.",
            "CCP ALLOWANCECAP / ALLOWANCE / ACCOUNT and later bounded account projection; Planning original 24; Communication core 03 and 09.",
            "Source ownership only; this document does not establish live account login or end-to-end device delivery."),
    ]
    if include_cloud:
        records.append(record(PREVIOUS_EDITION, "Security", "Cloud authorization is a separate trust boundary",
            "Use owner sign-in, a key-bound device credential and an explicit owner/device/client grant. An ID, LAN pairing key or account label never authorizes arbitrary destinations. A short enrollment code is one-use and expires; owner claim and physical confirmation belong to the same transaction. HTTPS protects transport; DPoP binds the device token to its P-256 key. Revocation is checked server-side. Existing LAN UDP is not exposed publicly.",
            "New cloud relay concept; original local pairing screens 12-15 remain local. No new firmware screen IDs are assigned.",
            "Private implementation is in progress. Build, runtime/security review, Dot integration, firmware storage and physical confirmation are pending."))
    return records


# label: Atlas identity retained when a shared chapter is used by another document.
def connection_chapters(label="Communication Atlas"):
    return [
        ("Current local connection", cards_page("Current connection: Cody and its paired PC", CURRENT_SCOPE, [
            ("Cody", "The canonical network/link owners authenticate the retained PC. The device model presents dashboard data and semantic events. Wi-Fi, PC presence and Codex app presence are separate facts."),
            ("CCP on the computer", "One local broker owns the PC identity and active/pending credentials. Explicit tools create notices or questions; genuine hooks provide chat/turn activity. The agent does not receive device link keys."),
            ("Local transport", "CCP1 uses UDP on the reachable private LAN. The link has authenticated message checks and current-presence challenges. Ordinary payloads are not encrypted. Do not publish these UDP ports to the Internet."),
            ("Independent cloud work", "The new private relay is a separate service under implementation. It will require a distinct enrollment, device token and client grant. Existing local pairing does not automatically grant cloud access."),
        ], label)),
        ("Local pairing and recovery", cards_page("Pair once, prove current presence again", "Local workflow / original screens 12-15, 40-42. IP addresses and names are not credentials.", [
            ("Start deliberately", "BOOT > Options > Connections > Connect Codex. Keep link resumes the remembered PC. Pair again explicitly permits replacement. Forget opens a separate confirmation."),
            ("Read the intended display", "Use all six characters from the device's current code after discovery. Code preparation is not measured progress. The pairing window is five minutes; no code is logged or broadcast in plaintext."),
            ("Persist both sides", "PC and Cody stage separate candidates while retaining active records. Confirmed persistence and authenticated ACTIVE/COMMITTED complete association. A pending record alone is not success."),
            ("Recover real state", "Ordinary LAN loss or an IP change resumes the saved association. Cancel preserves the old operational link. A local Forget does not revoke the independent PC record. Storage failure requires explicit repair."),
        ], label)),
        ("Messages and delivery", cards_page("A notification is an event; an answer needs delivery", "Semantic content carries stable identity, freshness and the actual outcome.", [
            ("Activity", "Follow the latest authorized prompt with exact chat/turn identity. Tools and completions in another chat cannot take over. A response-ready event does not prove task success or passed checks."),
            ("Questions", "26 opens a question; 27 shows numbered rows, at most two visible. Read the full label/detail before Send. Selecting, scrolling or going Back does not answer."),
            ("Acknowledgements", "PC receipt and recipient delivery are different. Only the recipient's authenticated acknowledgement permits 31 / Reply sent. The deadline includes delivery; timeout never chooses a reply."),
            ("Data discipline", "Keep bounded printable ASCII, immutable request identities, original timestamps and explicit unavailable/error state. Native Codex approvals stay on the PC. A remote notification cannot authorize a privileged action."),
        ], label)),
        ("Cloud trust model", cloud_map(label)),
        ("Cloud pairing ceremony", cards_page("Connect the person, the device and one client", "Proposed cloud ceremony / a separate permission for each association.", [
            ("1 / Device starts", "After an explicit local action, Cody creates a pending enrollment bound to its device key and shows a short one-use code. No client grant exists yet."),
            ("2 / Owner claims", "The owner signs into the relay's browser UI and enters the displayed code. The claim expires and is attempt-limited. Guessing an ID or finding an endpoint grants no access."),
            ("3 / Confirm on Cody", "The physical confirmation must match that exact key-bound transaction and intended owner. The proposed firmware confirmation UI and protected credential storage remain to implement."),
            ("4 / Grant Dot access", "The owner selects the already owned device and authorizes a particular client and scope. The server issues a separate connection credential or accepts the configured OAuth client grant. Other clients need their own grant."),
        ], label, "The server proves control of enrolled credentials and a confirmed ceremony; it cannot continuously prove who physically holds the device.")),
        ("Cloud operation and limits", cards_page("A private service using a standard mounted share", "Implementation target: ASP.NET Core .NET 10, Aspire and OpenTelemetry. Deployment is not part of this edition.", [
            ("Relay persistence on SMB/Samba", "One relay instance stores bounded JSON state, cookie protection keys and bounded operational logs on the OS-mounted share, with no local persistence fallback. The OS handles mount credentials. Local .env.local is configuration only. The relay uses no application database, Azure SDK or provider storage API."),
            ("HTTPS polling first", "Cody initiates outbound HTTPS polling and acknowledges admitted events. The relay bounds queued content, expiry and duplicate identities. No inbound device port or WebSocket transport is required for the first version."),
            ("Aspire on Windows and Linux", "Use normal Aspire AppHost orchestration and its dashboard. Ordinary local files and temporary storage used by Aspire and its tools are permitted. Relay business state, cookie protection keys and bounded service logs use the mounted share. Both host platforms remain targets; builds and runtime verification are pending."),
            ("Telemetry and mount gates", "OpenTelemetry feeds the Aspire dashboard or a configured OTLP endpoint. The dashboard manages its own telemetry storage normally. Bounded relay logs persist on SMB. Exclude secrets and message bodies. Verify mount locks, durable flush, atomic replacement and disconnect/crash recovery. TLS and owner sign-in configuration remain pending."),
        ], label)),
        ("Voice and operator boundaries", cards_page("Use the operation that the current source supports", "Local customer controls, optional voice and engineering access have separate admission.", [
            ("Voice is optional", "The default profile omits microphone acquisition. The wake-word variant admits Sophia for standby wake; the separate Codex voice-input owner remains disabled in that profile."),
            ("Audio evidence", "Recording, encrypted transport, queue acceptance and recipient transcription are separate outcomes. The retained 15 October synthetic queue result did not prove speech receipt. No new physical voice evidence is claimed here."),
            ("Settings and diagnostics", "Authenticated commands retain identity, nonce and actual acknowledgements. Diagnostic measurements come from canonical owners. Acknowledged settings do not prove physical pixels or sound."),
            ("Reset and approval", "Restart and factory reset are separate, explicit actions. A reset ACK establishes admission; reboot and deletion need actual evidence. Codex's native approval remains on the computer."),
        ], label)),
    ]


# No parameters: current device routes and options.
def navigation_chapters():
    label = "Navigation Atlas"
    return [
        ("Start and connect", cards_page("Start from the saved state", "VibeWare appears on cold boot/software reset. The device-first route then uses actual network and pairing state.", [
            ("No saved Wi-Fi", "Not connected > Tap to pair > Wi-Fi setup. App download QR opens the installation page; Show pairing QR starts the Bluetooth session. Scan inside the Espressif provisioning app."),
            ("Saved Wi-Fi", "Connecting to Wi-Fi waits for valid association/IP. Recovery offers Set up Wi-Fi or Later. Later preserves saved credentials and permits background recovery."),
            ("Wi-Fi ready", "Without a retained association, prepare the Codex pairing code. With a valid saved record, search for that PC until current authentication arrives. Internet availability is a separate condition."),
            ("Errors stay explicit", "Storage, service and cleanup failures retain the original cause. Retry is not consent to replace a PC. A missing observation does not become Connected or a fabricated numeric value."),
        ], label)),
        ("Dashboard", cards_page("Read the current dashboard", "Counter mode defaults to On without a saved preference; horizontal swipes change its pages.", [
            ("Counter mode", "Three pages show reset, current context and model. Missing or incoherent observations are unavailable. Change the saved switch in Behavior > More... > More...."),
            ("Carousel when disabled", "Device > Codex reset > Context > Codex limits > Today > Last 7 days > Token history > Credits > Codex account > About."),
            ("Carousel navigation", "With Counter mode off, shake returns to the first enabled statistics page, Codex reset by default. If all statistics are hidden, Device is the fallback. Arrows fade after ten seconds; reveal them before starting a new touch."),
            ("Current and saved data", "Measured counters keep their source date/time. Saved offline observations are labeled with their UTC update times. Missing daily counts remain gaps. No extrapolation, backfill or invented zeros."),
            ("About", "System shows measured CPU/uptime; Memory shows allocatable RAM/PSRAM used, free and largest free block; Firmware shows version/build context. The project QR follows these child pages and is not a carousel item."),
        ], label)),
        ("Options", cards_page("BOOT opens Options", "Three menu pages; the saved Counter mode controls the Home presentation.", [
            ("Page 1", "Connections, Codex stats, Timer. Connections opens Connect Codex and Set up Wi-Fi. Timer runs for 1-120 minutes with Bell or Chime; Back leaves it running."),
            ("Page 2", "Visible screens, Display, Behavior. Display owns brightness/rotation/orientation. Behavior owns Sounds, shake-to-wake and profile-dependent Dictation; More behavior contains the optional screensaver."),
            ("Page 3", "Restart device, Factory reset, Quick guide. Restart retains configuration. Factory reset requires confirmation and verified settings deletion. Quick guide has three pages and returns to Options."),
            ("Saving and returning", "Visibility/settings apply only after confirmed storage; failure retains the prior policy. Back and Home preserve the correct caller. An opening touch cannot also select the newly shown control."),
        ], label)),
        ("Pairing and retained PC", connection_chapters(label)[1][1]),
        ("Questions and notifications", connection_chapters(label)[2][1]),
        ("Standby, battery and voice", cards_page("Local behavior and optional services", "The current source defines behavior; physical verification remains separate.", [
            ("Power and wake", "PWR toggles software standby. Authenticated questions and Reply ready can wake software standby while the LAN link is retained. A running Timer can wake at expiry. The route remains owned by its model."),
            ("Battery", "Awake notices use observed 50%, 20%, 10% and 5% thresholds. An observed 0% requests USB. If hardware cuts power earlier, no unobserved 0% message is invented."),
            ("Screensaver and sound", "Screensaver is off by default; when enabled it uses the idle dashboard without resetting standby timers. A dismissing touch cannot activate hidden content. Sounds and quiet hours govern optional cues."),
            ("Voice profile", "The default build omits microphone acquisition. The wake-word variant uses Sophia for standby wake and does not enable the separate Codex voice-input owner. Recipient transcription is not established."),
        ], label)),
        ("Cloud status", cards_page("Cloud connection is a separate development step", "The current firmware remains a local paired companion.", [
            ("Private service", "The relay is under implementation with separate owner, device and client identities. Grants bind a particular client to an owned device; IDs and device names are not permissions."),
            ("Device work remains", "Cloud enrollment, physical confirmation, protected device credentials and outbound HTTPS polling must be integrated into firmware before this journey can appear on Cody."),
            ("Local route remains explicit", "The six-character Connect Codex procedure describes local CCP pairing. It is not a cloud enrollment code and must not be repurposed as a global API key."),
            ("No live claim", "No Azure deployment, Dot integration, firmware cloud connection or physical behavior is established by this edition. Original screen IDs remain unchanged; no cloud route ID is fabricated."),
        ], label)),
        ("Original screen IDs", identity_page(label)),
    ]


# No parameters: current planning responsibilities and acceptance boundaries.
def planning_chapters():
    label = "Planning Atlas"
    return [
        ("Product and source boundaries", cards_page("One device, separate owners", CURRENT_SCOPE, [
            ("Platform", "Acquisition owners read hardware. DeviceSnapshotService publishes validity, timestamps, health and actual errors. Consumers do not create parallel caches, sensor reads or stale-as-current fallbacks."),
            ("Device presentation", "A device model owns route and interaction state. The renderer owns prepared glyphs, layouts and the last retired frame. Partial output does not become the next displayed-state reference."),
            ("Local host", "CCP owns the local identity, protected credentials, hooks and semantic events. App presence, current chat activity and account observations keep independent freshness and identity."),
            ("Optional cloud", "The relay belongs in its own private service repository, separate from the public firmware. Identity, grants and bounded delivery have separate owners. This public overview explains architecture and limits without distributing the implementation or claiming deployment."),
        ], label)),
        ("Circular layout", geometry_page(label)),
        ("Current screen identities", cards_page("Keep identity while routes evolve", "Original screen IDs describe stable reference identities; PDF pages and storage indices are not IDs.", [
            ("Network and pairing", "04-09 describe network setup; 12-15 describe local Codex pairing and presence. 40-42 retain original decision/setup/recovery references. Device-first labels and BOOT entry are current."),
            ("Work and questions", "15-20 and 18U describe connection/work/unavailable. 21-24 are read-only PR status; 25 is retired. 26/27 retain question/review identity; 31 requires recipient delivery; 32 is link loss."),
            ("Controls", "Question rows are numbered and single-line, two visible at most; the full label/detail precedes Send. Standard actions use text-only captions. Full hit bounds and actual visibility gate touch."),
            ("Character", "The Muse avatar provides the startup motion and touch reaction. Device status and controls have separate owners."),
        ], label)),
        ("Original screen ID inventory", identity_page(label)),
        ("Cloud trust model", cloud_map(label)),
        ("Enrollment and client grants", connection_chapters(label)[4][1]),
        ("Service operation", connection_chapters(label)[5][1]),
        ("Acceptance boundaries", cards_page("Remaining work is explicit", "No test, build, device operation or deployment is implied by document authoring.", [
            ("Host service", "Review identity-provider configuration, client/grant boundaries, token expiry/revocation, DPoP replay controls and the bounded queue. Build and runtime checks require their own authorized execution."),
            ("Mounted SMB/Samba storage", "Verify locks, durable flush, atomic rename and disconnect/crash recovery on real Windows and Linux mounts. Relay state, cookie protection keys and bounded service logs must stay on the share without a local fallback. Aspire's ordinary local tooling and temporary files are permitted. Source settings are not runtime proof."),
            ("Firmware", "Integrate outbound TLS, certificate/time handling, key protection, bounded polling and canonical event ownership. Budget buffers and nested stack use. Physical pairing confirmation and recovery need device evidence."),
            ("Dot and hosting", "Use documented public integration points and independently authorize the agent. Configure the mounted share, TLS, owner identity provider and Aspire dashboard access, then verify real delivery. Normal AppHost/dashboard composition requires host verification. Azure is one host option, with no provider storage API dependency."),
        ], label)),
    ]


# No parameters: a single A4 current route map, including its final correction row.
def navigation_map():
    output, surface = page_start("Navigation map", "Device-first firmware / current source routes. Physical verification is separate.", "A4 desktop reference")
    columns = [
        ("Connect", [
            ("No saved Wi-Fi", "Not connected > Tap to pair > Wi-Fi setup. App download QR installs the app; Show pairing QR starts Bluetooth provisioning."),
            ("Saved Wi-Fi", "Wait for association/IP. Recovery offers Set up Wi-Fi or Later; saved credentials remain."),
            ("Local Codex link", "Wi-Fi ready without a link prepares a code. A saved link searches its PC. BOOT > Options > Connections manages Keep link, Pair again or confirmed Forget."),
        ]),
        ("Dashboard and Options", [
            ("Counter / carousel", "Counter mode defaults On: swipe reset, context and model pages. When Off, the enabled statistics carousel and Muse Home are available."),
            ("BOOT / Options pages", "1: Connections, Codex stats, Timer. 2: Visible screens, Display, Behavior. 3: Restart device, Factory reset, Quick guide. Behavior > More... > More... contains Counter mode."),
            ("About and visibility", "System > Memory > Firmware > project QR. Saved changes require confirmed storage. Arrows fade after 10 seconds; reveal before touching again."),
        ]),
        ("Events and local tools", [
            ("Questions", "Open > numbered choice rows > full review > Send. Recipient acknowledgement permits Reply sent. Back, offline and timeout never answer automatically."),
            ("Power, timer and sound", "PWR toggles standby. Timer keeps running after Back. Authenticated events can wake retained-link standby. Sounds and quiet hours govern optional cues."),
            ("Optional voice", "Default: no microphone acquisition. The wake-word variant uses Sophia for standby wake; Codex voice capture is separately gated and excluded from that profile."),
        ]),
    ]
    width = (W - 110) / 3
    for index, (title, entries) in enumerate(columns):
        x = 38 + index * (width + 17)
        top = copy(surface, f"0{index + 1} / " + title, x, 149, width, 14, TEAL, True) + 16
        for heading, text in entries:
            top = copy(surface, heading, x, top, width, 10.5, INK, True) + 5
            top = copy(surface, text, x, top, width, 9.5, MUTED) + 15
        if top > 471:
            raise ValueError("Navigation map column overflow")
    surface.setFillColor(PALE)
    surface.roundRect(38, 59, W - 76, 65, 6, fill=1, stroke=0)
    copy(surface, PREVIOUS_EDITION + " / ERRATA", 50, 480, 160, 8.5, TEAL, True)
    copy(surface, "Map page 1: carousel, Options, local tools and recipient delivery.", 220, 480, W - 275, 9, MUTED)
    copy(surface, DATE + " / ERRATA", 50, 506, 160, 8.5, TEAL, True)
    copy(surface, "Map page 1: Counter mode, wake-only profile and quiet hours; device verification remains separate.", 220, 506, W - 275, 9, MUTED)
    return page_finish(output, surface)


# No parameters: preserve exactly four current instructional A5 pages.
def quickstart():
    flyer.register_fonts()
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=A5, pageCompression=1)
    surface.setTitle("Cody / Get connected in four steps")
    surface.setAuthor("Umberto Giacobbi")
    for author in (flyer.prepare_page, flyer.plugin_page, flyer.pairing_page, flyer.everyday_page):
        author(surface)
    surface.save()
    return output.getvalue()


# path: Final PDF to render, never an application artifact.
# pdftoppm: Existing Poppler executable selected by the caller.
# directory: Task-owned review path inside artifacts/.
# Raises subprocess.CalledProcessError for rendering failure.
def render_document(path, pdftoppm, directory):
    destination = directory / path.stem
    destination.mkdir(parents=True, exist_ok=False)
    subprocess.run([str(pdftoppm), "-r", "90", "-png", str(path), str(destination / "page")], check=True)
    pages = sorted(destination.glob("page-*.png"))
    for start in range(0, len(pages), 6):
        sheet = Image.new("RGB", (1260, 648), "#CAD5DF")
        draw = ImageDraw.Draw(sheet)
        for index, page in enumerate(pages[start:start + 6]):
            with Image.open(page) as image:
                image.thumbnail((412, 294))
                x, y = (index % 3) * 420, (index // 3) * 324
                sheet.paste(image, (x + 4, y + 23))
                draw.text((x + 8, y + 5), f"Page {start + index + 1}", fill="black")
        sheet.save(destination / f"contact-{start // 6 + 1:02}.png")
    print(f"Rendered {path.name}: {len(pages)} pages")


# argv: Literal authoring options from scripts/device.ps1.
# Raises OSError for missing sources, write failures or mirror mismatches.
# Raises ValueError for unsafe review paths or invalid document layout.
# Raises subprocess.CalledProcessError when Poppler cannot render a final document.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdftoppm", type=Path, required=True)
    parser.add_argument("--review-dir", type=Path, default=REVIEW)
    parser.add_argument("--render-only", action="store_true", help="Render the maintained PDFs without rewriting their content or copies")
    args = parser.parse_args(argv)
    review = args.review_dir.resolve()
    if not review.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF review output must stay inside artifacts/")
    if not args.pdftoppm.is_file():
        raise OSError("Existing Poppler executable required")
    if args.render_only:
        for name in DOCUMENT_NAMES:
            render_document(DOCS / name, args.pdftoppm, review)
        return 0
    register_fonts()
    routes = route_records()
    current_revision = record(DATE, "Errata", "Muse, flat actions and voice profile boundaries",
        "The device source includes Muse, the first-install birth introduction and a saved Counter mode. Actions draw text and existing icons without filled cards. The wake-word profile admits Sophia for standby wake but not the separate Codex voice-input owner. Quiet hours are available in device settings.",
        "Navigation/Planning current pages 3-8 and original screens 01-04, 26-29, 35A/35B and 40-42.",
        "Source review and document authoring only; visual, physical and voice-delivery acceptance remain separate.")
    communication_records = routes[4:]
    communication_records.append(record("12 October 2026", "Addendum", "Operator and diagnostics boundaries",
        "Separate customer operations, authenticated diagnostics and USB engineering. A worker acknowledgement establishes admission or application of a command; the intended physical outcome requires separate observation. Debug and Release share the diagnostic entry without exposing a build-mode choice.",
        "CCP operator/diagnostic routes; original diagnostic screen 38.", "Source review and document authoring only."))
    communication_records.append(record("15 October 2026", "Errata", "Audio queue admission did not prove speech receipt",
        "A synthetic localAudio item entered and left the public experimental queue, while Desktop reported that audio content was omitted because audio input was unsupported. Queue acceptance is not transcription or successful recipient audio delivery.",
        "Communication audio delivery / original voice screens 28-30.",
        "Observed synthetic queue/Desktop result. Physical-device voice submission was not performed."))
    routes.append(current_revision)
    communication_records.append(current_revision)
    outputs = {
        "cody-ccp-connection-atlas-v1.pdf": atlas("Communication Atlas", "CCP Communication Atlas", "Local pairing, semantic events and the proposed private cloud connection.", connection_chapters(), communication_records, security_records()),
        "cody-navigation-atlas-v1.pdf": atlas("Navigation Atlas", "Navigation Atlas", "Current device-first routes, controls and recovery.", navigation_chapters(), routes, security_records(False)),
        "cody-planning-atlas-v1.pdf": atlas("Planning Atlas", "Planning Atlas", "Current responsibilities, geometry and cloud implementation boundaries.", planning_chapters(), routes, security_records()),
        "cody-ccp-pairing-quickstart-a5-v1.pdf": quickstart(),
        "device-navigation-map-a4.pdf": navigation_map(),
    }
    MIRROR.mkdir(parents=True, exist_ok=True)
    # Prepare every complete document before replacing any maintained PDF. Mirrors
    # copy exact final bytes; this creates no validation manifest or history file.
    for name, data in outputs.items():
        (DOCS / name).write_bytes(data)
        shutil.copyfile(DOCS / name, MIRROR / name)
        if (MIRROR / name).read_bytes() != data:
            raise OSError("Obsidian copy differs: " + name)
    for name in outputs:
        render_document(DOCS / name, args.pdftoppm, review)
    print("Authored five current PDFs and identical Obsidian copies; firmware/cloud execution remains unverified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
