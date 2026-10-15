"""Draw the concise, visual body of the Cody/CCP communication atlas."""

from io import BytesIO
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph, SimpleDocTemplate, Table, TableStyle


ROOT = Path(__file__).resolve().parents[2]
SIZE = landscape(A4)
W, H = SIZE
NAVY = colors.HexColor("#101D2E")
INK = colors.HexColor("#1B2B40")
MUTED = colors.HexColor("#586B80")
PAPER = colors.HexColor("#F5F8FC")
WHITE = colors.white
LINE = colors.HexColor("#D7E2EC")
CYAN = colors.HexColor("#0CAEA7")
BLUE = colors.HexColor("#2776DB")
AMBER = colors.HexColor("#E18B17")
RED = colors.HexColor("#C74C51")
PURPLE = colors.HexColor("#8158BD")
QUESTION_ART = ROOT / "docs/cody/ui-proposals/20261014-questions"
VOICE_ART = ROOT / "docs/cody/ui-proposals/20261012-voice/voice-05.png"


# surface: ReportLab page canvas.
# text: Plain English copy to wrap inside the given area.
# x: Left edge in points.
# top: Top edge in points.
# width: Available width in points.
# size: Font size in points.
# color: Text color.
# bold: Select the bold document font.
# max_height: Maximum permitted paragraph height.
# Raises ValueError if a paragraph would overflow its allocated region.
def copy(surface, text, x, top, width, size=11, color=INK, bold=False, max_height=100):
    style = ParagraphStyle(
        "VisualCopy", fontName="CodyDocBold" if bold else "CodyDoc",
        fontSize=size, leading=size * 1.28, textColor=color,
    )
    item = Paragraph(text, style)
    _, height = item.wrap(width, H)
    if height > max_height:
        raise ValueError(f"Atlas paragraph exceeds {max_height} pt: {text[:55]}")
    item.drawOn(surface, x, top - height)
    return top - height


# surface: ReportLab page canvas.
# title: Main visual-page heading.
# eyebrow: Short section label.
# page: One-based physical page number in the body PDF.
def frame(surface, title, eyebrow, page):
    surface.setFillColor(PAPER)
    surface.rect(0, 0, W, H, fill=1, stroke=0)
    surface.setFillColor(NAVY)
    surface.rect(0, H - 106, W, 106, fill=1, stroke=0)
    surface.setFillColor(CYAN)
    surface.roundRect(42, H - 38, 8, 8, 4, fill=1, stroke=0)
    surface.setFont("CodyDocBold", 10)
    surface.drawString(60, H - 37, eyebrow.upper())
    copy(surface, title, 42, H - 54, W - 84, 26, WHITE, True, 42)
    surface.setStrokeColor(LINE)
    surface.line(42, 37, W - 42, 37)
    surface.setFillColor(MUTED)
    surface.setFont("CodyDoc", 8)
    surface.drawString(42, 22, "Cody / CCP  |  Source-based visual guide  |  15 October 2026")
    surface.drawRightString(W - 42, 22, str(page + 2))


# surface: ReportLab page canvas.
# x: Left edge of the card.
# y: Bottom edge of the card.
# width: Card width.
# height: Card height.
# title: Short card heading.
# body: Short card body.
# accent: Semantic border and title color.
# dark: Use a dark card background.
def card(surface, x, y, width, height, title, body, accent=CYAN, dark=False):
    surface.setFillColor(NAVY if dark else WHITE)
    surface.setStrokeColor(accent if dark else LINE)
    surface.roundRect(x, y, width, height, 15, fill=1, stroke=1)
    surface.setFillColor(accent)
    surface.roundRect(x + 14, y + height - 23, 19, 4, 2, fill=1, stroke=0)
    copy(surface, title, x + 14, y + height - 34, width - 28, 13,
         WHITE if dark else accent, True, 36)
    copy(surface, body, x + 14, y + height - 72, width - 28, 10.2,
         WHITE if dark else INK, max_height=height - 80)


# surface: ReportLab page canvas.
# x1: Source arrow endpoint.
# y1: Source arrow endpoint.
# x2: Destination arrow endpoint.
# y2: Destination arrow endpoint.
# color: Arrow stroke color.
def arrow(surface, x1, y1, x2, y2, color=CYAN):
    surface.setStrokeColor(color)
    surface.setLineWidth(2.5)
    surface.line(x1, y1, x2, y2)
    if x2 >= x1:
        surface.line(x2 - 7, y2 + 5, x2, y2)
        surface.line(x2 - 7, y2 - 5, x2, y2)
    else:
        surface.line(x2 + 7, y2 + 5, x2, y2)
        surface.line(x2 + 7, y2 - 5, x2, y2)


# surface: ReportLab page canvas.
# image_path: Existing, versioned UI proposal artwork.
# x: Left image edge.
# y: Bottom image edge.
# width: Image display width.
# height: Image display height.
# caption: Evidence label shown below the image.
# Raises OSError when the referenced artwork is missing.
def screenshot(surface, image_path, x, y, width, height, caption):
    if not image_path.is_file():
        raise OSError(f"Missing visual source: {image_path}")
    surface.drawImage(str(image_path), x, y, width, height,
                      preserveAspectRatio=True, anchor="c", mask="auto")
    copy(surface, caption, x, y - 5, width, 8, MUTED, max_height=22)


# surface: ReportLab page canvas.
# labels: Ordered horizontal process stages.
# y: Vertical center of the stage row.
# colors_by_stage: Semantic color for each stage.
def flow(surface, labels, y, colors_by_stage):
    gap = 21
    width = (W - 84 - gap * (len(labels) - 1)) / len(labels)
    for index, label in enumerate(labels):
        x = 42 + index * (width + gap)
        surface.setFillColor(colors_by_stage[index])
        surface.roundRect(x, y - 31, width, 62, 13, fill=1, stroke=0)
        copy(surface, label, x + 11, y + 15, width - 22, 11,
             WHITE, True, max_height=45)
        if index:
            arrow(surface, x - gap + 3, y, x - 3, y, colors_by_stage[index])


# surface: ReportLab page canvas.
def page_system(surface):
    frame(surface, "One device, one PC, one chat", "01 / The whole system", 1)
    flow(surface, ("Cody<br/>screen + microphone", "Paired PC<br/>CCP broker", "Codex plugin<br/>genuine hooks", "Current<br/>Codex chat"),
         397, (CYAN, BLUE, PURPLE, NAVY))
    copy(surface, "The network link carries device state and authenticated commands. The plugin adds real chat events. These are different sources of truth.",
         42, 330, W - 84, 14, INK, True, 58)
    card(surface, 42, 83, 228, 170, "Pairing", "Both sides remember one intended PC. A saved identity alone says nothing about whether it is online.", CYAN)
    card(surface, 307, 83, 228, 170, "Presence", "Fresh authenticated LAN replies show that the paired PC is reachable now. Internet availability is separate.", BLUE)
    card(surface, 572, 83, 228, 170, "Activity", "Real Codex hooks identify the followed chat, its current work and when a reply is ready.", PURPLE)


# surface: ReportLab page canvas.
def page_setup(surface):
    frame(surface, "From power-on to Setup", "02 / Decide from saved state", 2)
    flow(surface, ("No Wi-Fi?<br/>Provision locally", "Wi-Fi, no IP?<br/>Reconnect", "No pairing?<br/>Offer Connect", "Saved pair?<br/>Find same PC"),
         411, (BLUE, BLUE, CYAN, CYAN))
    card(surface, 42, 172, 344, 174, "Only a real IP opens the next step", "Keep saved Wi-Fi credentials during a reconnect. A storage error is not an absent profile. The installation QR starts no radio; Bluetooth provisioning begins only when selected.", BLUE)
    card(surface, 405, 172, 395, 174, "Setup is reversible", "Connect Codex, Network setup and Options are explicit choices. Saying No or Later does not create a pairing request or erase existing settings.", CYAN)
    surface.setFillColor(NAVY)
    surface.roundRect(42, 76, W - 84, 72, 16, fill=1, stroke=0)
    copy(surface, "Cody route: Welcome 04-09  →  preferences 10/36/11  →  pairing 12-15  →  connected work 16-20.",
         59, 130, W - 118, 13, WHITE, True, 45)
    copy(surface, "Illustrative route map; screen IDs remain stable across PDF revisions.", 59, 94, W - 118, 9, colors.HexColor("#BDCBD9"), max_height=20)


# surface: ReportLab page canvas.
def page_pairing(surface):
    frame(surface, "Pair once, prove presence each time", "03 / Two-sided pairing", 3)
    flow(surface, ("Fresh six-character<br/>code on Cody", "PC selects one<br/>matching offer", "Both save pending<br/>then commit", "Fresh READY /<br/>COMMITTED proof"),
         407, (CYAN, BLUE, PURPLE, CYAN))
    card(surface, 42, 185, 360, 148, "The old link survives a failed replacement", "PC candidate: DPAPI. Cody candidate: separate NVS pending record. Promotion follows authenticated proof and durable readback.", BLUE)
    card(surface, 440, 185, 360, 148, "Discovery is not connection", "FIND alone cannot show the PC as present, redirect notifications or reveal current activity. READY binds a fresh challenge to the observed endpoint.", CYAN)
    surface.setFillColor(colors.HexColor("#FFF3DF"))
    surface.roundRect(42, 78, W - 84, 83, 15, fill=1, stroke=0)
    copy(surface, "If there are zero or multiple matching offers, stop with an error. Never pick the first PC; never reuse a code from a screenshot.",
         60, 141, W - 120, 13, INK, True, 53)


# surface: ReportLab page canvas.
def page_activity(surface):
    frame(surface, "Work and link are separate", "04 / Everyday state", 4)
    card(surface, 42, 282, 228, 111, "PC connected", "Fresh authenticated broker presence. It does not prove a chat is active.", CYAN)
    card(surface, 307, 282, 228, 111, "Working", "Actual prompt and tool hooks name the followed chat and observed phase.", BLUE)
    card(surface, 572, 282, 228, 111, "Reply ready", "A Stop hook says the response is available; it does not certify task success.", PURPLE)
    surface.setFillColor(NAVY)
    surface.roundRect(170, 89, 502, 166, 24, fill=1, stroke=0)
    copy(surface, "Working", 204, 229, 424, 25, colors.HexColor("#66E1D2"), True, 39)
    copy(surface, "Example chat title: Improve audio flow", 204, 188, 424, 16, WHITE, True, 28)
    copy(surface, "Tokens  •  Model  •  Connection  •  Chat", 204, 147, 424, 13,
         colors.HexColor("#BDCBD9"), max_height=24)
    copy(surface, "Illustrative Home panel, not a device capture. Activity expires after 120 s; PC presence after 8 s without current replies.",
         170, 76, 502, 9, MUTED, max_height=31)


# surface: ReportLab page canvas.
def page_question(surface):
    frame(surface, "A question waits for its real recipient", "05 / Reply flow", 5)
    paths = ("02-answer-needed.png", "04-answers-one-two.png",
             "06-read-full-answer.png", "08-reply-received.png")
    labels = ("1 / Notice", "2 / Choose", "3 / Review", "4 / Delivered")
    for index, (name, label) in enumerate(zip(paths, labels, strict=True)):
        x = 42 + index * 198
        screenshot(surface, QUESTION_ART / name, x, 218, 176, 176, label + " / concept mockup")
        if index:
            arrow(surface, x - 18, 305, x - 3, 305, AMBER)
    surface.setFillColor(colors.HexColor("#FFF4DF"))
    surface.roundRect(42, 87, W - 84, 103, 15, fill=1, stroke=0)
    copy(surface, "Two numbered, full-width choices fit at once; swipe for a third. Tap a choice to read it fully, then Send. A broker receipt is still not delivery: only the recipient's matching answer_id ACK makes it Reply sent.",
         59, 171, W - 118, 12.3, INK, max_height=79)


# surface: ReportLab page canvas.
def page_recording(surface):
    frame(surface, "Record, then decide to send", "06 / Voice flow", 6)
    screenshot(surface, VOICE_ART, 50, 149, 284, 284, "Voice animation proposal / not a physical capture")
    stages = (
        ("Prepare", "Complete screen visible before input admission.", BLUE),
        ("3, 2, 1", "Countdown; tap cancels before capture.", PURPLE),
        ("Listen", "AFE supplies real 16 kHz PCM; VAD tracks speech and silence.", CYAN),
        ("5 s quiet", "Five acquired silent seconds start Send countdown.", AMBER),
        ("3, 2, 1", "Tap cancels; no END is sent until countdown completes.", PURPLE),
    )
    for index, (title, body, accent) in enumerate(stages):
        x = 370
        y = 384 - index * 70
        surface.setFillColor(accent)
        surface.circle(x + 10, y, 9, fill=1, stroke=0)
        copy(surface, title, x + 30, y + 14, 104, 12, accent, True, 28)
        copy(surface, body, x + 144, y + 18, 277, 10.2, INK, max_height=45)
        if index < len(stages) - 1:
            surface.setStrokeColor(LINE)
            surface.line(x + 10, y - 11, x + 10, y - 58)
    copy(surface, "A shake or double tap discards Listening. A single tap requests Send. No transcript or audio archive is written to Cody's SD or internal flash.",
         50, 112, 284, 9.5, INK, max_height=64)


# surface: ReportLab page canvas.
def page_audio_delivery(surface):
    frame(surface, "Audio handoff: where proof stops", "07 / Current limit", 7)
    flow(surface, ("Cody<br/>PCM + VAD", "Encrypted<br/>LAN packets", "Private WAV<br/>on paired PC", "Codex<br/>localAudio queue"),
         407, (CYAN, BLUE, PURPLE, NAVY))
    card(surface, 42, 210, 360, 137, "What the pilot proved", "A synthetic WAV was admitted to the public experimental queue for this exact chat. It then left that queue.", CYAN)
    card(surface, 440, 210, 360, 137, "What this chat actually showed", "The received turn said: audio content omitted because audio input is unsupported. Speech was not understood or transcribed.", RED)
    surface.setFillColor(colors.HexColor("#FFF4DF"))
    surface.roundRect(42, 91, W - 84, 96, 15, fill=1, stroke=0)
    copy(surface, "Do not enable physical voice submission through this path yet. The documented model catalog exposes input modalities; this chat omitted the pilot audio. Desktop Voice is a UI feature, not a documented external-WAV injection point.",
         59, 171, W - 118, 12, INK, True, 73)


# surface: ReportLab page canvas.
def page_capabilities(surface):
    frame(surface, "What you can ask CCP to do", "08 / Customer operations", 8)
    card(surface, 42, 260, 360, 146, "Connect", "Discover the physical Cody; pair with its live code; read retained and current connection; list genuine followed sessions.", CYAN)
    card(surface, 440, 260, 360, 146, "Inform", "Report actual progress; publish a known notice; ask a bounded question; poll and acknowledge its answer.", BLUE)
    card(surface, 42, 91, 360, 146, "Personalize", "Read or set preferred name, timezone and quiet hours. Readback confirms a durable change; unsupported values fail.", PURPLE)
    card(surface, 440, 91, 360, 146, "Inspect or power", "Read audio status; show Debug views only on a Debug device. Restart and factory reset need separate explicit current intent and confirmation.", AMBER)
    copy(surface, "Native Codex approvals remain on the computer. CCP has no arbitrary shell, mount browser, firmware updater, transcript reader or PR action tool.",
         42, 77, W - 84, 9, MUTED, max_height=32)


# surface: ReportLab page canvas.
def page_diagnosis(surface):
    frame(surface, "Diagnose the right layer", "09 / A short decision tree", 9)
    flow(surface, ("No device IP?<br/>Fix Wi-Fi", "No authenticated PC?<br/>Check LAN + broker", "No real chat?<br/>Check hooks", "No audio reply?<br/>Check model support"),
         400, (BLUE, CYAN, PURPLE, RED))
    card(surface, 42, 183, 360, 151, "Do not erase to fix a disconnect", "A missing PC or Internet does not remove saved pairing. Keep the same PC identity and retry discovery. A failed write or invalid saved record remains a real error.", BLUE)
    card(surface, 440, 183, 360, 151, "Keep the evidence levels separate", "Queued ≠ consumed; consumed ≠ model-supported; sent ≠ understood. For questions, received ≠ recipient-delivered. A worker ACK ≠ visible pixels.", RED)
    surface.setFillColor(NAVY)
    surface.roundRect(42, 81, W - 84, 77, 15, fill=1, stroke=0)
    copy(surface, "Security boundary: CCP is authenticated but ordinary LAN metadata is not encrypted. The six-character pairing transcript remains offline-guessable; a standard PAKE/AEAD redesign is deferred.",
         58, 142, W - 116, 11.5, WHITE, max_height=58)


# surface: ReportLab page canvas.
def page_sources(surface):
    frame(surface, "Source map and evidence", "10 / Where to look next", 10)
    card(surface, 42, 257, 360, 149, "Packet and device contract", "plugins/cody-codex-plugin/protocol/CCP1.md defines fields, order, size and authentication. Firmware owns sensor reads, settings and screens.", CYAN)
    card(surface, 440, 257, 360, 149, "PC and Codex integration", "The installed CCP plugin owns pairing journals, hooks, private question tickets and audio receipt. The Codex App Server documents model/list and its model modalities.", BLUE)
    card(surface, 42, 84, 360, 149, "Artwork and screen IDs", "Question and voice images here are versioned design proposals. Navigation and planning atlases hold detailed UI routes; this PDF keeps the communication journey.", PURPLE)
    card(surface, 440, 84, 360, 149, "What is actually verified", "The pilot verified queue admission and removal, then this chat reported omitted audio. It did not verify speech recognition or physical audio delivery. The revision register preserves older evidence limits.", RED)


# No parameters; returns rendered body bytes and stable page headings.
# Raises OSError for absent versioned artwork.
# Raises ValueError for copy that cannot fit its assigned panel.
def visual_pdf():
    stream = BytesIO()
    surface = canvas.Canvas(stream, pagesize=SIZE)
    surface.setTitle("Cody / CCP communication journey")
    pages = (
        ("One device, one PC, one chat", page_system),
        ("From power-on to Setup", page_setup),
        ("Pair once, prove presence each time", page_pairing),
        ("Work and link are separate", page_activity),
        ("A question waits for its real recipient", page_question),
        ("Record, then decide to send", page_recording),
        ("Audio handoff: where proof stops", page_audio_delivery),
        ("What you can ask CCP to do", page_capabilities),
        ("Diagnose the right layer", page_diagnosis),
        ("Source map and evidence", page_sources),
    )
    for _, draw_page in pages:
        draw_page(surface)
        surface.showPage()
    surface.save()
    return stream.getvalue(), [title for title, _ in pages]


# Each entry retains the dated decision, its meaningful correction and its
# original page/screen crosswalk without replaying the former full narrative.
REVISION_DIGEST = {
    "One intended PC on a busy LAN": "Retain exactly one PC identity. Commissioning, saved pairing and fresh presence are distinct; the original worksheet remains in Git.",
    "A03: PRs remain read-only": "Watch the supplied PR head and real check names. PR action and native approval stay on the PC; screen 25 was retired.",
    "Startup decisions and reversible Setup": "Saved Wi-Fi without pairing offers a real Yes/No. Setup can reopen. No/Later writes nothing; saved pairing searches the same PC.",
    "Setup recovery after standby": "The old timeout hid Setup. A fresh, stationary hold can reopen it; movement, wake time and busy work cancel. Earlier capture did not test Connect Codex.",
    "Approved action layout and Options entry": "Two semantic cards sit side by side, a third below; Back remains lateral. Options returns to its caller. The approved artwork is a design reference, not a device capture.",
    "Malformed touch report interrupted rendering": "Captured: Yes 16497 ms, Connect Codex 16498 ms, then fifteen FF bytes and ACK=FF at 17112 ms (expected AB). Preserve ESP_ERR_INVALID_RESPONSE; runtime gesture interruption does not prove hardware cause.",
    "Earlier CCP 0.3 activity proposal": "Historical metadata hooks and manual selection were later superseded by the latest genuine prompt. Presence, hook freshness and user-reported earlier connection were different evidence.",
    "Retained pairing and two-sided commit": "PC DPAPI candidate and Cody ccp_pending preserve old active links. Fresh READY/COMMITTED proves the candidate; Cancel, Keep, Pair again and Forget have different effects.",
    "Notifications, choices and recipient delivery": "Question tickets expire within 180 s. Received is not delivered until answer_id ACK; no timeout selects a reply. Historical wire limits remain dated facts.",
    "Acknowledgement is admission, not reset completion": "Restart preserves settings; factory reset verifies erasure of Wi-Fi, pairing and preferences before reboot. A signed ACK alone proves neither reboot nor wipe.",
    "Current boot, setup and complete controls": "Boot artwork, QR timing, local-IP gate and immediate saved-PC search replaced older detours. Persist/read back preferences before claiming success; historical route sketches are not current behavior.",
    "Consolidated service and connection atlas": "Earlier Service and Connection atlases were unified, with original page/screen references and dated errata retained. Old source PDFs remain recoverable in Git.",
    "English correction retained from both earlier atlases": "Earlier Italian headings, captions, copy and footers were translated to English; dates, IDs, artwork meaning and evidence limits were preserved.",
    "One canonical CCP communication reference": "Unify commissioning, protocol, operator and voice material; keep the separate A5 quickstart. Source-only audio design, packet fields and resource bounds are not physical delivery proof.",
    "Audio review: bounded window, readiness and honest uncertainty": "Four 480-sample DATA packets, cumulative ACK and identical-ciphertext retry supersede one-packet send. Readiness is not localAudio support; uncertain native submission is retained without replay.",
    "Operator capabilities, customer tools and Debug boundaries": "Inventory customer and Debug operations. Applied ACK is not pixel evidence. No mount browser, arbitrary shell, face override or remote first-boot setter exists.",
    "Plain-language pairing and voice handoff": "Explain the PC/device/chat roles. A changed followed chat rejects the recording before submission; Cody stores no SD/internal-flash audio archive.",
    "Coordinated cover and navigable contents": "VibeWare cover, copyright and clickable section contents were added. Historical page numbers remain original-edition references.",
    "Voice countdowns, public audio API and integration policy": "Three-second listening and sending countdowns plus five acquired silent seconds. Public localAudio queue admission was a pilot, not transcription. Unsupported private bridges were removed.",
    "Visual journey replaces the table-heavy atlas": "Replace the 49-page text-heavy edition with a short visual journey, source-labelled UI proposals and diagrams. Preserve dated corrections and crosswalks in this final grid.",
    "Public audio pilot outcome": "The synthetic WAV left the queue, but the Desktop chat displayed that audio content was omitted because audio input is unsupported. Physical voice submission stays disabled.",
    "Fresh endpoint-bound proof before current data": "A recorded FIND cannot establish presence or obtain current data. READY binds a fresh one-time challenge to observed IP/port; matching COMMITTED gates current metadata and recovery.",
    "Bound unauthenticated work and retained logs": "Device/PC ingress, discovery, verifier work and broker logs have explicit limits. Startup runtime.log growth and real flood/pairing measurements remain separate open evidence.",
    "Remaining protocol risks are explicit": "Six-character transcripts permit offline guessing and ordinary CCP metadata is not confidential. Standard PAKE or high-entropy pairing plus authenticated encryption needs a coordinated redesign.",
}


# records: Original dated RevisionRecord objects, preserving IDs and chronology.
# start_page: One-based physical page of the first register page.
# section: Exact section heading, Revision register or Security.
# Raises ValueError if a dated record has no reviewed concise digest.
def visual_register(records, start_page, section="Revision register"):
    stream = BytesIO()
    body = ParagraphStyle("RegisterBody", fontName="CodyDoc", fontSize=9.2,
                          leading=11.4, textColor=INK)
    title = ParagraphStyle("RegisterTitle", parent=body, fontName="CodyDocBold",
                           fontSize=10.4, leading=12.6)
    small = ParagraphStyle("RegisterSmall", parent=body, fontSize=8.3,
                           leading=10, textColor=MUTED)
    heading = ParagraphStyle("RegisterHeading", parent=small,
                             fontName="CodyDocBold", textColor=WHITE)
    rows = [[Paragraph("DATE / TYPE", heading),
             Paragraph("CHANGE / AFFECTED ORIGINAL PAGES AND SCREENS", heading)]]
    for record in records:
        digest = REVISION_DIGEST.get(record.title)
        if digest is None:
            raise ValueError(f"Missing reviewed register digest: {record.title}")
        left = [Paragraph(record.date, title), Paragraph(record.kind.upper(), small)]
        right = [Paragraph(record.title, title), Paragraph(digest, body),
                 Paragraph("Affected: " + record.affected, small)]
        rows.append([left, right])
    table = Table(rows, colWidths=[115, W - 84 - 115], repeatRows=1,
                  splitByRow=1, splitInRow=0)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("BACKGROUND", (0, 1), (0, -1), colors.HexColor("#E7F2F6")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.5, LINE),
        ("LEFTPADDING", (0, 0), (-1, -1), 9),
        ("RIGHTPADDING", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 6),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
    ]))

    # surface: ReportLab page canvas.
    # state: Platypus page state for absolute numbering.
    def register_frame(surface, state):
        surface.setFillColor(NAVY)
        surface.rect(0, H - 86, W, 86, fill=1, stroke=0)
        copy(surface, section, 42, H - 32, W - 84, 23, WHITE, True, 38)
        surface.setStrokeColor(LINE)
        surface.line(42, 36, W - 42, 36)
        surface.setFont("CodyDoc", 8)
        surface.setFillColor(MUTED)
        surface.drawString(42, 22, "Historical corrections and original screen IDs / runtime evidence remains separate")
        surface.drawRightString(W - 42, 22, str(start_page + state.page - 1))

    document = SimpleDocTemplate(stream, pagesize=SIZE, leftMargin=42,
                                 rightMargin=42, topMargin=103, bottomMargin=46)
    document.build([table], onFirstPage=register_frame, onLaterPages=register_frame)
    return stream.getvalue()
