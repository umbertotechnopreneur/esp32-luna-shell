"""Author the canonical CCP communication reference, diagrams and dated history."""

from io import BytesIO
from pathlib import Path
import argparse
from hashlib import sha256
import json
import re
import sys

import pymupdf
from PIL import Image
from pypdf import PdfReader, PdfWriter
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas
from reportlab.platypus import Table,TableStyle

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "common"))
from cody_pdf_revision_register import RevisionRecord, plain_paragraph as shared_plain_paragraph, register_fonts

PAGE_SIZE = landscape(A4)
WIDTH, HEIGHT = PAGE_SIZE
INK = colors.HexColor("#18283E")
MUTED = colors.HexColor("#64748B")
TEAL = colors.HexColor("#147D86")
CYAN = colors.HexColor("#229EEA")
LINE = colors.HexColor("#DEE6EF")
DEFAULT_OUTPUT = ROOT / "docs" / "cody" / "cody-ccp-connection-atlas-v1.pdf"
STAGING = ROOT / "artifacts/docs/cody-communication-atlas-20261012"
PROTOCOL = ROOT / "plugins/cody-codex-plugin/protocol/CCP1.md"
SOURCES = ("cody-ccp-connection-atlas-v1.pdf","cody-ccp-service-atlas-v1.pdf",
           "cody-codex-connection-atlas-v1.pdf","cody-ccp-pairing-quickstart-a5-v1.pdf")


# text: Plain English text to render in a wrapped paragraph.
# style: Explicit ReportLab paragraph typography.
def plain_paragraph(text,style):
    return shared_plain_paragraph(text,style)


CORE = (
    ("One connection, three different facts", "01 / Orientation", (
        ("Pairing", "Cody remembers one PC identity and a retained link key. A saved record does not prove that the PC is reachable."),
        ("Presence", "Authenticated current replies establish that the retained PC service is reachable on the local LAN. Public Internet is a separate condition."),
        ("Activity", "Genuine Codex hook observations describe a chat and its work. An explicit question has its own ticket; its context label cannot invent a chat."),
        ("Read this atlas", "Pages 2-5 cover setup and retained pairing; 6-8 explain activity, replies and PRs; 9-11 record controls, trust and evidence. The final Revision register preserves dated changes and earlier qualifications."),
    )),
    ("Start from the saved state", "02 / Boot and network / screens 01-11, 39-42", (
        ("No Wi-Fi profile", "A genuinely absent saved profile opens Welcome 04, installation 05-06, Bluetooth provisioning 07 and local connection 09. Debug can show handoff 39. Continue opens preferences 10, Personalization 36 and skin 11."),
        ("Saved Wi-Fi, no local IP", "42 / No connection preserves credentials while the Wi-Fi owner reconnects. Network setup explicitly reopens 04-09; Later returns to Idle. Invalid or stale observations never count as a valid connection."),
        ("LAN ready", "Without a saved Codex pairing, 40 offers Yes or No. No starts no CCP request and changes no settings. With a retained pairing, search the saved PC on 14; enter 15 only after current authentication."),
        ("Boot and provisioning boundaries", "Cody AI Companion and A distraction-free sidekick sit above the full 192 x 219 floppy artwork at (137,164); title y=60, subtitle y=110. The 42-frame animation has a 3.5-second cycle; startup can extend wall time. Installation QR 06 starts no radio; Pair Cody starts Bluetooth 07. Its session QR exists only when ready to scan."),
    )),
    ("Pair from the intended Codex PC", "03 / First pairing / screens 12-15", (
        ("Prepare the code", "Hold the visible Cody still for 1.5 seconds, release, then choose Connect Codex. Preparing secure code / Please wait / About 30 seconds precedes the actual code. That text is an estimate, not a progress measurement."),
        ("Discover, then use the visible code", "Ask Codex to discover Cody and pair using all six characters currently shown on the intended physical device. Use no old code from a screenshot or document. Zero or multiple matching offers is an explicit error; the first responder is never selected automatically."),
        ("Keep the code screen open", "A request admitted by the service is not completed pairing. Wait for both sides to persist and confirm the relationship. The backend code window is five minutes; code-screen inactivity is three minutes from readiness."),
        ("All set -> connected -> actual work", "13 / You're all set follows the persisted handshake. Let's go searches the saved PC immediately on 14 or reuses authenticated presence on 15. The old ten-second Idle detour is retired. Send a real prompt to establish activity; pairing does not manufacture hook events."),
    )),
    ("Commit the relationship on both sides", "04 / Transaction ownership / CCP1", (
        ("Prepare without destroying the active pair", "The PC protects and persists its candidate with Windows DPAPI before PAIR. Cody writes a separate ccp_pending record. Each side preserves its previous active pairing until the authenticated commit is established."),
        ("Prove the candidate and the commit", "CONFIRM or signed READY proves the PC and permits Cody's commit/readback. READY now carries a fresh random challenge and the observed device address/port. Cody answers COMMITTED for that endpoint. Only the fresh matching response admits broker presence, promotion and current data; FIND alone does not. Lost acknowledgements and restarts can recover through another challenge."),
        ("Cancel and replace are different", "Cancel discards Cody's candidate and preserves the operational old link. The PC candidate can remain pending. Replacing it requires a new visible code and explicit replace_pending:true. Possession of the old key does not authorize deleting the candidate."),
        ("Failures remain real", "Storage corruption, failed writes/readback and ambiguous state are explicit failures, not missing settings. No silent repair, default substitution or success is reported. Pairing paused and Pairing error retain the incomplete candidate or storage cause."),
    )),
    ("Recover the same PC", "05 / Setup, office networks and standby / screens 14-16, 32, 35, 41-42", (
        ("One identity on a busy network", "Even among 100 PCs, Cody searches the retained relationship. Other responding PCs remain outside it. Keep pairing resumes discovery; Pair again explicitly permits replacement; Forget requires a second confirmation."),
        ("Four independent outages", "Public Internet down with a working LAN does not break local CCP presence. Missing Wi-Fi/IP preserves credentials and the pair. Missing PC/service preserves identity and retries discovery. A terminated or unavailable chat is distinct from loss of the paired PC."),
        ("Standby and wake", "Ordinary connected standby preserves the route and pairing and is designed to retain notification reception. An authenticated notice requests wake through the power owner. Deep sleep, wake-word listening and radio retention are separate service decisions; physical recovery must be observed independently."),
        ("A deliberate escape", "A stationary character hold opens Setup only from eligible resting routes; movement over 12 px, gaps over 250 ms, rotation or interruption cancels it. A hold spanning wake starts only after a fresh sample and restored artwork. Busy operations and pending cleanup cannot be bypassed. A normal application flash preserves NVS; erase and damaged storage are separate cases."),
    )),
    ("Describe work without inventing it", "06 / Hook activity / screens 16-20 and 32", (
        ("Real observations", "Reviewed/trusted plugin hooks observe the actual prompt and tool lifecycle. The current route follows the latest genuine prompt automatically; the old manual chat-selection proposal is historical. The shared local PC runtime can serve several open chats while retaining pairing."),
        ("Meaningful text", "Working, Editing code and Running command describe observed phases. A phrase such as Building firmware needs a concrete operation or explicit agent progress. Project and branch labels are context; Reply ready means a response is available and does not certify task success."),
        ("Independent freshness", "Presence normally polls every two seconds and expires after eight seconds without current replies. Hook activity expires after 120 seconds. An old observation is unavailable/stale, not new work. Paused, approval waiting and session ended retain their distinct meanings."),
        ("Native approvals remain on the computer", "PermissionRequest can produce On your PC; Cody does not grant native tool permissions. Explicit semantic questions and MCP notifications have their own identities and cannot create UserPromptSubmit, a fabricated session or an authorization."),
    )),
    ("A choice is delivered only after its recipient ACK", "07 / Questions / screens 26, 27 and 31", (
        ("Read, choose, review", "26 / Codex needs you shows the stated context, title and body. Open reveals two cards, or the pair plus one centered third card. Stable choice IDs preserve meaning. A selection opens Review choice; only Send transmits. Back returns without answering."),
        ("Pending -> Sending -> received -> delivered", "The local device keeps the chosen answer fixed while sending. received means the broker recorded it. The caller polls its private ticket/capability and obtains answer_id; a subsequent matching acknowledge_answer_id proves recipient receipt. Only delivered produces Reply sent."),
        ("Expiry never chooses an answer", "A question lasts at most 180 seconds; duplicates, reopening a view and reconnecting do not renew it. Before Send show Request expired; after Send show Reply unconfirmed / Check on your PC when delivery is uncertain. Cancelled/rejected and expired requests stay terminal."),
        ("Consecutive requests and offline recovery", "The broker waits for the device to acknowledge closure before replacing a question. An active question stays ahead of routine activity and battery updates. Repeating the same answer is idempotent; another answer cannot overwrite it. Offline preserves the question and its original deadline."),
    )),
    ("PR status stays read-only", "08 / Decision A03 / screens 21-24; screen 25 retired", (
        ("Watch the explicitly supplied PR head", "21 / PR pending displays the indicated PR and exact head from the caller's snapshot. A Stop hook does not prove check status. Cody does not merge, approve, close, rerun or comment on a PR."),
        ("Only complete current check data supports totals", "22 / Checks running or Checks passed uses numeric totals only from a complete check list for the same head. Checks passed has no buttons; after the narrative and three seconds the prior work view resumes."),
        ("Show names, not logs", "23 / View failures opens real failed-check names: at most six, each at most 48 printable ASCII characters. Unrepresentable input is rejected instead of truncated. 24 shows three names per page with up to two lines each, vertical scroll and lateral Back."),
        ("Keep diagnosis on the PC", "Check rows are not selectable actions. View log and diagnostic screen 25 are retired. Logs, traces and repairs remain on the computer. Screens 26/27 are not a substitute PR action channel. Recording 28, Transcribing 29 and Review transcript 30 remain a separate future voice route."),
    )),
    ("Settings, gestures and consistent controls", "09 / Local preferences / screens 10, 11, 36 and 41", (
        ("Options returns to its caller", "Setup shows Connect Codex and Network setup as a pair, with Options centered below. Options opens Make it yours, Personalization and Cody's look, then returns to the correct caller. Local preferences do not replace the paired PC or erase the network."),
        ("Persist before confirming", "Shake to wake, Auto-rotate and Sounds are On/Off settings. Orientation is Auto while auto-rotate is on; manual 0 or 180 is available while it is off. Sounds Off stops playback without disabling microphone/wake word. Skins use stable IDs: Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic, Dreamwave. Write/readback failure preserves the requested value and original error."),
        ("Action language", "Two actions use semantic gradient cards side by side; three add one centered card below; one uses an icon-left wide capsule. Labels have no teletype. Controls appear and admit touch together after title/body. Back remains left at (10,203), 60 x 60. Protect each circular chord and every QR quiet zone."),
        ("Physical buttons remain separate", "PWR standby/wake follows build policy; Debug can expose screenshot/restart gestures. BOOT restores a sleeping panel or toggles its existing display mode; its Debug long hold restarts. Neither normal restart nor entering Setup removes saved Wi-Fi/pairing. Explicit erase is a separate user operation."),
    )),
    ("Trust boundaries and security backlog", "10 / Security / source review, not a production certification", (
        ("Authentication is not confidentiality", "CCP1 uses HMAC with identity/nonce checks. Payloads remain readable on the LAN; CRC detects storage corruption without encrypting a key. Windows DPAPI protects PC persistence. The six-character code is not sent in clear, but the captured verifier transcript permits offline guessing; PBKDF2 alone is not a PAKE."),
        ("Private capabilities and explicit destructive intent", "MCP ticket/capability values stay on the PC. Signed reset/erase admission does not prove completion. Restart preserves configuration; factory reset removes preferences, Wi-Fi and pairing while retaining firmware, assets and SD, and reboots only after verified erasure. The nine-digit anti-mistake confirmation is not a password and must not be filled from documentation."),
        ("Source hardening in this revision", "Endpoint-bound one-time READY/COMMITTED challenges prevent recorded FIND from selecting a current-data peer. Firmware receive-pass and global/per-source ingress budgets protect maintenance. PC discovery and PBKDF2 work have explicit limits; rejected-traffic summaries and rotating logs bound diagnostics. The firmware and plugin must be updated together."),
        ("Still requires design and evidence", "A standard PAKE or high-entropy out-of-band pairing plus authenticated encryption needs a separate protocol decision. Do not call a LAN trusted merely because it is local. Firmware compilation, adversarial checks and physical pairing/recovery evidence are separate from this PDF consolidation."),
    )),
    ("Keep evidence and open decisions visible", "11 / Working notes / carried forward from the connection atlas", (
        ("Evidence levels", "Source implementation, static cross-component review, host assertions, successful build/flash and physical observation are different claims. This atlas is a source-based document; screenshots/schematics are not current device evidence. done.md records dated results."),
        ("Earlier observed failure retained", "A prior authorized capture showed Yes at 16497 ms, Connect Codex at 16498 ms, then a CST9217 report of fifteen FF bytes with ACK=FF at 17112 ms (expected AB). ESP_ERR_INVALID_RESPONSE stopped rendering. The optional runtime-acquisition correction does not establish the hardware root cause."),
        ("Scope of the touch correction", "After startup only, an invalid touch report can interrupt gestures while rendering continues. The acquisition owner keeps the actual error, clears queued gesture reports and requires a genuine acquired release. No release is fabricated; other startup/locking/route failures remain terminal."),
        ("Use the review worksheet", "Record the observed device route, PC/service state, connection flow, decision to retain and open question. Preserve screen IDs across repagination. A failure or partial failure is not a successful delivery, current sensor reading or permission."),
    )),
)


# surface: PDF canvas receiving a bounded paragraph.
# text: Plain printable text.
# x: Left edge in PDF points.
# y: Top edge in PDF points.
# width: Available line width.
# size: Font size.
# bold: Whether to use the bold face.
# Raises ValueError if a paragraph exceeds the page content area.
def paragraph(surface, text, x, y, width, size=12, bold=False):
    style = ParagraphStyle("AtlasText", fontName="CodyDocBold" if bold else "CodyDoc", fontSize=size, leading=size * 1.38, textColor=INK)
    item = plain_paragraph(text, style)
    _, needed = item.wrap(width, HEIGHT)
    if y - needed < 48:
        raise ValueError("Atlas paragraph exceeds the reserved footer")
    item.drawOn(surface, x, y - needed)
    return y - needed


# surface: PDF page canvas.
# title: Main page heading.
# section: Section label above the heading.
# number: Absolute page number.
def frame(surface, title, section, number):
    surface.setFillColor(colors.white)
    surface.rect(0, 0, WIDTH, HEIGHT, fill=1, stroke=0)
    surface.setFillColor(CYAN)
    surface.setFont("CodyDocBold", 10)
    surface.drawString(42, HEIGHT - 35, "CODY / CCP COMMUNICATION ATLAS")
    surface.setFillColor(MUTED)
    surface.setFont("CodyDoc", 10)
    surface.drawRightString(WIDTH - 42, HEIGHT - 35, "12 October 2026 / source reference")
    paragraph(surface, title, 42, HEIGHT - 57, WIDTH - 84, 27, True)
    surface.setFillColor(MUTED)
    surface.setFont("CodyDoc", 10)
    surface.drawString(42, HEIGHT - 109, section)
    surface.setStrokeColor(LINE)
    surface.line(42, 35, WIDTH - 42, 35)
    surface.setFont("CodyDoc", 8)
    surface.drawString(42, 21, "Cody / confidential / source-based reference; runtime evidence recorded separately")
    surface.drawRightString(WIDTH - 42, 21, str(number))


# surface: Page canvas receiving the simple relationship diagram.
# labels: Ordered labels describing independent stages.
# y: Vertical center in PDF points.
def process_strip(surface, labels, y):
    gap = 16
    box_width = (WIDTH - 84 - gap * (len(labels) - 1)) / len(labels)
    for index, label in enumerate(labels):
        x = 42 + index * (box_width + gap)
        surface.setFillColor(colors.HexColor("#EDF5F6"))
        surface.setStrokeColor(colors.HexColor("#CBE1E4"))
        surface.roundRect(x, y - 24, box_width, 48, 8, fill=1, stroke=1)
        surface.setFillColor(TEAL)
        surface.setFont("CodyDocBold", 11)
        surface.drawCentredString(x + box_width / 2, y - 4, label)
        if index:
            surface.setStrokeColor(TEAL)
            surface.line(x - gap + 3, y, x - 3, y)
            surface.line(x - 7, y + 3, x - 3, y)
            surface.line(x - 7, y - 3, x - 3, y)


# start_page: Physical page number of the first retained semantic chapter.
# crosswalk_page: Physical page number of the closing source-crosswalk page.
# Raises ValueError if authored content exceeds the fixed page layout.
def core_pdf(start_page=1,crosswalk_page=12):
    register_fonts()
    output = BytesIO()
    surface = canvas.Canvas(output, pagesize=PAGE_SIZE)
    surface.setTitle("Cody CCP Connection Atlas v1")
    for number, (title, section, blocks) in enumerate(CORE, 1):
        if number == 8:
            title = "PR/check observations stay with the work"
        frame(surface, title, section, start_page+number-1)
        y = HEIGHT - 143
        for heading, body in blocks:
            body = body.replace("Keep pairing", "Keep link")
            if heading == "Read this atlas":
                body = "Pages 1-4 give the nontechnical picture: pairing, voice input and the rule that a changed chat rejects the recording. Pages 5-15 cover commissioning, recovery, activity, questions, observations and trust. The detailed wire, API and audio reference follows from page 16. The source crosswalk and final dated register retain earlier decisions and evidence."
            if number == 8 and heading == "Keep diagnosis on the PC":
                body = body.replace("Recording 28, Transcribing 29 and Review transcript 30 remain a separate future voice route.",
                    "Screens 28-30 originated as a future voice route. Source version 0.6.5 now adds one-way capture, transport and native input; installation and physical delivery remain unverified, as qualified in the audio chapter.")
            y = paragraph(surface, heading, 42, y, WIDTH - 84, 13, True) - 5
            y = paragraph(surface, body, 42, y, WIDTH - 84, 11) - 15
        if number in (1, 3, 4, 7) and y > 108:
            labels = {1: ("Saved pairing", "Fresh presence", "Observed activity"), 3: ("12 / Code", "13 / All set", "14 / Search", "15 / Connected"), 4: ("Candidate", "Peer proof", "Persist + readback", "Commit proof"), 7: ("Pending", "Sending", "received", "Recipient ACK", "delivered")}[number]
            process_strip(surface, labels, 77)
        if number not in (1,3,4,7) and y > 130:
            notes(surface)
        surface.showPage()
    frame(surface, "Source crosswalk", "Original pagination and screen identities retained as references", crosswalk_page)
    groups = (
        ("Current concepts and trust", "Service 1-2, 7-10; Connection 1, 9-11, 13", "1-3, 8-12"),
        ("First pairing and persistence", "Service 3-6; Connection 2-8, 12", "4-7, 15-16"),
        ("Setup decision and recovery", "Service 11-15; Connection 15-19", "4, 7, 11 + grid"),
        ("UI decision and touch cause", "Service 16-18; Connection 20-22", "11, 13 + grid"),
        ("Earlier activity proposal", "Service 19-20; Connection 23-24", "8, 17-19 + grid"),
        ("Retained pairing revision", "Service 21-24; Connection 25-28", "4-7, 16 + grid"),
        ("Notifications and PR revision", "Service 25-28; Connection 29-32", "9-10, 20 + grid"),
        ("Boot, settings and controls", "Service 29-32; Connection 33-36", "4-5, 11, 21-22"),
        ("Sketch and open decisions", "Connection 14", "13, worksheet"),
    )
    y = HEIGHT - 145
    for topic, original, current in groups:
        y = paragraph(surface, topic, 42, y, 255, 11, True)
        paragraph(surface, original, 306, y + 15, 350, 9)
        paragraph(surface, current, 668, y + 15, 125, 9)
        y -= 20
        surface.setStrokeColor(LINE)
        surface.line(42, y + 6, WIDTH - 42, y + 6)
    paragraph(surface, "Service means the former 32-page CCP Service Atlas. Connection means the former 36-page Codex Connection Atlas. All substantive topics and dated evidence are carried forward once, in English. Repeated preservation boilerplate is consolidated here. Earlier diagrams remain in Git; no historical illustration becomes proof of current physical behavior.", 42, 125, WIDTH - 84, 10)
    surface.showPage()
    frame(surface, "Communication review worksheet", "Sketch the next refinement / original Connection page 14", crosswalk_page+1)
    for x, y, w, h, label in ((42, 288, 360, 160, "DEVICE / observed route and input"), (422, 288, 378, 160, "PC / CODEX / service and chat"), (42, 153, 758, 112, "CONNECTION FLOW / identities, messages and timing"), (42, 52, 360, 79, "DECISIONS TO KEEP"), (422, 52, 378, 79, "OPEN QUESTIONS")):
        surface.setStrokeColor(LINE)
        surface.roundRect(x, y, w, h, 5, stroke=1, fill=0)
        surface.setFillColor(MUTED)
        surface.setFont("CodyDocBold", 9)
        surface.drawString(x + 12, y + h - 20, label)
    surface.showPage()
    surface.save()
    return output.getvalue()


# Raises OSError when the optional approved reference image is unreadable.
def revision_history():
    approved = ROOT / "docs" / "cody" / "ui-proposals" / "20261009" / "selected-multi-action.png"
    return [
        RevisionRecord("2026-10-04", "Change", "A03: PRs remain read-only", ("Retain PR/check states and the indicated head, show only real failed-check names, and leave diagnosis/actions on the PC. Screen 25 is retired. Native approvals remain computer-owned; voice screens 28-30 are future work.",), "Service scope/security; Connection 9-14; screens 21-30", "Original design decision; not a physical capture."),
        RevisionRecord("2026-10-09", "Addendum", "Startup decisions and reversible Setup", ("The earlier analysis omitted saved Wi-Fi without Codex pairing and a reversible Setup entry. Add 40 / Yes-No, 41 / character hold and 42 / reconnect wait. No/Later causes no persistence or new CCP request. Saved pairing searches 14 and requires current authentication for 15.", "PWR/BOOT do not clear saved pairing. A factory erase remains separately authorized."), "Service 11-13; Connection 15-17; original screens 12-16, 40-42", "Source/static review only in this revision; firmware, host and physical verification remained pending."),
        RevisionRecord("2026-10-09", "Errata", "Setup recovery after standby", ("The legacy Welcome timeout path obscured reopening Setup. A resting Welcome tap release or eligible stationary hold can reopen 41. Wake time cannot count toward the hold until fresh acquisition and visible restoration; movement, gaps and rotation cancel it. Busy provisioning and pending cleanup are excluded.",), "Service 14-15; Connection 18-19; screens 04-06, 12, 35B, 41", "A 90-second capture showed LAN/DHCP, inactivity return and standby, but no Connect Codex press. Corrected physical behavior was still pending."),
        RevisionRecord("2026-10-09", "Change", "Approved action layout and Options entry", ("Two semantic gradient cards sit side by side; a third is centered below. A single action is an icon-left wide capsule. Back stays lateral, render/touch share bounds, and QR quiet zones are protected. Options reopens preferences and skin before returning to Setup.",), "Service 16-18; Connection 20-22; screens 05-15, 35B, 36, 40-42", "Approved design reference, not a device screenshot.", str(approved) if approved.exists() else None, "Approved multi-action reference / 09 October 2026."),
        RevisionRecord("2026-10-09", "Errata", "Malformed touch report interrupted rendering", ("The original analysis failed to distinguish an optional runtime touch error from renderer failure. A capture recorded Yes at 16497 ms, Connect Codex at 16498 ms, then fifteen FF report bytes and ACK=FF at 17112 ms. The expected ACK was AB; ESP_ERR_INVALID_RESPONSE stopped rendering.", "The owner preserves the original error. Only after startup, invalid acquisition may interrupt gestures while rendering continues, with queued reports discarded and a real release required. The FF hardware root cause was not established."), "Service 17-18; Connection 21-22; screens 12 and 35B", "Measured failure retained; corrected physical touch behavior remained pending."),
        RevisionRecord("2026-10-10", "Addendum", "Earlier CCP 0.3 activity proposal", ("Metadata-only hooks, shared local runtime, explicit manual session selection and nonce-bound ACTIVITY were the proposal recorded then. Screen 15 proved PC presence only; 18U made unavailable activity explicit. Two-second polling, eight-second presence and 120-second hook freshness were separate.", "Historical limitation: branch acquisition, notifications and voice were then future work. Manual chat selection was later retired in favor of the genuine latest prompt; later source revisions supersede that policy."), "Service 19-20; Connection 23-24; original Service 1-2, 7-8, 10 / Connection 10-11, 13", "User-reported Connected applied to earlier firmware/CCP 0.2, not proof of the new 0.3 activity proposal."),
        RevisionRecord("2026-10-10", "Change", "Retained pairing and two-sided commit", ("Separate commissioning, recovery and real chat activity. Already paired offers Keep pairing, Pair again and explicitly confirmed Forget. Retry keeps the same PC; Cancel abandons the candidate; Forget removes only Cody's CCP records. Preferences and Wi-Fi remain separate.", "PC DPAPI candidate and Cody ccp_pending preserve old active records. CONFIRM/fresh READY proves the peer; persisted ACTIVE/COMMITTED proves promotion. Repeat COMMITTED on each current READY to recover lost acknowledgement. Explicit replacement is required for an existing pending PC candidate."), "Service 21-24; Connection 25-28; screens 04-16, 32, 35B, 36, 40-42", "Source revision; paired Internet outages, restarts and wake behavior require their own device evidence."),
        RevisionRecord("2026-10-10", "Addendum", "Notifications, choices and recipient delivery", ("26 opens a bounded explicit ticket; 27 uses cards plus a separate Send review. The recipient must acknowledge answer_id before delivered / Reply sent. The 180-second deadline applies even to received; neither disconnection, timeout nor cancellation chooses a reply. Terminal states do not reopen.", "The recorded wire revision used activity=1/events=1 and a 1280-byte EVENT ceiling, versus 512-byte ordinary packets. Context/title/body and choice text are bounded printable ASCII; capabilities stay private on the PC. Later capability revisions must be read from current source, not this historical example."), "Service 25-28; Connection 29-32; screens 21-27, 31-32", "Source contract; original 24/24/63-character context/title/body and 12/63 label/detail limits are historical revision facts."),
        RevisionRecord("2026-10-10", "Errata", "Acknowledgement is admission, not reset completion", ("Restart retains configuration; factory reset removes preferences, Wi-Fi and pairing but preserves firmware, assets and SD. Destructive intent and its nine-digit anti-mistake confirmation must come from the current user message. Never complete it from a document. Signed ACK means accepted; new presence proves reboot, and verified erasure precedes factory-reset reboot.",), "Service 27-28; Connection 31-32; reset and factory-reset commands", "Real erase/boot outcomes remain distinct from authenticated command admission."),
        RevisionRecord("2026-10-10", "Change", "Current boot, setup and complete controls", ("Boot gains the title/subtitle above complete floppy art. Installation QR starts no radio, Bluetooth session QR exists only when ready, and local Wi-Fi/IP gates Continue. Code preparation estimates do not fabricate progress. All set searches immediately, removing the old Idle detour.", "Four local preference rows, stable skin IDs and readback-confirmed saves retain real errors. Controls appear together after narrative completion, without teletype labels. Historic artwork sizes and obsolete route sketches remain dated references, not the current contract."), "Service 29-32; Connection 33-36; boot 01-03, Wi-Fi 04-09/42, preferences 10/36/11, Codex 12-15/40/41", "Static source geometry and approved art only; this document revision executed no simulator or physical-device validation."),
        RevisionRecord("2026-10-10", "Change", "Consolidated service and connection atlas", ("Merge the former 32-page service atlas and 36-page connection atlas into one English reference. Deduplicate shared setup/notification history, retain original page/screen crosswalks and the review worksheet, and replace scattered closing errata with this reusable dated register.", "The register uses a fixed Date/Type column and a separate narrative/artwork column, repeating its header across pages. Supported categories are Security, Errata, Addendum and Change. Old source documents remain recoverable in Git; their historical evidence and qualifications are preserved above."), "Both source atlases in full; consolidated pages 1-13 and Revision register", "PDF authoring and visual review are document checks, not firmware/security runtime validation."),
    ]


# Returns dated source-implementation records, without claiming runtime evidence.
def security_history():
    return (
        RevisionRecord("2026-10-10", "Security", "Fresh endpoint-bound proof before current data", (
            "A captured authenticated FIND can request a challenge, but cannot establish presence, change the current notification destination or obtain ACTIVITY, TECH or EVENT. READY is signed over device, pc, nonce, a random challenge, observed device IPv4 address and UDP port. Cody verifies that endpoint and answers COMMITTED.",
            "The ready-v2 and committed-v2 authentication domains require the matching fresh one-time response from that endpoint before commit recovery, liveness and current-data delivery. This is a coordinated firmware/plugin update; no old-handshake fallback is provided.",
        ), "Core page 4; FIND / READY / COMMITTED; screens 12-15 and current activity/questions", "CCP 0.6.3 source implementation/static review only; no deployment, pairing or adversarial runtime evidence accompanies this PDF."),
        RevisionRecord("2026-10-10", "Security", "Bound unauthenticated work and retained logs", (
            "Firmware limits each UDP receive pass by packet count and time, then runs local commands and protocol maintenance even while traffic remains queued. Fixed global and per-source ingress budgets precede expensive authentication. Rotating source addresses cannot bypass the global budget.",
            "PC discovery bounds total packets/offers and per-source offers; verifier work also has a time/work ceiling. Overflow is an explicit failure with no association staged, not an incomplete result labelled complete. Broker admission precedes credential work; rejection summaries and the exclusively owned rotating broker log bound network diagnostic growth.",
            "Residual: startup-only runtime.log still appends across launches. LAN UDP rejection traffic cannot grow that bootstrap file; bounding repeated-launch output is deferred separately from the implemented broker/network log rotation.",
        ), "Core page 10; device worker, PC discovery/pairing, broker ingress and runtime logs", "Source-only limits need measurement against real pairing/reconnection bursts. No suite, flood probe, build or flash is claimed."),
        RevisionRecord("2026-10-10", "Security", "Remaining protocol risks are explicit", (
            "Preserve local pairing admission, expiring codes, constant-time proofs, distinct active/pending journals, strict packet/identity checks, signed choices/settings/debug/reset and loopback-authenticated PC control. Native approval stays on the PC; storage/authentication failures remain failures.",
            "Six-character pairing transcripts still permit offline candidate checking; rate limiting does not solve it. CCP payloads remain authenticated and unencrypted. Standard PAKE or high-entropy QR admission followed by authenticated encryption is deferred as a coordinated redesign. An enabled optional device HTTP service needs its own review.",
        ), "Pairing verifier/link-key derivation; ACTIVITY / TECH / EVENT; optional HTTP", "No confidentiality or production-security certification. Compilation, stack headroom and physical recovery remain pending."),
    )


# surface: PDF drawing owner for the review area.
# y: Upper baseline of the separate writable notes area.
def notes(surface,y=108):
    surface.setFillColor(CYAN)
    surface.setFont("CodyDocBold",9)
    surface.drawString(42,y,"NOTES")
    surface.setStrokeColor(LINE)
    for baseline in (y-12,y-30,y-48):
        surface.line(42,baseline,WIDTH-42,baseline)


# surface: PDF page receiving a compact technical grid.
# columns: Column heading strings.
# rows: Plain English rows, with exact protocol fields where applicable.
# widths: Available widths in PDF points for each column.
# y: Upper table coordinate.
# Raises ValueError if the table reaches the writing area or footer.
def technical_table(surface,columns,rows,widths,y=HEIGHT-143):
    body = ParagraphStyle("TechnicalBody",fontName="CodyDoc",fontSize=9.3,leading=12.5,textColor=INK)
    header = ParagraphStyle("TechnicalHeader",parent=body,fontName="CodyDocBold",textColor=colors.white)
    table = Table([[plain_paragraph(text,header) for text in columns]]+
                  [[plain_paragraph(str(text),body) for text in row] for row in rows],colWidths=widths)
    table.setStyle(TableStyle([("BACKGROUND",(0,0),(-1,0),INK),("BACKGROUND",(0,1),(0,-1),colors.HexColor("#EDF5FA")),
        ("VALIGN",(0,0),(-1,-1),"TOP"),("GRID",(0,0),(-1,-1),0.5,LINE),
        ("LEFTPADDING",(0,0),(-1,-1),9),("RIGHTPADDING",(0,0),(-1,-1),9),
        ("TOPPADDING",(0,0),(-1,-1),8),("BOTTOMPADDING",(0,0),(-1,-1),8)]))
    _,height = table.wrap(WIDTH-84,HEIGHT)
    if y-height < 135:
        raise ValueError("Technical grid overlaps notes: " + str(columns))
    table.drawOn(surface,42,y-height)
    notes(surface)


# source_directory: Protected snapshots of the four user-mentioned source PDFs.
# Raises OSError for unreadable historical material or staging writes.
def preserve_sources(source_directory):
    source_directory.mkdir(parents=True,exist_ok=True)
    manifest = []
    for name in SOURCES:
        backup = source_directory/name
        if not backup.exists():
            import subprocess
            backup.write_bytes(subprocess.run(["git","show","1d46253f:docs/cody/"+name],
                cwd=ROOT,capture_output=True,check=True).stdout)
        document = pymupdf.open(backup)
        manifest.append({"file":name,"pages":len(document),"sha256":sha256(backup.read_bytes()).hexdigest()})
        (source_directory/(name+".txt")).write_text("\n\n".join(
            f"ORIGINAL PAGE {index+1}\n"+page.get_text() for index,page in enumerate(document)),encoding="utf-8")
    (source_directory/"manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")


# surface: Native PDF drawing owner.
# x: Center of the small illustrated device identity.
# y: Center of the device identity.
# Raises subprocess.CalledProcessError if the immutable original artwork is unavailable.
def cody_identity(surface,x,y):
    import subprocess
    from generate_cody_planning_atlas import sprites
    original = subprocess.run(["git","show","027498aa:docs/Cody/cody-planning-atlas-v1.pdf"],
                              cwd=ROOT,capture_output=True,check=True).stdout
    document = pymupdf.open(stream=original,filetype="pdf")
    illustration = sprites(document)[0]
    width,height = illustration.getSize()
    surface.setFillColor(colors.black)
    surface.circle(x,y,54,fill=1,stroke=0)
    target_width = 88
    target_height = target_width*height/width
    surface.drawImage(illustration,x-target_width/2,y-target_height/2,width=target_width,height=target_height,mask="auto")


# Raises ValueError if an introductory diagram cannot fit the authoring contract.
# Returns four plain-language introductory pages before the detailed reference.
def introduction_pdf():
    output = BytesIO()
    surface = canvas.Canvas(output,pagesize=PAGE_SIZE)
    frame(surface,"Cody, the PC helper and your chat","A plain-English map / read this before the detailed reference",1)
    cody_identity(surface,132,HEIGHT-229)
    paragraph(surface,"CODY",42,HEIGHT-310,180,12,True)
    paragraph(surface,"Your device. It shows its own status, listens only when admitted, and remembers its paired PC.",42,HEIGHT-334,190,11)
    for x,title,body in ((305,"YOUR PC HELPER","The installed CCP plugin and its local helper keep the pairing, pass genuine chat activity to Cody, and receive optional voice audio."),
                         (566,"YOUR ACTUAL CODEX CHAT","The chat you are already using. It keeps approvals and decisions on the desktop. Cody never makes a replacement chat.")):
        surface.setFillColor(colors.HexColor("#EDF5FA"))
        surface.setStrokeColor(LINE)
        surface.roundRect(x,HEIGHT-295,232,124,10,fill=1,stroke=1)
        paragraph(surface,title,x+12,HEIGHT-187,208,12,True)
        paragraph(surface,body,x+12,HEIGHT-214,208,11)
    surface.setStrokeColor(CYAN)
    for x in (205,):
        surface.line(x,HEIGHT-234,x+62,HEIGHT-234)
        surface.line(x+62,HEIGHT-234,x+55,HEIGHT-230)
        surface.line(x+62,HEIGHT-234,x+55,HEIGHT-238)
    process_strip(surface,("Pair once", "See real activity", "Optionally speak", "Keep decisions on PC"),160)
    notes(surface,95)
    surface.showPage()
    frame(surface,"Pairing is recognition, not a promise","A simple connection flow / it does not prove a chat or audio is ready",2)
    process_strip(surface,("Show a fresh code", "Choose that one Cody", "Both sides remember", "Check it is reachable"),HEIGHT-190)
    paragraph(surface,"1. On Cody, open Connect Codex and use the code currently visible on the intended physical device. 2. On the PC, the plugin accepts only one matching offer; ambiguous offers stay errors. 3. Both sides save the relationship only after their protected handshake completes. 4. Later, Cody still checks that this same PC is actually present before treating it as connected.",42,HEIGHT-270,WIDTH-84,12)
    paragraph(surface,"What pairing does: lets the device and the local PC helper recognize each other on the private LAN. What it does not do: prove the Internet, a desktop chat, voice recording, transcription, a model response, or permission to approve an action.",42,HEIGHT-360,WIDTH-84,12,True)
    paragraph(surface,"If the PC or service is absent, Cody keeps the remembered relationship and reports the real unavailable state. It does not quietly choose another PC.",42,HEIGHT-407,WIDTH-84,11)
    notes(surface,95)
    surface.showPage()
    frame(surface,"Speaking to Cody: one careful handoff","Optional voice input / no device audio archive",3)
    process_strip(surface,("Double tap Cody", "Record one message", "PC keeps a private WAV", "Same chat may receive it"),HEIGHT-190)
    paragraph(surface,"A double tap on the visible character requests voice input. The device listens to a single recording, shows its listening state, and ends after real silence or the time limit. A deliberate shake can stop listening or sending; it cannot revoke a submission the desktop has already accepted.",42,HEIGHT-270,WIDTH-84,12)
    paragraph(surface,"While the recording is active, its sound is sent only to the paired PC helper. The device does not save the recording to its SD card or internal flash. The PC helper keeps a private WAV only for the handoff and any needed review of an uncertain result.",42,HEIGHT-361,WIDTH-84,12)
    paragraph(surface,"The PC helper then asks the already configured desktop connection to place that WAV in the same genuine Codex chat. This source path exists, but end-to-end delivery, model support and transcription have not yet been verified on a real device and desktop.",42,HEIGHT-432,WIDTH-84,11,True)
    notes(surface,95)
    surface.showPage()
    frame(surface,"If you change chat while recording","The recording is protected from being sent to the wrong place",4)
    process_strip(surface,("START freezes followed chat", "You change the followed chat", "Plugin refuses the mismatch", "Private WAV stays for review"),HEIGHT-190)
    paragraph(surface,"At the beginning of the recording, the PC helper binds it to the exact chat Cody was genuinely following. Before submitting the finished WAV, it checks that identity again.",42,HEIGHT-270,WIDTH-84,12)
    paragraph(surface,"If you changed the followed chat in the meantime, the plugin rejects the recording instead of sending it into the new chat. It does not silently select the new chat, create a second chat, or automatically retry an ambiguous submission. The private WAV and its status remain available on the PC for review.",42,HEIGHT-340,WIDTH-84,12,True)
    paragraph(surface,"The same cautious result applies when the desktop endpoint is unavailable, when a native turn conflicts, or when acceptance cannot be confirmed: tell the user to check the PC rather than claiming that Cody understood or sent the audio.",42,HEIGHT-422,WIDTH-84,11)
    notes(surface,95)
    surface.showPage()
    surface.save()
    return output.getvalue()


# operation: Exact CCP1 operation identity.
# protocol: Current versioned protocol source text.
# Raises ValueError when the requested packet is absent from the source.
def field_order(operation,protocol):
    packets = re.findall(r"^CCP1\|"+re.escape(operation)+r"\|([^\n]+)",protocol,re.MULTILINE)
    if not packets:
        raise ValueError("No source packet for " + operation)
    if operation == "FIND":
        packets = [packet for packet in packets if "activity=4|events=2" in packet]
    return " -> ".join(re.findall(r"(?:^|\|)([a-z]+)=",packets[-1]))


# surface: Page canvas receiving illustrative, explicitly non-physical Debug views.
# No live channel names or sensor measurements are fabricated by these diagrams.
def debug_view_diagram(surface):
    positions = (168, WIDTH / 2, WIDTH - 168)
    for x, mode in zip(positions, ("face_channels", "character", "normal")):
        surface.setFillColor(CYAN)
        surface.setFont("CodyDocBold", 13)
        surface.drawCentredString(x, 440, mode)
        surface.setFillColor(colors.black)
        surface.setStrokeColor(LINE)
        surface.circle(x, 337, 85, fill=1, stroke=1)
    surface.setFillColor(CYAN)
    surface.setFont("CodyDocBold", 11)
    surface.drawCentredString(positions[0], 358, "LIVE CHANNELS")
    surface.setFillColor(colors.white)
    surface.setFont("CodyDoc", 10)
    for y, text in ((339, "Actual composed rows"), (321, "Count from worker ACK"), (303, "Scroll locally or by tool")):
        surface.drawCentredString(positions[0], y, text)
    cody_identity(surface, positions[1], 347)
    cody_identity(surface, positions[2], 337)
    surface.setFillColor(colors.HexColor("#A9C3D2"))
    surface.setFont("CodyDoc", 8)
    surface.drawCentredString(positions[1], 280, "Canonical diagnostics")
    descriptions = (
        "Request the live list, then use the returned channel_count. No fixed names or count are invented here.",
        "Inspect the native character with canonical snapshot diagnostics. Invalid or stale data stays explicit.",
        "Restore the retained normal route. Session-only views preserve settings, pairing and ordinary work.",
    )
    for x, text in zip(positions, descriptions):
        paragraph(surface, text, x - 107, 229, 214, 11)
    notes(surface)


# start_page: Absolute physical page number of the first technical chapter.
# Raises ValueError if any authored table overlaps notes or footer.
def technical_pdf(start_page):
    protocol = PROTOCOL.read_text(encoding="utf-8")
    output = BytesIO()
    surface = canvas.Canvas(output,pagesize=PAGE_SIZE)
    chapters = []
    # title: Chapter heading.
    # section: Scope and evidence label.
    # columns: Table headings.
    # rows: Current source-derived facts.
    # widths: Table column widths in PDF points.
    def add(title,section,columns,rows,widths):
        chapters.append((title,section,columns,rows,widths))
    add("Ports and packet envelopes","Transport is separate from UI, chat identity and Internet reachability",
        ("CHANNEL","SOURCE CONTRACT","BOUNDARY"),(
        ("UDP 42117 / device","Pairing discovery and normal CCP1 endpoint. The retained device identity is a MAC address encoded as 12 lowercase hexadecimal characters.","The device acquires Wi-Fi and an IP address first. A PC identity contains 32 random lowercase hexadecimal characters."),
        ("UDP 42118 / PC broker","Normal CCP metadata and commands use printable ASCII, canonical | field separators and HMAC proofs.","Ordinary packets: 512 bytes. ACTIVITY/TECH: 1024 bytes. EVENT: 1280 bytes. No application fragmentation."),
        ("UDP 42119 / optional PC audio","Binary CYA1 device packets and CYA2 authenticated ACKs use AES-256-GCM.","49-byte authenticated header + up to 960 PCM bytes + 16-byte tag = 1025 bytes maximum. Audio has a separate ingress owner."),
        ("Loopback PC control / native input","Authenticated local control: 32 workers, a 3-second socket deadline and a 64 KiB body. Native audio uses an explicitly configured loopback WebSocket endpoint.","URL credentials, query strings and fragments are rejected; a literal loopback IP is required. Configuration does not prove attachment to the desktop chat."),
    ),(140,316,WIDTH-84-456))
    add("Commissioning packets and persistence","512-byte CCP1 envelopes; the visible six-character code is never transmitted",
        ("MESSAGE / DIRECTION","CANONICAL FIELD ORDER","MEANING"),(
            ("DISCOVER / OFFER",field_order("DISCOVER",protocol)+"\n"+field_order("OFFER",protocol),"The PC discovers; the device offers. The six-character code admits exactly one verifier match. PBKDF2-HMAC-SHA256 uses 50,000 iterations, a 16-byte salt and a 32-byte verifier."),
            ("PAIR",field_order("PAIR",protocol),"The PC persists its DPAPI candidate first. Link key = HMAC(verifier, link|device|session|pc|nonce). The device writes a separate NVS ccp_pending record."),
            ("PAIRED",field_order("PAIRED",protocol),"Device -> PC: pending write succeeded, not active new pairing. Both old active records remain until commit."),
            ("CONFIRM / ACTIVE",field_order("CONFIRM",protocol)+"\n"+field_order("ACTIVE",protocol),"PC peer proof permits device commit/readback. ACTIVE confirms that stage. Broker promotion still requires the fresh endpoint challenge."),
        ),(140,300,WIDTH-84-440))
    add("Rediscovery proves the observed endpoint","FIND -> READY -> COMMITTED precedes current ACTIVITY / TECH / EVENT",
        ("MESSAGE","CANONICAL FIELD ORDER","ADMISSION RULE"),(
            ("FIND",field_order("FIND",protocol),"Current source requests activity=4 / events=2; current and state describe the open device question. HMAC domain binds exact capability fields."),
            ("READY",field_order("READY",protocol),"The broker supplies a fresh random challenge, the observed device IPv4 address and its UDP port. The ready-v2 proof binds that endpoint; lifetime: 2 seconds."),
            ("COMMITTED",field_order("COMMITTED",protocol),"committed-v2 proof from that exact endpoint consumes the matching challenge once. Only then admit presence, commit recovery and current data."),
            ("Retained link versus fresh proof","DPAPI active/pending PC records; NVS ccp_pairing/ccp_pending","FIND alone cannot establish presence, redirect notifications or release data. Retained pairing alone cannot admit audio START."),
        ),(136,308,WIDTH-84-444))
    add("ACTIVITY and coherent chat identity","1024 bytes; one current snapshot, no transcripts and no manufactured hook",
        ("PROJECTION","FIELDS / LIMITS","OWNER / VALIDITY"),(
            ("ACTIVITY order",field_order("ACTIVITY",protocol),"Authenticated after fresh COMMITTED. chat=SHA256 actual followed session ID; revision is volatile diagnostics, not durable ordering."),
            ("Human metadata","project/branch: at most 64 printable ASCII characters; description: at most 192. Text is hex encoded; - denotes unavailable.","Only working/editing/running admits a description. The branch owner reads the nearest checkout's .git/HEAD or worktree pointer; DETACHED is explicit. Rejected text is not truncated."),
            ("No chat / no new work","zero chat; phase=none; project=-; description=-; branch=-","The latest genuine UserPromptSubmit selects context. Tool/Stop events from background chats cannot steal it. Hook activity expires after 120 seconds; presence expires after 8 seconds."),
            ("Paired ACTIVITY + TECH","One ActivityStore lock, same chat and revision; independent consumed-nonce flags","TECH is admitted only after matching ACTIVITY. No merge with an older projection or freshness fallback; next FIND acquires another coherent pair."),
        ),(140,306,WIDTH-84-446))
    add("TECH: report sources, never infer token usage","1024 bytes; technical-v2 authenticated order is exact",
        ("FIELD GROUP","SOURCE / FORMAT","UNAVAILABLE AND EXPIRY"),(
            ("Exact TECH order",field_order("TECH",protocol),"Lowercase hex identities/model; decimal ms without leading zeroes; - unavailable. All fields and their order are authenticated."),
            ("model / modelage / effort","Full model slug: 1-128 ASCII characters, codes 33-126. Model age follows genuine hook emission. effort=-.","A reported model does not prove rerouting. The model expires after 120 seconds; an absent model requires an absent age. Reasoning effort has no connected source."),
            ("compaction / compactseq / compactage","Real PreCompact/PostCompact on exact chat/turn; none,active,refreshed,orphaned,interrupted,stale.","Only active age denotes observed start; other ages are transition ages. Duplicate/old-turn boundaries rejected. Refreshed never creates token data."),
            ("activityage / turnelapsed / usage","Genuine emission age and observed duration since the prompt. Duration freezes at Stop, Interrupt or SessionEnd.","used, capacity, cumulative, input, output, threshold and usageage must all remain -. No guessed occupancy, model-global denominator, progress percentage or free-token bar."),
        ),(136,312,WIDTH-84-448))
    add("Catalog cost is a qualitative local-day lease","TECH cost/costage/costttl; numbers and billing stay on the PC",
        ("FACT","CONTRACT","EVIDENCE BOUNDARY"),(
            ("Band","unknown / economy / balanced / expensive, matched to the exact observed model.","Qualitative public Standard catalog classification; no currency, bill or inference-cost claim."),
            ("Original age","costage is original public catalog retrieval age. Repeated packets never refresh that source age.","Separate from hook emission, receiver transport timestamps and PC heartbeat."),
            ("Lease","costttl lasts until the next PC local midnight. Duration is positive; catalog age and expiry are at most 26 hours to allow daylight-saving transitions.","Firmware expires the lease using its own monotonic elapsed time, without comparing wall clocks. Unknown requires both durations to be -."),
            ("Independent invalidation","Require model age below 120000 ms. A stale model or transport, an expired lease or a disconnect invalidates cost.","Catalog source, rates, retrieval attempts, errors and classification thresholds remain PC-owned. An unavailable catalog is never fabricated."),
        ),(136,314,WIDTH-84-450))
    add("EVENT, replies and recipient closure","Immutable ticket; 180-second maximum; received is not delivered",
        ("MESSAGE / PAYLOAD","ORDER / BOUNDS","RULE"),(
            ("EVENT",field_order("EVENT",protocol),"1280-byte envelope; ttl is 0-180 remaining seconds, never UTC. Pending/received require at least 1 second. The current nonce admits one publication; repetition never renews the deadline."),
            ("REPLY / REPLYACK",field_order("REPLY",protocol)+"\n"+field_order("REPLYACK",protocol),"512-byte packets; stable choiceID; same answer idempotent, another cannot overwrite. Broker receipt is not recipient confirmation."),
            ("EVENTACK",field_order("EVENTACK",protocol),"Terminal closure acknowledged before publishing successor. Unknown ticket after broker restart produces unavailable/rejected; never reconstruct an answer."),
            ("data v1 order","version, kind, context, title, body, choice_count, choices; each choice retains id, label and detail","ASCII lengths: context 0-24, title 1-24, body 1-63. Questions have 2 or 3 choices; other events have none. Choice label: 1-12; detail: 1-63. Excess or unrepresentable values remain errors."),
            ("Private PC capability","Random 128-bit ticket and 256-bit capability. Poll and cancel require both; acknowledge_answer_id confirms receipt.","The capability never leaves the PC. Explicit caller context never invents verified hook/chat identity or native authorization."),
        ),(138,310,WIDTH-84-448))
    add("Signed controls keep user intent explicit","Clock, preferred name, Debug and reset are independent from audio transport",
        ("OPERATION","FIELDS / OWNER","ADMISSION / OUTCOME"),(
            ("CLOCK / CLOCKACK",field_order("CLOCK",protocol)+"\n"+field_order("CLOCKACK",protocol),"read sends all settings as -1; init adopts a supported host timezone once; set sends explicit values. UTC/Rome/Ho Chi Minh IDs: 0/1/2. Minutes: 0-1439; quiet hours use a half-open window."),
            ("NAME / NAMEACK","NAME: device -> pc -> nonce -> id -> op -> name -> proof. NAMEACK adds status -> error -> available before name. NAMECAP: device -> pc -> nonce -> name=1 -> proof.","Read/set permits at most 24 printable ASCII characters without padded edges. Empty explicitly clears. Persist and read back before applied status. Fresh endpoint-bound capability is single-use."),
            ("DEBUG / DEBUGACK","DEBUG: device -> pc -> nonce -> id -> mode -> scroll -> proof. DEBUGACK adds status -> error -> count before proof. DEBUGCAP: device -> pc -> nonce -> debug=1 -> proof.","Only Debug firmware advertises this single-use capability. Selection does not choose a chat or read hardware independently. pixels_verified remains false; a new command needs a fresh capability."),
            ("RESET / FACTORYRESET",field_order("RESET",protocol)+"\n"+field_order("FACTORYRESET",protocol),"Current user intent and an exact nine-digit confirmation are required. Restart retains configuration. Factory reset removes Wi-Fi, pairing and preferences; firmware, assets and SD remain."),
            ("RESETACK / FACTORYRESETACK",field_order("RESETACK",protocol)+"\n"+field_order("FACTORYRESETACK",protocol),"accepted proves command admission, not boot or erase completion. Fresh presence proves reboot; verified erase precedes the factory-reset reboot."),
        ),(140,314,WIDTH-84-454))
    add("MCP calls and their local authority","Method names are API boundaries; identity and permissions stay typed and explicit",
        ("CALLS","PURPOSE","AUTHORIZATION / LIMIT"),(
            ("ccp_discover_devices / ccp_pair_device","Bounded device discovery; visible-code pairing with explicit pending replacement.","Zero or multiple matching offers fail. A simulated identity must be explicitly included and visibly labelled SIMULATED."),
            ("ccp_connection_status / ccp_list_sessions / ccp_report_progress","Read retained/current state, list observed sessions and explicitly report a work descriptor.","Never manufacture hooks or choose a chat from a label. The latest actual prompt selects the followed context."),
            ("ccp_publish_notification / ccp_ask_question / ccp_poll_request / ccp_cancel_request","Explicit notice/question; capability-bound poll/cancel; recipient answer ACK.","Native approvals stay exclusively on the PC. This reference PDF does not itself authorize messages."),
            ("ccp_read_name / ccp_set_name / ccp_read_clock / ccp_configure_clock / ccp_debug_screen","Bounded preference reads/writes and a requested Debug display.","Readback precedes applied status. Unsupported timezones and persistence failures retain their real cause."),
            ("ccp_reset_device / ccp_factory_reset_device / ccp_audio_status","Explicit restart/factory-reset intent; audio receiver diagnostics without PCM or secrets.","Audio status neither chooses a chat nor starts recording. Reset confirmation comes only from the current user message."),
        ),(250,247,WIDTH-84-497))
    add("Initial resource budgets and failure ownership","Source constants bound work; they are not throughput or anti-jamming guarantees",
        ("OWNER","INITIAL LIMIT","FAILURE / EVIDENCE"),(
            ("Firmware ordinary UDP","8 packets or 5 ms per receive slice; global rate 32/s, burst 64; per IPv4 rate 16/s, burst 32; 16 source slots.","Maintenance still runs. A slice cannot preempt blocking storage or crypto. Runtime tuning and stack headroom remain unverified."),
            ("PC ingress / discovery","Global rate 64/s, burst 128; per IP rate 16/s, burst 32; 64 slots. Discovery: 16 offers, 4 per IP, 128 packets.","Overflow discards the entire result. Pairing verification allows 16 PBKDF2 candidates within 2 seconds; no partial association staging."),
            ("Challenges / control / logs","128 challenges, 2-second lifetime. Local control: 32 workers, 3-second deadline, 64 KiB body. Rejection summary every 5 seconds; broker log: 3 x 2 MiB.","Original source diagnostics and errors survive. Startup runtime.log is a separate logging boundary; no replay or denial-of-service runtime certification."),
            ("Audio / PC artifacts","4 active captures/jobs; 64 transient utterance records; 60-second, 1,920,000-byte PCM ceiling per capture; 10-second abandoned-capture timeout.","Disk quota: 64 MiB and 1024 WAV files. Reject new persistence; never silently delete another utterance. Whole native submission deadline: 15 seconds."),
        ),(147,309,WIDTH-84-456))
    add("Audio is a separate one-way binary channel","Plugin 0.6.5 source increment / UDP 42119 / no device playback or full duplex",
        ("BINARY HEADER OFFSET","FIELD / WIDTH","MEANING"),(
            ("0","magic / 4 bytes","CYA1 comes from the device; CYA2 comes from the PC. Network-order struct !4s6s16s16sIBH defines the 49-byte header."),
            ("4 / 10 / 26","device / 6 bytes; utterance / 16 bytes; discovery nonce / 16 bytes","The 12-character hexadecimal device identity becomes binary. A fresh authenticated peer and discovery nonce admit START; an active utterance remains bound to its original endpoint, credential and nonce."),
            ("42 / 46 / 47","sequence / uint32; kind / uint8; plaintext length / uint16","START=1: sequence 0, no PCM. DATA=2: at most 960 bytes. END=3: no PCM. CANCEL=4: a new sequence/nonce, no PCM. Audio is little-endian PCM16, mono, 16 kHz."),
            ("49 + plaintext length","AES-256-GCM ciphertext + 16-byte tag","The whole header is AAD. Maximum: 49 + 960 + 16 = 1025 bytes. Key = HMAC-SHA256(link_key, cody-audio-v1|utterancehex|discoveryhex)."),
            ("AEAD nonce / ACK","DAT1||uint64(sequence); ACK1||uint64(ackcounter)","A retry repeats identical ciphertext; never reuse a nonce with different plaintext. PC ACK counters strictly increase. ACK plaintext !IB contains the highest contiguous received device sequence and status."),
        ),(153,262,WIDTH-84-415))
    add("A ten-second ring tolerates a stalled PC","160000 samples x 2 bytes = 320000 bytes PSRAM; metadata and transport buffers are additional",
        ("STAGE","DEVICE / PC OWNER","BOUNDARY"),(
            ("Capture and display",'Existing AFE enhanced mono 16 kHz PCM and VAD. Wake word or double tap requests Listening; capture starts with the next real AFE block, without preroll. At the same lower position, alternate exact ASCII captions "Shake to fade" and "We will send the audio to Codex for transcription"; scroll the long caption inside the clipped row.',"Hide Cody. Synthetic wave: four phase-shifted colored curves in a 440 x 40 band; 16 poses at 8 FPS, Q8 antialiasing and alpha overlaps, no runtime trig or voice-amplitude tracking. One row cycles model -> cost band -> thinking effort -> branch -> project, skipping unavailable values with existing fades/scrolling. No fixed cost pill. Elapsed and remaining seconds show the 60-second limit."),
            ("Ring and sender","Keep PCM until cumulative authenticated ACK. Four DATA packets, each 480 samples / 960 bytes. Retry identical packets every 100 ms; transport every 5 ms, at least one tick.","Ten-second PSRAM/grace survives CCP loss only for the same authenticated key and IP. Overflow fails; no overwrite or SD/flash fallback. Capture ends at 1.2-second speech silence or 60 seconds."),
            ("PC receiver / persistence","Freeze the genuine START chat. Discard out-of-order DATA; ACK the contiguous prefix. Persist completed 16 kHz, 16-bit mono WAV under private runtime artifacts.","Separate UDP/native workers: 4 captures/jobs, 64 records, 60-second PCM limit. Disk quota: 64 MiB/1024 WAV. START requires recent readiness for the followed chat."),
            ("Pause / deliberate shake","Shake cancels Listening/Sending; no voice Cancel button. Microphone pause stops capture/transport. Transport failure after transmitted END is Unconfirmed.","No automatic wake/restart. CANCEL cannot revoke native submission in flight; retain its actual outcome. The hint is not proof of transcription."),
        ),(139,318,WIDTH-84-457))
    add("PC receipt, native acceptance and work completion","Native endpoint is an explicit integration dependency, not a discovered guarantee",
        ("ACK / NATIVE STEP","SOURCE BEHAVIOR","HONEST OUTCOME"),(
            ("Audio ACK statuses","1: START accepted. 2: PCM received. 3: whole utterance received. 4: native Codex accepted. 5: failed/cancelled. 6: native input unconfirmed.","END receipt is not native acceptance. Native acceptance is not task completion. Unconfirmed submissions never repeat automatically."),
            ("Read-only native readiness","CODY_CODEX_AUDIO_ENDPOINT selects an existing literal-loopback WebSocket. Probe initialize and exact thread/read within 5 seconds; refresh every 5 seconds, freshness limit 10 seconds. Reject authenticated START without recent readiness for the followed chat.","The probe does not prove localAudio acceptance, model support or transcription. cryptography and websocket-client remain required; optional token file: CODY_CODEX_AUDIO_TOKEN_FILE. Never expose token values."),
            ("Native localAudio",'input=[{"type":"localAudio","path":completedWAV}]. Preserve the START chat; resolve its current hook turn at submission. Active turn: turn/steer(expectedTurnId); otherwise thread/resume then turn/start. clientUserMessageId is the recording hex ID.',"Revalidate followed-chat identity and cancellation. Concurrent native turn conflicts fail explicitly; retain the WAV and never retry automatically. The correlation ID provides no idempotency guarantee."),
            ("Native approvals and uncertainty","The whole submission has a 15-second monotonic deadline. Authorization and user questions stay on the desktop. Status exposes recording ID, native turn, WAV artifact and uncertainty.","Explicit rejection means Failed. Uncertain submission means status 6: Check on your PC. Endpoint attachment, installation, account/backend, dependencies and physical audio remain unverified."),
        ),(150,314,WIDTH-84-464))
    add("CCP and the plugin: who does what?","Operator guide / source inventory / the Codex plugin is not a second device server",
        ("OWNER","RESPONSIBILITY","CONFIGURATION / LIMIT"),(
            ("Cody firmware / client","Own Wi-Fi, canonical measurements, screen composition, durable preferences and authenticated commands. Discover the retained PC and receive its genuine metadata.","Release supports customer operations; Debug additionally advertises DEBUGCAP. A build choice is not a remote permission switch."),
            ("CCP PC broker / server","One hidden Windows Python runtime serves MCP clients and trusted hooks. Own DPAPI pairing journals, observed chats, question tickets, delivery acknowledgement and optional audio reception.","MCP attaches using the installed .mcp.json. No OpenAI API key is required for normal CCP; no Codex credential goes over LAN."),
            ("Codex plugin / skills","Package the MCP entrypoint, ten native hooks and three skills. Tools express operations; skills constrain when an agent may use them. The latest genuine prompt chooses the followed chat.","Install/enable the local package and review hooks separately. Editing this repository does not update an installed plugin or flash Cody."),
            ("Native audio endpoint","Optional existing loopback WebSocket receives localAudio in the genuine followed chat. The broker receives encrypted PCM on a separate UDP channel.","Explicit endpoint/dependencies/account integration required. Native acceptance is not transcription or work completion; no second chat is created to bypass missing integration."),
        ),(148,306,WIDTH-84-454))
    add("Configure once; diagnose the correct layer","Operator setup / no action in this document authorizes installation, trust or hardware changes",
        ("STEP","OPERATOR ACTION","WHAT IT ESTABLISHES"),(
            ("1 / Install plugin","Make Python available to the desktop environment. Register the local catalog with codex plugin marketplace add . from the repository root if absent. Install/enable CCP in Cody local plugins; open a chat loading that package.","Installed version and package path must match the intended source. Canonical manifest: .codex-plugin/plugin.json. Do not add a competing root plugin.json."),
            ("2 / Review hooks","Open /hooks in a local Codex session; review and authorize the discovered CCP command hooks. Send a real prompt afterwards.","Installation does not grant hook trust. Empty sessions call for environment/trust diagnosis; never fabricate hooks or edit trust records to create activity."),
            ("3 / Pair physical Cody","Allow private LAN Python UDP 42117/42118. Open Setup > Connect Codex on the intended device; discover and use only its currently visible six-character code. Read ccp_connection_status afterwards.","Retained identity, pending pairing and authenticated current presence differ. Public Internet is independent of local LAN. Simulator inclusion is false unless explicitly requested."),
            ("4 / Optional audio","Install mcp/requirements-audio.txt in the plugin Python environment. Configure CODY_CODEX_AUDIO_ENDPOINT for an existing literal-loopback WebSocket; optional CODY_CODEX_AUDIO_TOKEN_FILE points to its existing token file. Allow audio UDP 42119 as needed.","ccp_audio_status reports dependency/bind/native errors and recent readiness. Never publish tokens. The read-only probe does not prove localAudio/model support or physical capture."),
        ),(125,362,WIDTH-84-487))
    add("Customer tools: connection and actual work","Complete tool inventory / available without a Debug screen capability",
        ("EXACT MCP TOOL","USE / ARGUMENTS","RESULT / BOUNDARY"),(
            ("ccp_discover_devices / ccp_pair_device","Discover current offers; pair the unique cryptographic match for the visible code. include_simulator defaults false. replace_pending:true requires explicit replacement intent and a new visible code.","Never select the first responder. A request or saved candidate is not completed pairing. Ambiguous pairing is not automatically repeated."),
            ("ccp_connection_status / ccp_list_sessions","Read retained identities, current presence, broker status and genuine hook-observed sessions. followed identifies the chat, not its project label.","No manual chat selector, transcript reader or window-focus inference. Status does not prove displayed pixels."),
            ("ccp_report_progress","Use the exact currently followed session_id and turn_id; report actual English ASCII work, at most 192 characters.","No invented percentage, estimate, completion, prompt excerpts or shell output. A stale identity is rejected; re-read rather than overwrite selection."),
            ("ccp_ask_question / ccp_publish_notification","Ask a self-contained two/three-choice question, or send known milestone/read-only PR facts. PR reports require exact head and real complete/current check data.","Private ticket/capability remains on PC. Questions do not grant native approvals. No PR merge, approve, rerun, close or comment operations."),
            ("ccp_poll_request / ccp_cancel_request","Poll the caller's private ticket; after received, acknowledge its answer_id in a second poll. Cancel only a ticket whose capability belongs to the caller.","Only delivered proves recipient receipt. Poll waits at most 15 seconds. Default question lifetime 180 seconds; expiry never supplies a default choice."),
        ),(182,294,WIDTH-84-476))
    add("Customer tools: preferences, voice and reset","No Debug build required / fresh authenticated capability still required where applicable",
        ("EXACT MCP TOOL","USE / ARGUMENTS","RESULT / BOUNDARY"),(
            ("ccp_read_clock / ccp_configure_clock","Select device_id. Supported timezone values: UTC, Europe/Rome, Asia/Ho_Chi_Minh. Quiet-hours start/end use HH:MM; omit preferences not requested.","Fresh clock capability and awake target. Applied with error 0 confirms durable result. Ambiguous write: read after a fresh heartbeat; do not repeat automatically."),
            ("ccp_read_name / ccp_set_name","Read stored availability or set preferred_name explicitly supplied by the user: up to 24 printable ASCII characters, no edge spaces. Empty string clears only the name.","Fresh name capability. Never infer a name from login, repository, profile or pairing identity; never truncate/transliterate unsupported input."),
            ("ccp_audio_status","Read receiver/dependency/bind status, native readiness, utterance/native turn identity, retained WAV artifact and uncertainty.","No PCM or secret export. This tool does not start recording, select another chat or prove transcription. Wake word/double tap owns the device trigger."),
            ("ccp_reset_device","Restart the intended paired target only with current-message explicit restart intent and exact confirmation_code string.","Preserves Wi-Fi, pairing and preferences. accepted proves admission only. New authenticated presence is separate boot evidence."),
            ("ccp_factory_reset_device","Erase settings only with current-message explicit factory-reset intent and exact confirmation_code string.","Clears preferences, Wi-Fi and pairing; preserves firmware, assets and SD. Verified erasure precedes reboot. Do not infer wipe from LAN disappearance."),
        ),(190,288,WIDTH-84-478))
    add("Debug screen control: observe, do not override","ccp_debug_screen / only current authenticated Debug devices advertise the capability",
        ("MODE / CALL","OBSERVED OUTPUT","PREREQUISITES / HONEST RESULT"),(
            ("face_channels","Show the actual composed character channels as a scrollable list. Read real channel_count from the worker ACK; scroll is the absolute first row, 0-255. Touch may scroll locally.","Select the current device_id through ccp_connection_status. Use mode:face_channels, scroll:0 initially; do not assume a fixed count or invent channel values."),
            ("character","Show the native character and diagnostics from the canonical snapshot underneath, including actual IP or explicit unavailable/stale state.","Use mode:character and scroll:0. No consumer-side sensor/I2C reads, fabricated freshness or remote face-channel override."),
            ("normal","Restore the retained normal route. Debug views are session-only; they do not change durable preferences or paired identity.","Use mode:normal and scroll:0. The request must refer to the intended currently paired Debug target; do not pair again to bypass missing capability."),
            ("Application / timeout","applied comes only from an authenticated worker ACK; rejected preserves the real error. A timeout means unknown; never infer success or automatically repeat.","pixels_verified:false means the ACK does not attest visible pixels. Release has no Debug capability. SIMULATED inclusion needs explicit simulator intent."),
            ("Not exposed","No arbitrary shell, file/mount browser, raw hardware register access, channel setter, remote screenshot/capture, firmware updater or generic settings writer.","A tool appearing in the PC catalog does not grant its capability to Release firmware. Host capture/flash/check commands are separate operations, not CCP commands."),
        ),(135,325,WIDTH-84-460))
    add("Restart, factory reset and the first-boot flag","Different operations / neither this manual nor an old message provides destructive authorization",
        ("INTENTION","SOURCE CONTRACT","WHAT TO DO INSTEAD OF GUESSING"),(
            ("Restart","ccp_reset_device uses signed RESET / RESETACK. Both Debug and Release can request the power owner to restart while preserving saved state.","Require corresponding explicit intent and the exact nine-digit confirmation string in the current user message. Never derive it from documentation, memory or an earlier request."),
            ("Factory reset","ccp_factory_reset_device uses signed FACTORYRESET / FACTORYRESETACK. Settings owner erases and verifies the intended durable settings before requesting reboot.","Requires its separate erasure intent and exact current-message confirmation string. Do not use it to fix a routine disconnect or firmware update. PC retained pairing is not automatically removed."),
            ("Admission / uncertainty","One transmission and at most five seconds for authenticated admission. accepted, not_sent and ambiguous are different outcomes.","An accepted ACK proves neither later boot nor successful wipe. Do not automatically retry ambiguity, independently re-pair or erase the PC record."),
            ("First boot","CODY settings store a persisted first-boot preference. CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT is a compile-time development override; it never overwrites a stored false or repairs a read error.","There is no MCP/CCP runtime first-boot toggle. The internal persist_first_boot function is not a remote command. Factory reset reopens setup by erasing broader settings, not by toggling one flag."),
        ),(144,310,WIDTH-84-454))
    add("Three skills, ten hooks and separate host diagnostics","Source paths: plugins/cody-codex-plugin/skills and hooks/hooks.json; firmware command handlers",
        ("CONTRACT","SUPPORTED OPERATIONS","BOUNDARY / EXAMPLE REQUEST"),(
            ("cody-codex","Pair/status; genuine progress; notices; semantic questions with recipient acknowledgement; explicit confirmed restart or factory reset.","Examples: Show Cody connection status; send a known milestone. Skills guide tool use; they are not additional protocol commands or permission grants."),
            ("cody-debug","face_channels, character and normal through ccp_debug_screen on the intended authenticated Debug target.","Examples: Show the live face channels; show character diagnostics; restore Cody's normal screen. Requests do not authorize settings changes or restart."),
            ("cody-settings","Read/configure timezone and quiet hours; read/set/clear explicitly supplied preferred name. Preserve omitted and unrelated preferences.","Examples: Set timezone to Rome; quiet hours 22:00 to 07:00; disable quiet hours while retaining the schedule. Unsupported input is rejected."),
            ("Ten real hooks","SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, PermissionRequest, Stop, Interrupt, PreCompact, PostCompact and SessionEnd.","A genuine prompt selects the followed chat. PermissionRequest leaves authorization on PC; Stop means reply ready, not task success. Hooks require native review/trust."),
            ("USB / local engineering CLI","scripts/device.ps1 is the sole public CLI: capture, monitor, sd-log, reset, flash, erase, recover; named checks/generators and simulator routes remain separate.","No CCP mount browser exists. USB logs and owner status can explain storage errors after a separately authorized operation. No implicit SD format, firmware flash or test suite."),
        ),(148,305,WIDTH-84-453))
    for offset,(title,section,columns,rows,widths) in enumerate(chapters):
        frame(surface,title,section,start_page+offset)
        technical_table(surface,columns,rows,widths)
        surface.showPage()
    title = "Debug views: three explicit requests"
    frame(surface,title,"Illustrative request diagrams, not device captures / no fabricated channels or measurements",start_page+len(chapters))
    debug_view_diagram(surface)
    surface.showPage()
    surface.save()
    return output.getvalue(),[heading for heading,*_ in chapters]+[title]


# records: All semantic historical revisions retained from the source atlas.
# start_page: Absolute first page of the final dated register.
# section_title: The final register heading, including its labelled Security section.
# Raises ValueError when a dated row exceeds a complete page.
def compact_history(records,start_page,section_title="Revision register"):
    from reportlab.platypus import SimpleDocTemplate
    output = BytesIO()
    security = "Security" in section_title
    body = ParagraphStyle("CommunicationHistory",fontName="CodyDoc",fontSize=8.4 if security else 9,
                          leading=10 if security else 12,textColor=INK,spaceAfter=2 if security else 4)
    title = ParagraphStyle("CommunicationHistoryTitle",parent=body,fontName="CodyDocBold",fontSize=9 if security else 10,
                           leading=11 if security else 13)
    small = ParagraphStyle("CommunicationHistorySmall",parent=body,fontSize=8,leading=10,textColor=MUTED)
    header = ParagraphStyle("CommunicationHistoryHeader",parent=small,fontName="CodyDocBold",textColor=colors.white)
    rows = [[plain_paragraph("DATE / TYPE",header),plain_paragraph("DESCRIPTION / ORIGINAL PAGE AND SCREEN IDS",header)]]
    for record in records:
        left = [plain_paragraph(record.date,small),plain_paragraph(record.kind.upper(),small)]
        right = [plain_paragraph(record.title,title),plain_paragraph("Affected: "+record.affected,small)]
        right.extend(plain_paragraph(text,body) for text in record.paragraphs)
        if record.evidence:
            right.append(plain_paragraph("Evidence / limits: "+record.evidence,small))
        if record.image_path:
            from reportlab.platypus import Image as PdfImage
            artwork = PdfImage(record.image_path)
            scale = min(270/artwork.imageWidth,94/artwork.imageHeight)
            artwork.drawWidth,artwork.drawHeight = artwork.imageWidth*scale,artwork.imageHeight*scale
            right.append(artwork)
            right.append(plain_paragraph(record.image_caption,small))
        rows.append([left,right])
    table = Table(rows,colWidths=[118,WIDTH-84-118],repeatRows=1,splitByRow=1,splitInRow=0)
    table.setStyle(TableStyle([("BACKGROUND",(0,0),(-1,0),INK),("BACKGROUND",(0,1),(0,-1),colors.HexColor("#EDF5FA")),
        ("VALIGN",(0,0),(-1,-1),"TOP"),("GRID",(0,0),(-1,-1),0.5,LINE),
        ("LEFTPADDING",(0,0),(-1,-1),9),("RIGHTPADDING",(0,0),(-1,-1),9),
        ("TOPPADDING",(0,0),(-1,-1),8),("BOTTOMPADDING",(0,0),(-1,-1),8)]))
    # surface: Drawing owner for the paginated final register.
    # state: ReportLab physical page state.
    def register_frame(surface,state):
        surface.setFillColor(CYAN)
        surface.setFont("CodyDocBold",9)
        surface.drawString(42,HEIGHT-29,"CODY / CCP COMMUNICATION ATLAS / FINAL REVISION REGISTER")
        surface.setFillColor(INK)
        surface.setFont("CodyDocBold",23)
        surface.drawString(42,HEIGHT-59,section_title)
        surface.setFont("CodyDoc",8)
        surface.setFillColor(MUTED)
        surface.drawString(42,HEIGHT-75,"Original dates, screen IDs, decisions and evidence are retained. Source code does not prove device behavior.")
        surface.setStrokeColor(LINE)
        surface.line(42,33,WIDTH-42,33)
        surface.drawString(42,21,"Cody / confidential / source, host, deployment and physical evidence are distinct")
        surface.drawRightString(WIDTH-42,21,str(start_page+state.page-1))
    document = SimpleDocTemplate(output,pagesize=PAGE_SIZE,leftMargin=42,rightMargin=42,topMargin=88,bottomMargin=45)
    document.build([table],onFirstPage=register_frame,onLaterPages=register_frame)
    return output.getvalue()


# argv: Optional CLI arguments supplied by the public device.ps1 generator route.
# Raises OSError for inaccessible output files.
# Raises ValueError for unrepresentable authored layout or invalid register entries.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args(argv)
    register_fonts()
    preserve_sources(STAGING/"protected-sources")
    writer = PdfWriter()
    introduction = PdfReader(BytesIO(introduction_pdf()))
    technical_bytes,technical_titles = technical_pdf(16)
    technical = PdfReader(BytesIO(technical_bytes))
    core = PdfReader(BytesIO(core_pdf(start_page=5,crosswalk_page=16+len(technical.pages))))
    writer.append(introduction)
    writer.append(core,pages=(0,11))
    writer.append(technical)
    writer.append(core,pages=(11,13))
    writer.add_outline_item("System map and owners",0)
    writer.add_outline_item("Connection, activity and question semantics",2)
    writer.add_outline_item("Wire protocol, MCP and audio",13)
    for offset,title in enumerate(technical_titles):
        writer.add_outline_item(title,13+offset)
    writer.add_outline_item("Source crosswalk",len(writer.pages)-2)
    writer.add_outline_item("Review worksheet",len(writer.pages)-1)
    first_register_page = len(writer.pages)
    history = revision_history()
    history.insert(0,RevisionRecord("2026-10-04","Change","One intended PC on a busy LAN",(
        "Cody retains one explicit PC identity even among 100 PCs; other responders do not replace it. First commissioning, a persisted relationship and authenticated current presence are separate facts. The original connection worksheet remains a separate writing page.",),
        "Original Connection pages 9 and 14; screens 12-16, 32, 35B, 40-42","Original design reference, not a current capture."))
    history.extend((RevisionRecord("2026-10-11","Errata","English correction retained from both earlier atlases",(
        "Service original page 33 and legacy Connection original page 37 translated historical Italian headings, diagram captions, body copy and footers into English. Original dates, screen IDs, technical values, artwork meaning and evidence boundaries were preserved. This dated correction is now consolidated in the canonical final grid.",),
        "Service page 33; Codex Connection page 37; original historical pages referenced therein","Document language and visual review only; no firmware or physical validation."),
        RevisionRecord("2026-10-12","Change","One canonical CCP communication reference",(
        "Unify the existing CCP Connection Atlas, former Service Atlas and legacy Codex Connection Atlas into coherent system, commissioning, wire/API, audio and evidence chapters. Keep the separate four-page A5 pairing quickstart as the nontechnical user guide. Preserve earlier meaningful decisions, original page/screen crosswalks and dated Errata, Addendum, Change and Security records.",
        "Current packet fields, order and ceilings; hook/technical projections; clock/name/debug/reset; MCP calls; and resource budgets are source-derived. Metadata remains authenticated plaintext. Audio is a separate AES-256-GCM source increment and does not strengthen six-character commissioning.",
        "Voice documentation follows source version 0.6.5 and the 12 October UI decision: enhanced PCM from the next real AFE block after wake detection, a ten-second PSRAM ring, authenticated UDP 42119, frozen native thread/turn identity and localAudio input. Capture lasts at most 60 seconds; deliberate shake cancels Listening or Sending without a voice Cancel button. Hide Cody. The upper synthetic wave is 40 pixels high. One central row cycles model, cost band, thinking effort, branch, then project; unavailable values are skipped using the existing fade/scroll style. There is no fixed cost pill. The lower hint alternates Shake to fade and We will send the audio to Codex for transcription in the same position; the long message scrolls inside the clipped row. Show both elapsed and remaining seconds. Native endpoint attachment, account/backend, dependencies, installation, cryptographic wire checks, device stack headroom and physical capture/delivery remain unverified. Whole native deadline: 15 seconds; uncertainty never becomes success.",),
        "All three technical source PDFs; A5 quickstart context; current CCP1.md, ccp_voice_input.py and LUNASHELL_voice_input.cpp",
        "PDF generation, rendering and visual review only. No tests, scans, configure/build/size, simulator, flash, hardware or native-input execution.")))
    history.append(RevisionRecord("2026-10-12","Change","Audio review: bounded window, readiness and honest uncertainty",(
        "Replace the one-packet DATA window with four 480-sample packets. Cumulative authenticated ACK names the highest contiguous sequence; out-of-order DATA is discarded while that prefix is acknowledged. Retry identical ciphertext after 100 ms. Transport runs every 5 ms with at least one scheduler tick. Ten-second unacknowledged PSRAM/grace may survive CCP disconnection only for the same authenticated key and IP. Transport failure after transmitted END remains Unconfirmed.",
        "Add a periodic read-only native initialize/thread-read probe: 5-second whole budget, refresh every 5 seconds, freshness 10 seconds. START requires recent readiness matching the genuine followed chat. Readiness does not certify localAudio, model support or transcription. Keep the START chat frozen, then resolve its current hook turn at submission for expectedTurnId. A concurrent native turn conflict fails explicitly; keep the WAV and do not retry automatically.",
        "Use the recording hex ID as clientUserMessageId for correlation without promising idempotency. Status exposes recording ID, native turn, WAV artifact and uncertainty. These source changes supersede the earlier single-packet and START-frozen-turn description without erasing that dated history.",),
        "Current main pages 24-26; original voice screens 28-30; firmware voice owner and PC native-audio broker",
        "Source implementation and PDF authoring only; tests, build, scans, installation and physical capture/delivery remain unverified."))
    history.append(RevisionRecord("2026-10-12","Addendum","Operator capabilities, customer tools and Debug boundaries",(
        "Add a source-backed operator guide for firmware/client, Windows CCP broker/server and the Codex plugin. Record installation and hook-review prerequisites, physical LAN pairing and explicit optional loopback audio configuration without publishing keys or tokens.",
        "List all seventeen MCP tools and all three supported skills, plus the ten genuine hook types. Separate customer/Release operations from Debug-only face_channels, character and normal views. Worker applied ACK is not physical pixel evidence; unknown timeouts and ambiguous mutations never become success or automatic retries.",
        "Document restart versus verified factory erasure, current-message intention and confirmation requirements, preserved data and admitted-versus-completed evidence. There is no CCP mount browser, arbitrary shell, face override or runtime first-boot setter; preserve the explicit persisted/compile-time distinction and separate USB engineering operations.",),
        "Original Communication main pages 1-28, protocol/MCP control chapters; added operator pages 27-34; CODY first-boot settings, plugin catalog, skills and command handlers",
        "Static source review and PDF generation/rendering/visual inspection only; no live tools, settings changes, tests, simulator, build, hardware, reboot, reset or audio submission."))
    history.append(RevisionRecord("2026-10-12","Change","Plain-language pairing and voice handoff",(
        "Add four illustrated plain-English opening pages to the canonical atlas. They explain the separate roles of Cody, the paired PC helper and the genuine desktop chat without requiring the reader to understand packet formats or endpoints.",
        "Document the current source rule for voice input: a visible-character double tap requests one recording; the PC binds START to the genuinely followed chat; a changed followed chat rejects the finished recording before native submission. The plugin keeps the private WAV for review, never chooses the new chat, creates no substitute chat, and never retries ambiguous native delivery automatically.",
        "Clarify that device audio is neither written to SD nor internal flash, and that actual end-to-end desktop acceptance, model support and transcription are still unverified. The detailed source chapters and their existing evidence boundaries remain after the new overview pages.",),
        "Current atlas opening pages 1-4; source voice screens 28-30; original communication pages 1-28; CCP voice input source increment",
        "Static source review plus PDF generation/rendering/visual inspection only. No tests, configure/build/size, simulator, flash, hardware capture, pairing, or native audio submission."))
    register = compact_history(history,first_register_page+1)
    writer.append(PdfReader(BytesIO(register)))
    writer.add_outline_item("Revision register", first_register_page)
    first_security_page = len(writer.pages)
    security = compact_history(security_history(),first_security_page+1,"Security")
    writer.append(PdfReader(BytesIO(security)))
    writer.add_outline_item("Security", first_security_page)
    writer.add_metadata({"/Title": "Cody CCP Communication Atlas v1", "/Author": "Umberto Giacobbi", "/Subject": "Canonical device/PC roles, commissioning, protocol, native voice input and dated history"})
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with arguments.output.open("wb") as destination:
        writer.write(destination)
    render_directory = STAGING/"final-render"
    render_directory.mkdir(parents=True,exist_ok=True)
    for pattern in ("page-*.png","contact-*.png"):
        for old in render_directory.glob(pattern):
            old.unlink()
    document = pymupdf.open(arguments.output)
    thumbnails = []
    for index,page in enumerate(document):
        image = page.get_pixmap(matrix=pymupdf.Matrix(1.15,1.15))
        image.save(render_directory/f"page-{index+1:02}.png")
        thumbnail = Image.frombytes("RGB",[image.width,image.height],image.samples)
        thumbnail.thumbnail((421,298))
        thumbnails.append(thumbnail)
    for index in range((len(thumbnails)+5)//6):
        contact = Image.new("RGB",(1263,596),"#c7d1db")
        for offset,thumbnail in enumerate(thumbnails[index*6:index*6+6]):
            contact.paste(thumbnail,((offset%3)*421,(offset//3)*298))
        contact.save(render_directory/f"contact-{index+1:02}.png")
    (STAGING/"receipt.json").write_text(json.dumps({"pages":len(document),"main_pages":first_register_page,
        "revision_records":len(history),"security_records":len(security_history()),
        "protected_sources":"protected-sources/manifest.json","evidence":"PDF authoring; runtime validation deferred"},indent=2),encoding="utf-8")
    print(f"Generated {arguments.output} ({len(writer.pages)} pages)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
