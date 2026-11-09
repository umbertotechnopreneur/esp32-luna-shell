"""Illustrate the maintained Cody navigation atlas with isolated Studio frames."""

import argparse
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
import sys
from types import SimpleNamespace

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor, white
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parents[2]
DEVICE = ROOT / "cody-device"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(DEVICE / "simulator/windows"))
sys.path.insert(0, str(ROOT / "cody-desktop/plugins/cody-codex-plugin/mcp"))
from ccp_presence import AppState
from ccp_technical import TechnicalProjection
from cody_ccp_simulator import LinkState, SimulatorAppPresence, SimulatorSnapshot
from cody_device_state import InfoPage
from cody_device_ui import DeviceStudioWindow, PAGES, WifiSetupRoute
from cody_question_preview import question_frames
from vibeware_boot import VibeWareBoot
from generate_cody_simulator_graphics import main as prepare_graphics
from scripts.common.cody_pdf_revision_register import RevisionRecord, render_revision_register

NAME = "cody-navigation-atlas-v1.pdf"
REFERENCE = ROOT / "docs/cody" / NAME
DESTINATION = ROOT / "artifacts/docs/navigation-review" / NAME
W, H = landscape(A4)
DATE = "10 November 2026"
INK = HexColor("#182D42")
MUTED = HexColor("#42596D")
TEAL = HexColor("#007978")
LINE = HexColor("#BCCDD8")
PALE = HexColor("#EEF5F8")
SIDE = 148
NOW = 100.0


@dataclass(frozen=True)
class Screen:
    caption: str
    route: str | WifiSetupRoute = "widgets"
    state: str = "offline"
    index: int = 0


@dataclass(frozen=True)
class ReviewPage:
    title: str
    subtitle: str
    screens: tuple[Screen, ...]
    notes: tuple[str, str, str]


REVIEW = (
    ReviewPage("Start and recover", "Original routes 04-09 and 42 / the saved network selects the entry point.", (
        Screen("VibeWare boot", "boot"), Screen("First start", WifiSetupRoute.NOT_CONNECTED),
        Screen("Wi-Fi setup", WifiSetupRoute.PHONE_GUIDE), Screen("Wi-Fi recovery", WifiSetupRoute.OFFLINE),
    ), ("Keep the first action", "Show recovery only when", "Change the first-run order")),
    ReviewPage("Connect Wi-Fi and Codex", "Original routes 06, 07 and 12-14 / provisioning and PC pairing are separate.", (
        Screen("Install app QR", WifiSetupRoute.INSTALL_APP), Screen("Provisioning handoff", WifiSetupRoute.SCAN_TO_PAIR),
        Screen("Connections", "connections", "lan"), Screen("Pair Codex / dummy code", "connections", "pairing"),
    ), ("Keep the QR or handoff", "Show pairing only when", "Change the return route")),
    ReviewPage("Device across connection states", "Identical Device layout, four synthetic observations. Internet reachability is not encoded in these frames.", (
        Screen("No Wi-Fi / IP"), Screen("Wi-Fi, no PC", state="lan"),
        Screen("PC linked, Codex closed", state="closed"), Screen("Codex connected", state="open"),
    ), ("Keep the main status", "Show a warning only when", "Change status labels")),
    ReviewPage("Followed chat and context", "A long synthetic title scrolls in the same fixed row. Context appears only with an admitted measurement or compaction.", (
        Screen("Device / followed chat", state="context"), Screen("Context / start", "context", "context"),
        Screen("Context / middle", "context", "context", 3), Screen("Context / end", "context", "context", 8),
    ), ("Title to keep visible", "Scroll speed or placement", "Context details to keep")),
    ReviewPage("Navigation and account identity", "Decisions for the next offline review. Hardware routes and Studio coverage are identified below.", (),
               ("Default landing page", "How to identify the account", "Next navigation change")),
    ReviewPage("About and device information", "About has child pages. Studio does not substitute host readings for physical telemetry.", (
        Screen("System", "about", index=0), Screen("Memory", "about", index=1),
        Screen("Firmware", "about", index=2), Screen("Project QR", "about", index=3),
    ), ("Keep in About", "Show only for diagnostics", "Change the return target")),
    ReviewPage("Options and visibility", "BOOT opens Options. Home restores the dashboard. Codex stats opens the first enabled page with data.", (
        Screen("Options / Tools", "options", index=0), Screen("Options / Preferences", "options", index=1),
        Screen("Options / Device", "options", index=2), Screen("Visible screens", "visibility"),
    ), ("Keep on the first page", "Move to an advanced page", "Hide by default")),
    ReviewPage("Display and behavior", "Studio settings are session-only. Firmware has saved settings and voice is build-dependent.", (
        Screen("Display", "display"), Screen("Behavior", "behavior"),
        Screen("More behavior", "more_behavior"), Screen("Screensaver", "screensaver"),
    ), ("Keep a dedicated screen", "Show only in this build", "Change the setting labels")),
    ReviewPage("Timer and device actions", "The timer is local. Restart retains setup. Factory reset clears saved setup after explicit confirmation.", (
        Screen("Timer setup", "timer"), Screen("Timer running", "timer", index=1),
        Screen("Timer finished", "timer", index=2), Screen("Restart confirmation", "restart"),
    ), ("Keep on Options", "Show only when a timer exists", "Change the confirmation flow")),
    ReviewPage("Recovery and local information", "The project QR belongs to About. Factory reset is a separate, destructive action.", (
        Screen("Factory reset confirmation", "factory_reset"), Screen("Connections", "connections", "lan"),
        Screen("Offline guide", "quick_guide", index=2),
    ), ("Keep as an explicit entry", "Show only with confirmation", "Change the return route")),
    ReviewPage("Buttons, touch and rotation", "Quick guide frames plus two orientations of the same Device surface.", (
        Screen("Button guide", "quick_guide"), Screen("Touch guide", "quick_guide", index=1),
        Screen("Device at 0 degrees", state="open"), Screen("Device at 180 degrees", "rotated", "open"),
    ), ("Keep this gesture", "Disable in this state", "Change the default action")),
    ReviewPage("Questions and choice review", "Original routes 26-27 / native Studio renderer with synthetic requests and completed captions.", (
        Screen("Question notice", "question", index=0), Screen("Pending questions", "question", index=1),
        Screen("Numbered choices", "question", index=2), Screen("Read full detail", "question", index=4),
    ), ("Keep as a distinct step", "Show only when multiple items exist", "Change the Back destination")),
    ReviewPage("Delivery and approvals", "Original route 31 and native PC approvals / receipt and final delivery are distinct.", (
        Screen("Awaiting delivery", "question", index=6), Screen("Reply sent", "question", index=7),
        Screen("Approval on your PC", "question", index=8),
    ), ("Keep a separate outcome", "Show only when action is needed", "Change what happens after OK")),
)


# state: Named synthetic observation independent of Internet reachability.
def snapshot_for(state):
    if state == "offline":
        return SimulatorSnapshot(state=LinkState.NETWORK_DOWN, network_ready=False)
    if state in {"lan", "pairing"}:
        return SimulatorSnapshot(state=LinkState.UNPAIRED)
    if state == "context":
        return SimulatorSnapshot(state=LinkState.CONNECTED, retained_pairing=True, ready_age_ms=0,
            app_presence=SimulatorAppPresence(state=AppState.OPEN, valid=True, error=0), activity_valid=True,
            chat="a" * 64, phase="working", title="Cody navigation review across Linux and the Waveshare display",
            title_state="current", title_age_ms=0, title_error=0, technical_valid=True, technical_age_ms=0,
            technical=TechnicalProjection("a" * 64, "b" * 64, "1", context_used=45000, context_capacity=128000))
    presence = AppState.OPEN if state == "open" else AppState.CLOSED
    return SimulatorSnapshot(state=LinkState.CONNECTED, retained_pairing=True, ready_age_ms=0,
                             app_presence=SimulatorAppPresence(state=presence, valid=True, error=0))


# screen: Selected existing Studio route and an isolated state fixture.
# questions: Current native question frames from the Studio capture path.
# Raises original frame-rendering errors without substituting artwork.
def capture(screen, questions):
    if screen.route == "question":
        return questions[screen.index][1]
    preview = DeviceStudioWindow(None, SimpleNamespace(rotation=180 if screen.route == "rotated" else 0,
                                                       safe_guide=False, ccp=False))
    preview.last_navigation = NOW
    if screen.route == "boot":
        return preview.display_image(VibeWareBoot(DEVICE, cody_companion=True).image(1550))
    preview.route = screen.route
    if screen.route == "context":
        preview.route = "widgets"
        preview.page = PAGES.index("Context")
        snapshot = snapshot_for(screen.state)
        preview.frame(snapshot, "", NOW)
        preview.last_navigation = NOW + screen.index
        return preview.frame(snapshot, "", NOW + screen.index)
    if screen.route == "widgets":
        preview.page = screen.index
    elif screen.route == "options":
        preview.options_page = screen.index
    elif screen.route == "quick_guide":
        preview.quick_guide_page = screen.index
    elif screen.route == "about":
        preview.route = "widgets"
        preview.page = PAGES.index("About")
        preview.info.page = tuple(InfoPage)[screen.index]
    elif screen.route == "timer" and screen.index:
        preview.local_timer.start(NOW - 65 if screen.index == 1 else NOW - 305)
        if screen.index == 2:
            preview.local_timer.observe(NOW)
    elif screen.route == "screensaver":
        preview.route = "widgets"
        preview.screensaver.enabled = True
        preview.last_navigation = NOW - 17
    return preview.frame(snapshot_for(screen.state), "DEMO42" if screen.state == "pairing" else "", NOW)


# surface: Landscape PDF canvas.
# text: One complete line to measure and draw.
# x, y: Baseline position in points.
# size: Font size in points.
# color: Foreground color.
# bold: Use the bold face when true.
# width: Maximum measured line width.
# Raises ValueError when text would overflow its assigned region.
def line(surface, text, x, y, size=10, color=INK, bold=False, width=765):
    font = "Helvetica-Bold" if bold else "Helvetica"
    if stringWidth(text, font, size) > width:
        raise ValueError(f"Atlas line exceeds {width} points: {text}")
    surface.setFillColor(color)
    surface.setFont(font, size)
    surface.drawString(x, y, text)


# surface: Landscape PDF canvas.
# title: Review page title.
# subtitle: Interpretation and provenance.
# number: Page number in the delivered atlas.
def header(surface, title, subtitle, number):
    surface.setFillColor(TEAL)
    surface.rect(0, H - 8, W, 8, fill=1, stroke=0)
    line(surface, "CODY / NAVIGATION ATLAS", 40, H - 37, 9, TEAL, True)
    line(surface, title, 40, H - 77, 27, INK, True)
    line(surface, subtitle, 40, H - 103, 10.5, MUTED)
    surface.setStrokeColor(LINE)
    surface.line(40, 35, W - 40, 35)
    line(surface, f"Cody / VibeWare / {DATE}", 40, 19, 8, MUTED)
    line(surface, str(number), W - 62, 19, 8, MUTED)


# surface: Decision page canvas.
# prompts: Exactly three prompts with room for handwritten notes.
def note_lines(surface, prompts):
    line(surface, "REVIEW NOTES", 40, 151, 9, TEAL, True)
    for index, prompt in enumerate(prompts, 1):
        y = 151 - 29 * index
        line(surface, f"{index:02d}   {prompt}", 40, y + 3, 9.5, MUTED, True, 247)
        surface.setStrokeColor(LINE)
        surface.line(284, y, W - 40, y)


# surface: Decision page canvas.
# page: Current screen group and review prompts.
# questions: Current native Studio question frames.
# number: Page number in the delivered atlas.
def screen_page(surface, page, questions, number):
    header(surface, page.title, page.subtitle, number)
    if not page.screens:
        for index, (label, detail) in enumerate((
            ("HOME", "Restores the previous dashboard. If the Events feed is unavailable, Device stays accessible."),
            ("CODEX STATS", "Skips empty usage pages. Dated saved samples and measured zero values can still appear."),
            ("EVENTS AND CURRENT TURN", "On firmware, swipe left from Events when a followed turn is available. Back returns to Events."),
            ("OPTIONS AND DICTATION", "Use Home or Back to leave Options. Shake stays within the dashboard. New dictation waits for an eligible route."),
            ("CODEX ACCOUNT", "Shows the plan and available account metrics. The current CCP projection carries no signed-in display name."),
            ("ACCOUNT SOURCE", "The stable account read can supply email. The separate session display-name API is experimental."),
        )):
            y = 446 - index * 48
            line(surface, label, 40, y, 9, TEAL, True)
            line(surface, detail, 40, y - 18)
        surface.linkURL("https://learn.chatgpt.com/docs/app-server#auth-endpoints", (40, 185, 748, 207), relative=0)
        line(surface, "Gesture ideas: tap the chat title to replay its scroll; swipe left on empty stats space for the next page.",
             40, 168, 9, MUTED)
        note_lines(surface, page.notes)
        return
    spacing = (W - 80) / len(page.screens)
    for index, screen in enumerate(page.screens):
        center = 40 + spacing * (index + 0.5)
        surface.drawImage(ImageReader(capture(screen, questions)), center - SIDE / 2, 282,
                          width=SIDE, height=SIDE, mask="auto")
        caption_width = stringWidth(screen.caption, "Helvetica-Bold", 10)
        line(surface, screen.caption, center - caption_width / 2, 260, 10, INK, True, spacing - 8)
    line(surface, "Source: Cody Studio. Dashboard/setup are simulator-rendered; question frames use the native C++ presentation.",
         40, 226, 9, MUTED)
    if page.title == "Questions and choice review":
        line(surface, "Scroll to the next choice or detail page. Back retains the question until Send or expiry.",
             40, 206, 9, MUTED)
    if page.title == "Buttons, touch and rotation":
        line(surface, "BOOT opens Options. Short PWR toggles standby. Hold PWR for 3 seconds to request shutdown.", 40, 206, 9, MUTED)
        line(surface, "Tap shows arrows. On the dashboard, shake returns to the first available statistics page. Shake to wake is optional.", 40, 190, 9, MUTED)
        line(surface, "Double tap starts eligible dictation outside Options. Auto-rotate keeps touch aligned at 180 degrees when enabled.", 40, 174, 9, MUTED)
    note_lines(surface, page.notes)


STATES = (
    ("No saved Wi-Fi / no IP", "Local controls and provisioning. Saved statistics only if a prior valid record exists."),
    ("Wi-Fi and LAN, no Internet, PC absent", "Timer, settings and available saved statistics. The PC link is required for remote events."),
    ("Wi-Fi and LAN, no Internet, CCP current", "Local pairing and protocol traffic can work. Codex cloud work needs its own service access."),
    ("Internet, PC missing or Codex closed", "Internet alone does not supply chat activity. A linked broker can show PC linked."),
    ("Internet, CCP and Codex current", "Fresh Codex observations and admitted events. Missing or stale fields retain their own status."),
)
HOOKS = (
    ("SessionStart", "Yes / session metadata", "Registers the session; opening a chat does not select it on Cody."),
    ("UserPromptSubmit", "Yes / chat + turn", "Selects the followed chat, clears stale approval, begins active status."),
    ("PreToolUse / PostToolUse", "Yes / tool + turn", "Current snapshot. A short tool call can finish between two device polls."),
    ("PermissionRequest", "Yes / followed chat", "Transient Approval needed once per turn and device. Decide on the PC."),
    ("Stop", "Yes / once per turn", "Queues Reply ready for the current or a background chat on a current LAN peer."),
    ("Interrupt / SessionEnd", "Yes / lifecycle", "Interrupt retires the turn; session end is distinct from closing the app."),
    ("PreCompact / PostCompact", "Yes / auto or manual", "Shows compaction for the current turn; completion does not refresh token counts."),
    ("PreModelRequest / PostModelRequest", "No", "Codex exposes model-attempt hooks; CCP defines no device response."),
    ("SubagentStart / SubagentStop", "No", "Codex exposes subagent lifecycle hooks; CCP defines no dedicated device response."),
    ("CCP semantic question / notice", "Explicit MCP request", "Question or reply-ready joins Events. Ordinary notices are transient."),
    ("Codex presence / PR checks", "MCP lease / explicit notice", "Presence is independent of hooks. PR data is read-only and evidence-bound."),
)


# surface: Matrix page canvas.
# title, subtitle: Matrix heading and scope.
# columns: Column labels and their left edges.
# rows: Complete short cells in reading order.
# notes: Three prompts reserved for decisions.
# number: Page number in the delivered atlas.
def table_page(surface, title, subtitle, columns, rows, notes, number):
    header(surface, title, subtitle, number)
    row_height = 22 if len(rows) > 8 else 34
    top = H - 132
    for label, x in columns:
        line(surface, label, x, top - 13, 9, TEAL, True)
    edges = [x for _, x in columns] + [W - 40]
    for index, row in enumerate(rows):
        y = top - 20 - (index + 1) * row_height
        if index % 2 == 0:
            surface.setFillColor(PALE)
            surface.rect(40, y, W - 80, row_height, fill=1, stroke=0)
        for col, value in enumerate(row):
            line(surface, value, edges[col], y + row_height / 2 - 3, 8.8, INK, col == 0,
                 edges[col + 1] - edges[col] - 8)
    note_lines(surface, notes)


# writer: Destination PDF.
# source: Previous atlas with retained detail and revision pages.
# Raises ValueError if required source sections cannot be located.
def retain_reference(writer, source):
    titles = {item.title: source.get_destination_page_number(item)
              for item in source.outline if hasattr(item, "title")}
    detail = titles.get("Source route reference", titles.get("Start and connect"))
    corrections = titles.get("Corrections register")
    security = titles.get("Security")
    if detail is None or corrections is None or security is None or not detail < corrections < security:
        raise ValueError("Source atlas must include detail, corrections and security sections")
    update = titles.get("Review edition change", security)
    sections = (
        ("Source route reference", range(detail, corrections)),
        ("Corrections register", range(corrections, update)),
        ("Security", range(security, len(source.pages))),
    )
    for heading, indices in sections:
        if heading == "Security":
            update_page = len(writer.pages)
            record = RevisionRecord(DATE, "Change", "Decision pages from current Cody Studio",
                ("Added uniform current Studio frames, connection cases, gesture references and the Codex hook and request matrix.",),
                "Previous atlas pages 3-10 and original screens 04-15, 26-32, 40-42.",
                "Offline synthetic Studio renders and source review. Hardware and actual network outcomes require separate evidence.")
            register = render_revision_register([record], (W, H), "Cody / Navigation Atlas",
                                                start_page=len(writer.pages) + 1, section_title="Review edition change")
            writer.append(PdfReader(BytesIO(register)), import_outline=False)
            writer.add_outline_item("Review edition change", update_page)
        section_page = len(writer.pages)
        for index in indices:
            page = writer.add_page(source.pages[index])
            overlay = BytesIO()
            footer = canvas.Canvas(overlay, pagesize=(W, H))
            footer.setFillColor(white)
            footer.rect(W - 94, 10, 62, 20, fill=1, stroke=0)
            line(footer, str(len(writer.pages)), W - 62, 19, 8, MUTED)
            if heading == "Source route reference" and "Go home returns" in (page.extract_text() or ""):
                footer.setFillColor(white)
                footer.rect(36, H - 112, W - 72, 25, fill=1, stroke=0)
                line(footer, "Three menu pages. Home restores the dashboard. Codex stats opens a page with data.",
                     38, H - 103, 10.5, MUTED)
            footer.save()
            page.merge_page(PdfReader(overlay).pages[0])
        writer.add_outline_item(heading, section_page)


# argv: Public generator arguments forwarded by device.ps1.
# Raises original dependency, renderer or PDF authoring errors.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=REFERENCE)
    args = parser.parse_args(argv)
    source = PdfReader(args.reference)
    prepare_graphics([])
    questions = tuple(question_frames(0, 10000))
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=(W, H), pageCompression=1)
    header(surface, "Navigation Atlas", "Device-first review workbook / compare current screens and decide what should remain visible.", 1)
    line(surface, "HOW TO USE THIS ATLAS", 40, 444, 12, TEAL, True)
    line(surface, "Screens share the same printed scale. Use the three ruled lines to record a decision on each review page.", 40, 417)
    line(surface, "SIMULATED observations show interface states. They do not establish hardware behavior or live telemetry.", 40, 396)
    line(surface, "Studio does not render a live provisioning QR, real usage graphs, dictation or measured battery states.", 40, 375)
    line(surface, "The appendix retains original screen IDs, earlier route details, and the corrections and security records.", 40, 354)
    line(surface, "Printed frame size: 148 points (about 52 mm) from the 466 by 466 logical canvas.", 40, 333)
    note_lines(surface, ("Primary audience", "Default screen", "First navigation change"))
    surface.showPage()

    page_titles = [page.title for page in REVIEW]
    page_titles.insert(3, "Connectivity matrix")
    page_titles += ["Codex hooks and device requests", "Source route reference"]
    header(surface, "Contents", "Screen IDs identify routes. PDF page numbers locate the review pages.", 2)
    for index, title in enumerate(page_titles):
        column, row = divmod(index, 8)
        x, y = 40 + column * 384, 445 - row * 30
        line(surface, f"{index + 3:02d}", x, y, 10, TEAL, True)
        line(surface, title, x + 35, y, 11, INK, True, 336)
    note_lines(surface, ("Pages to review first", "Screens to compare", "Questions for offline review"))
    surface.showPage()

    number = 3
    for index, page in enumerate(REVIEW):
        if index == 3:
            table_page(surface, "Connectivity matrix", "Wi-Fi/IP, Internet access, paired PC and Codex presence are independent.",
                       (("CONDITION", 46), ("AVAILABLE UI AND BEHAVIOR", 302)), STATES,
                       ("Keep accessible offline", "Use last saved values for", "Hide until fresh"), number)
            line(surface, "Saved countdown uses a valid clock and reset instant. Expired reset windows need a fresh Codex observation.",
                 40, 254, 9, MUTED)
            line(surface, "Double tap needs the voice profile, Dictation On, a current paired LAN link and eligible dashboard space.",
                 40, 237, 9, MUTED)
            surface.showPage()
            number += 1
        screen_page(surface, page, questions, number)
        surface.showPage()
        number += 1
    table_page(surface, "Codex hooks and device requests", "Subscribed events come from CCP hooks.json. The last two rows describe separate MCP paths.",
               (("CODEX HOOK OR ENTRY", 46), ("CCP INTERCEPTS", 264), ("DEVICE HANDLING", 427)), HOOKS,
               ("Keep as a status only", "Show as a user-facing notice", "Require an explicit action"), number)
    line(surface, "Sources: learn.chatgpt.com/docs/hooks and CCP hooks/hooks.json, ccp_activity.py and ccp_server.py.",
         40, 187, 8, MUTED)
    surface.linkURL("https://learn.chatgpt.com/docs/hooks", (40, 182, 224, 199), relative=0)
    line(surface, "Cody polls activity every 2 seconds. An absent or expired LAN peer cannot receive the native hook notice.",
         40, 168, 9, MUTED)
    surface.save()

    writer = PdfWriter()
    writer.append(PdfReader(output), import_outline=False)
    writer.add_outline_item("Cover", 0)
    writer.add_outline_item("Contents", 1)
    for index, title in enumerate(page_titles[:-1], 2):
        writer.add_outline_item(title, index)
    retain_reference(writer, source)
    writer.add_metadata({"/Title": "Cody / Navigation Atlas", "/Author": "Umberto Giacobbi",
                         "/Subject": "Current Studio frames, interaction review, connectivity and Codex hooks"})
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    writer.write(DESTINATION)
    print(DESTINATION)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
