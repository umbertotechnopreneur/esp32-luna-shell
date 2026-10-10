"""Consolidate the two requested atlases from their main pages, retaining history."""

import argparse
from io import BytesIO
from pathlib import Path

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.pdfgen import canvas

from generate_cody_setup_revision import paragraph, register_fonts


ROOT = Path(__file__).resolve().parents[2]
REVISION = "CODY-AUTO-CONTEXT-20261010"
DATE = "10 October 2026"
INK = HexColor("#142737")
MUTED = HexColor("#536776")
BLUE = HexColor("#359FE7")
TEAL = HexColor("#008E87")
PALE = HexColor("#F1F6F9")
RULE = HexColor("#D6E1E8")
WHITE = HexColor("#FFFFFF")


# pdf: Destination vector canvas.
# dimensions: Source landscape page size in points.
# kind: Navigation or Planning atlas identity.
# title: Current page heading.
# subtitle: Current scope, referenced by screen ID.
# number: New physical page number, never a screen identity.
def frame(pdf, dimensions, kind, title, subtitle, number):
    width, height = dimensions
    paragraph(pdf, "CODY / " + kind.upper() + " ATLAS / CURRENT FLOW", 34, 23,
              width - 68, height, 8, True, TEAL)
    paragraph(pdf, title, 34, 52, width - 68, height, 27, True)
    paragraph(pdf, subtitle, 34, 99, width - 68, height, 10, False, MUTED)
    pdf.setStrokeColor(RULE)
    pdf.line(34, height - 128, width - 34, height - 128)
    pdf.line(34, 46, width - 34, 46)
    pdf.setFillColor(MUTED)
    pdf.setFont("SetupBody", 7)
    pdf.drawString(34, 31, "Umberto Giacobbi | hello@umbertogiacobbi.biz | confidential")
    pdf.drawString(34, 19, DATE + " | " + REVISION + " | Refer to screen IDs")
    pdf.drawRightString(width - 34, 19, str(number))


# pdf: Destination canvas.
# title: Short section label.
# copy: Literal explanatory text.
# x: Left column coordinate.
# top: Top edge measured down the page.
# width: Available column width.
# height: Page height.
# Raises ValueError when authored text exceeds the allocated section.
def section(pdf, title, copy, x, top, width, height):
    end = paragraph(pdf, title, x, top, width, height, 13, True)
    end = paragraph(pdf, copy, x, end + 8, width, height, 10.5)
    if end > top + 154:
        raise ValueError("Atlas section exceeds its allocated row: " + title)


# pdf: Destination canvas.
# dimensions: Source page geometry.
# kind: Atlas identity.
# title: Main heading.
# subtitle: Bounded scope line.
# entries: Exactly four independent sections laid out in two columns.
# number: Physical page number.
# Raises ValueError for an invalid section count or text overflow.
def sections_page(pdf, dimensions, kind, title, subtitle, entries, number):
    if len(entries) != 4:
        raise ValueError("A two-column atlas page requires four sections")
    frame(pdf, dimensions, kind, title, subtitle, number)
    width, height = dimensions
    column = (width - 94) / 2
    for index, (label, copy) in enumerate(entries):
        section(pdf, label, copy, 34 + (index % 2) * (column + 26),
                155 + (index // 2) * 185, column, height)
    pdf.showPage()


# pdf: Destination canvas.
# center_x: Logical center on the document page.
# center_y: Center in PDF coordinates, measured from the bottom.
# caption: Bounded illustrative status caption; never device evidence.
def screen_circle(pdf, center_x, center_y, caption):
    pdf.setFillColor(HexColor("#000000"))
    pdf.circle(center_x, center_y, 66, stroke=0, fill=1)
    pdf.setFillColor(WHITE)
    for dx, dy, radius in ((-20, 16, 14), (-3, 26, 20), (19, 16, 16)):
        pdf.circle(center_x + dx, center_y + dy, radius, stroke=0, fill=1)
    pdf.setFillColor(INK)
    for dx in (-10, 10):
        pdf.circle(center_x + dx, center_y + 24, 2.5, stroke=0, fill=1)
    pdf.setFillColor(WHITE)
    pdf.setFont("SetupBold", 9)
    pdf.drawCentredString(center_x, center_y - 23, caption)
    pdf.setFillColor(HexColor("#7ACCC4"))
    pdf.setFont("SetupBody", 7)
    pdf.drawCentredString(center_x, center_y - 38, "From Codex")


# pdf: Destination canvas.
# dimensions: Source page dimensions.
# kind: Atlas identity.
# title: Page heading.
# subtitle: Scope and screen IDs.
# cards: Three screen ID, caption and behavior descriptions.
# note: Additional ownership or evidence boundary.
# number: Physical page number.
# Raises ValueError when descriptions exceed their allotted space.
def screens_page(pdf, dimensions, kind, title, subtitle, cards, note, number):
    if len(cards) != 3:
        raise ValueError("A screen page requires three cards")
    frame(pdf, dimensions, kind, title, subtitle, number)
    width, height = dimensions
    card_width = (width - 100) / 3
    for index, (identity, caption, copy) in enumerate(cards):
        x = 34 + index * (card_width + 16)
        pdf.setFillColor(PALE)
        pdf.roundRect(x, height - 445, card_width, 298, 12, stroke=0, fill=1)
        paragraph(pdf, identity, x + 14, 160, card_width - 28, height, 12, True, TEAL)
        screen_circle(pdf, x + card_width / 2, height - 260, caption)
        end = paragraph(pdf, copy, x + 14, 342, card_width - 28, height, 10)
        if end > 432:
            raise ValueError("Screen card text overflow: " + identity)
    paragraph(pdf, note, 34, 467, width - 68, height, 10, False, MUTED)
    pdf.showPage()


# pdf: Destination canvas.
# dimensions: Source page dimensions.
# kind: Requested atlas identity.
# number: Cover page number.
def cover_page(pdf, dimensions, kind, number):
    frame(pdf, dimensions, kind, "Cody / " + kind + " Atlas",
          "Current journey from screen 01 onward. Historical addenda and errata follow the main atlas.", number)
    width, height = dimensions
    paragraph(pdf, "CODEX LEADS.\nCODY FOLLOWS.", 34, 162, 445, height, 32, True)
    paragraph(pdf, "CCP automatically sends the chat context when you submit work in Codex. "
              "Cody shows connection and activity; no chat-selection step is required.",
              34, 274, 455, height, 15)
    paragraph(pdf, "Screen IDs are the stable reference. The main pages describe the current "
              "decision; older dated appendices document how the design changed.",
              34, 360, 455, height, 11, False, MUTED)
    screen_circle(pdf, width - 177, height - 272, "Waiting for Codex")
    paragraph(pdf, "15 / Connected", width - 280, 369, 205, height, 15, True, TEAL)
    paragraph(pdf, "Pairing and presence are implemented. Automatic chat-following is "
              "prepared in CCP 0.4; genuine-hook and physical delivery verification remain pending. "
              "PR data, answers and voice transcription remain planned.",
              34, 446, width - 68, height, 10)
    pdf.showPage()


# pdf: Destination canvas.
# dimensions: Source page geometry.
# kind: Atlas identity.
# number: Updated inventory page number.
def inventory_page(pdf, dimensions, kind, number):
    frame(pdf, dimensions, kind, "Screen inventory", "Stable IDs; lifecycle variants do not add a chat picker.", number)
    groups = [
        ("01-09 / Entry and LAN", "01 Sleeping; 02 Waking; 03 Ready; 04 Welcome; 05 Let's pair; "
         "06 Install QR; 06-bis pairing QR; 07 Pairing; 08 Try again; 09 LAN ready. "
         "39 is the temporary Debug handoff."),
        ("10-16 / Personalize and connect", "10 Make it yours; 36 Settings; 11 Skin; 12 Connect Codex code; "
         "13 You're all set; 14 Looking for PC; 15 Connected / Waiting for Codex; 16 Idle. "
         "40 Connect Codex?; 41 Setup; 42 No connection."),
        ("17-32 / Work and planned interactions", "17 Project active; 18 Working; 18U Unavailable; "
         "19 Response ready; 20A Paused; 20B Ended; 21-24 PR status (planned); 25 retired; "
         "26-31 questions/voice/replies (planned); 32 Connection lost."),
        ("33-38 / Device and development", "33 Low battery; 34 Charging; 35A Sleep; 35B Wake; "
         "36 Settings; 37 Rig view; 38 Expression channels. Two actions share a horizontal "
         "row; a third is centered below. One action uses an icon capsule. Gradients use semantic colors."),
    ]
    width, height = dimensions
    column = (width - 94) / 2
    for index, (title, copy) in enumerate(groups):
        section(pdf, title, copy, 34 + (index % 2) * (column + 26),
                155 + (index // 2) * 185, column, height)
    pdf.showPage()


AUTOMATIC_SECTIONS = [
    ("15 / Connected, waiting for Codex", "An authenticated PC reply proves presence. Until a genuine "
     "prompt arrives, show Waiting for Codex. There is no device picker and no manual selection command in Codex."),
    ("The user prompt chooses context", "Trusted UserPromptSubmit sends the exact chat and turn identities "
     "to CCP. The latest prompt-hook emission becomes the followed context for this PC's paired Cody devices. "
     "SessionStart alone does not select a chat."),
    ("Background chats stay in the background", "Tool activity, completion and interrupts from another chat "
     "cannot steal the followed context. A new prompt in that other chat switches it. A tab click alone "
     "has no documented hook signal and is not claimed as focus tracking."),
    ("32 / Recover the same context", "LAN reconnect restores the broker's current context. A broker restart "
     "waits for the next genuine prompt. The workspace label is display text, not identity. No transcripts, "
     "prompts or responses are retained by CCP."),
]

ACTIVITY_CARDS = [
    ("17 / Project active", "Ready when you are", "The plugin provides context. Workspace text is an ASCII "
     "label; the session digest distinguishes chats. Branch names are not yet acquired."),
    ("18 / Working", "Working in Codex", "A genuine prompt enters Working directly. Tool hooks show Editing "
     "or Running. A new prompt in another chat changes context automatically."),
    ("18U / Unavailable", "No recent activity", "Active observations expire after 120 seconds without a new "
     "hook. A live PC heartbeat does not prove that a chat is still working."),
]

RESPONSE_CARDS = [
    ("19 / Response ready", "Read it in Codex", "Stop means the response is ready to read in Codex. It is "
     "not evidence that the task, tests or deployment succeeded."),
    ("20A / Paused", "Continue in Codex", "PermissionRequest means awaiting approval. Interrupt means the "
     "turn stopped; completion from a background tool cannot restart it."),
    ("20B -> 16 / Ended", "Waiting for Codex", "SessionEnd is shown for five active seconds, then Cody rests. "
     "The next real prompt resumes following automatically, without selecting a chat."),
]


# kind: Requested atlas identity.
# Returns page numbers used only to locate existing authored material for replacement.
def main_revisions(kind):
    if kind == "Navigation":
        return {
            1: ("cover",),
            2: ("sections", "Boot and saved decisions", "Screens 01-09, 14-16, 40-42 / LAN availability is sufficient.", [
                ("01-03 / Startup checkpoints", "VibeWare precedes Cody's local checkpoints. A failed startup "
                 "stage retains its real error. Ready does not establish a PC connection."),
                ("No saved Wi-Fi", "04 -> 05 -> 06 / 06-bis -> 07 -> 09. Network credentials and Codex pairing "
                 "are separate decisions. Saved Wi-Fi awaiting an IP uses 42; no public-Internet probe is required."),
                ("LAN ready; no saved Codex pairing", "40 asks Connect Codex? Yes starts 12; No returns to 16 "
                 "without saving a refusal or starting CCP. Hold the character for 1.5 seconds to reopen 41."),
                ("LAN ready; saved Codex pairing", "14 searches for the saved PC. Authenticated READY reaches "
                 "15 / Connected. CCP then supplies chat context automatically when a genuine prompt is submitted."),
            ]),
            4: ("sections", "Personalization and pairing", "Screens 09-15, 36, 40 and 41 / preferences and PC identity have separate owners.", [
                ("09 -> 10 -> 36 -> 11", "After LAN setup, Make it yours opens preferences and Skin. Screen 11 "
                 "selects Cody's skin, not a color-only option. Save confirmed preferences through their owner."),
                ("40 -> 12 -> 13", "Connect Codex? Yes opens the six-character code. Keep 12 awake for three "
                 "minutes. Pair using CCP in Codex; show 13 only after the two-sided persisted-link confirmation."),
                ("13 -> 14 -> 15 -> activity", "Let's go searches for the paired PC. Connected waits for a real "
                 "prompt. CCP associates its chat automatically; there is no extra selection screen or tool call."),
                ("41 / Setup and Options", "Hold Cody for 1.5 seconds. Connect Codex and Network setup share "
                 "the horizontal row; Options sits below and opens 10. Done returns to the setup caller. "
                 "PWR/BOOT preserve saved pairing."),
            ]),
            5: ("sections", "Connect and follow automatically", "Screens 14-20 and 32 / the plugin owns chat association.", AUTOMATIC_SECTIONS),
            6: ("screens", "Everyday activity from Codex", "Screens 17, 18 and 18U / context and activity are authenticated metadata.",
                ACTIVITY_CARDS, "19 follows Stop; 20A follows pause/interrupt; 20B follows SessionEnd, then 16. "
                "Missing authenticated packets expire after eight seconds and use 32 once a chat has been followed."),
            12: ("sections", "Return paths and decisions", "Screen IDs are stable; historical page numbers are secondary.", [
                ("Setup / 06-12, 36, 41", "Back follows the real caller. Cancel waits for active cleanup before "
                 "leaving network setup. Options returns to 41. The left-chevron has its own circular touch target."),
                ("Companion / 15-20, 32", "CCP supplies context automatically. Setup re-entry preserves it. "
                 "Response ready is not success; reconnect restores the latest genuine context. No chat picker is planned."),
                ("Planned interactions / 21-31", "21-24 report PR status; 25 is retired. 27 Back preserves the "
                 "pending question; 29 Cancel and 30 Back return to 27. Show 31 only after reply acknowledgment. "
                 "These transports remain outside the implemented activity adapter."),
                ("Device / 35-38 and open work", "35A/35B preserve navigation through standby. 36 returns to its "
                 "caller; 37/38 remain development routes. Gesture/wake, action audio and genuine multi-chat "
                 "delivery need physical review; voice-entry and transcription details remain planned."),
            ]),
        }
    return {
        1: ("cover",),
        2: ("inventory",),
        7: ("sections", "Make it yours, then connect", "Screens 10-13, 36, 40 and 41 / the current personalization and pairing journey.", [
            ("10 / Make it yours", "Open preferences on 36, then 11 / Skin. The same route is available from "
             "41 / Options. Keep caller context so Done returns to setup when opened there."),
            ("11 / Skin", "Select Cody's skin with a live character preview. This replaces the old Blue/Mint "
             "color-only step. Retain the reusable character and confirmed settings ownership."),
            ("12 / Connect Codex", "Generate the secure six-character code with a visible waiting state, then "
             "show it in full. The screen keeps the device awake for three minutes. CCP pairs from Codex."),
            ("40-41 / Actions", "Connect Codex / Network setup use side-by-side gradient cards with icons. "
             "Options is centered below. A single action uses a wide icon capsule. Labels appear complete "
             "after the main-line teletype, with touch enabled only while visible."),
        ]),
        8: ("screens", "Connect and wait for Codex", "Screens 13-15 / connection confirmation does not require a chat choice.", [
            ("13 / You're all set", "Pairing saved", "Enter after both sides confirm the saved link. Let's go "
             "continues to the PC search. This is pairing confirmation, not a working-chat claim."),
            ("14 / Looking for PC", "Looking for PC", "The LAN is ready; Cody searches for its retained CCP "
             "instance. A saved identity alone does not establish presence."),
            ("15 / Connected", "Waiting for Codex", "The paired PC replied. Wait for CCP to send the chat "
             "of the next genuine prompt. No Pick a chat instruction, picker or manual association step."),
        ], "When you submit a prompt in Codex, CCP supplies that exact context and screen 18 shows work. "
           "The adapter does not treat a tab click as a documented focus event."),
        9: ("screens", "Cody follows submitted work", "Screens 17, 18 and 18U / the plugin selects context automatically.", ACTIVITY_CARDS,
            "16 / Idle keeps the breathing, gaze and blink channels. All devices paired to the PC mirror "
            "its latest prompted chat; tools and completions from other chats cannot steal the context."),
        10: ("screens", "Respond, pause and rest", "Screens 19, 20A, 20B and 16 / activity is separate from task success.", RESPONSE_CARDS,
             "32 / Connection lost restores the current broker context after LAN reconnect. A broker restart "
             "waits for the next prompt; it does not ask the user to select a chat again."),
        22: ("sections", "Decisions and review boundaries", "The main atlas is current; dated addenda and earlier errata remain at the end.", [
            ("Important decision / chat ownership", "CCP owns automatic chat association from UserPromptSubmit. "
             "Cody presents it. The manual Choose/Pick a chat step is removed. This corrects an initial "
             "analysis gap about responsibility and the user's flow."),
            ("Important decision / action language", "Two gradient icon cards share the horizontal row. "
             "A third action sits centered below. A single action uses a wide capsule. Keep semantic colors, "
             "the independent lateral Back and circular chord bounds."),
            ("Character and planned work", "Preserve cloud silhouette, named rig channels and composable "
             "expressions. Palette and spacing studies remain applicable. PR, answer and voice transports "
             "are still planned; do not infer their state from a response-ready hook."),
            ("Evidence", "The preceding 90-second boot capture reached Connected, accepted CCP packets and "
             "entered standby. That proves the preceding boot correction only. CCP 0.4 automatic activity "
             "and its physical presentation require fresh verification; host checks are separate evidence."),
        ]),
    }


# specification: Typed-by-name page recipe from this fixed authoring inventory.
# dimensions: Existing page dimensions.
# kind: Atlas identity.
# number: Output page number.
# Raises ValueError for unknown recipes, count mismatch or layout overflow.
def authored_page(specification, dimensions, kind, number):
    stream = BytesIO()
    pdf = canvas.Canvas(stream, pagesize=dimensions, pageCompression=1)
    recipe, *content = specification
    if recipe == "cover":
        cover_page(pdf, dimensions, kind, number)
    elif recipe == "inventory":
        inventory_page(pdf, dimensions, kind, number)
    elif recipe == "sections":
        sections_page(pdf, dimensions, kind, *content, number)
    elif recipe == "screens":
        screens_page(pdf, dimensions, kind, *content, number)
    else:
        raise ValueError("Unknown atlas page recipe: " + recipe)
    pdf.save()
    stream.seek(0)
    return PdfReader(stream).pages[0]


# kind: One of the two explicitly requested atlases.
# staging: Owned artifacts directory, never a root build directory.
# Raises OSError or ValueError for changed source topology or unsafe repeated edits.
def revise(kind, staging):
    source = ROOT / "docs/cody" / ("cody-" + kind.lower() + "-atlas-v1.pdf")
    reader = PdfReader(source)
    texts = [page.extract_text() or "" for page in reader.pages]
    if any(REVISION in text for text in texts):
        raise ValueError("This revision already exists: " + source.name)
    if len(reader.pages) != 36 or "Follow one Codex chat" not in texts[34]:
        raise ValueError("Atlas topology changed; review source pages before editing")
    dimensions = tuple(float(value) for value in (reader.pages[0].mediabox.width,
                                                 reader.pages[0].mediabox.height))
    backup = staging / (source.stem + "-original.pdf")
    if backup.exists():
        raise ValueError("Preserved original already exists: " + str(backup))
    backup.write_bytes(source.read_bytes())
    replacements = main_revisions(kind)
    # The obsolete activity addendum is corrected in place; earlier errata are
    # retained verbatim as history. The final page explains their supersession.
    replacements[35] = ("sections", "Addendum / automatic context", "CCP 0.4 / screens 15-20 and 32 / corrected 10 October 2026.", AUTOMATIC_SECTIONS)
    writer = PdfWriter()
    for number, page in enumerate(reader.pages, 1):
        writer.add_page(authored_page(replacements[number], dimensions, kind, number)
                        if number in replacements else page)
    writer.add_page(authored_page(("sections", "Addendum / current integration contract",
        "Screen IDs 15-20, 18U and 32 / genuine hooks are required.", [
            ("PC ownership", "One local broker owns the paired PC identity and authenticated activity. "
             "Multiple MCP clients share it. Trusted hook metadata supplies session/turn/tool identities; "
             "prompts, replies, transcripts and tool arguments are discarded at ingestion."),
            ("Device ownership", "Cody receives nonce-bound ACTIVITY alongside authenticated READY. The "
             "canonical snapshot owns validity, timestamps and real errors. UI and audio consume it; "
             "neither chooses a chat nor reads host data independently."),
            ("Limits and observation", "Eight seconds without packets expires device activity; 120 seconds "
             "without an active hook marks activity unavailable. Missing input never becomes success. "
             "Hook acceptance is not evidence of displayed pixels or audible playback."),
            ("Deployment and references", "CCP 0.4 removes manual select/clear tools. Install the updated "
             "plugin, review/trust its hooks and submit a real prompt. Source: learn.chatgpt.com/docs/hooks "
             "and developers.openai.com/plugins/build/plugins. Actual hook/display delivery remains to verify."),
        ]), dimensions, kind, 37))
    writer.add_page(authored_page(("sections", "Errata and corrections", "Revision date: " + DATE + " / " + REVISION, [
        ("Main pages updated", "Current flow is integrated from the cover onward, using screen IDs as the "
         "reference. Screens 10-15, 36, 40 and 41 now show Skin, real pairing and the agreed setup access. "
         "Screens 15-20, 18U and 32 describe automatic context and real activity."),
        ("Manual selection removed", "The old Choose/Pick a chat prerequisite was an initial analysis gap "
         "(buco di analisi iniziale). CCP associates the latest genuine prompt automatically. Cody only "
         "presents the supplied context. Background chat events do not change it."),
        ("History retained", "Earlier addenda, unrelated pages and errata remain at the end. The activity "
         "addendum itself is corrected. Historical errata mentioning explicit selection describe the "
         "previous decision and are superseded by this dated revision, not current instructions."),
        ("Evidence and limits", "CCP 0.4 and the Waiting for Codex caption are source changes. New host checks, "
         "plugin hook execution and physical behavior require their own evidence. The prior successful "
         "Connected/standby capture does not verify this revision. Screen artwork is illustrative."),
    ]), dimensions, kind, 38))
    writer.add_metadata({"/Title": "Cody / " + kind + " Atlas", "/Author": "Umberto Giacobbi",
                         "/Subject": REVISION + " - automatic plugin-owned chat context"})
    destination = staging / source.name
    with destination.open("wb") as output:
        writer.write(output)
    print(source.name + ": main pages consolidated; historical errata retained; 38 pages")
    return source, destination


# argv: Explicit authoring arguments passed through scripts/device.ps1.
# Raises OSError or ValueError if authoring cannot complete both requested documents.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staging", type=Path, default=ROOT / "artifacts/docs" / REVISION.lower())
    args = parser.parse_args(argv)
    staging = args.staging.resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF staging must remain within artifacts")
    staging.mkdir(parents=True, exist_ok=True)
    register_fonts(Path("C:/Windows/Fonts"))
    prepared = [revise(kind, staging) for kind in ("Navigation", "Planning")]
    # Publish only after every page of both documents has been authored.
    for source, destination in prepared:
        source.write_bytes(destination.read_bytes())


if __name__ == "__main__":
    main()
