"""Capture isolated synthetic question frames through Cody Studio's device view."""

from dataclasses import replace

from PIL import Image, ImageDraw

from ccp_interactions import Choice, EventKind, EventPayload, PendingQuestion, Tone
from cody_ccp_event_view import EventView
from cody_ccp_simulator import SimulatorSnapshot
from cody_device_ui import SIZE, label, rotate


# rotation: Cardinal display orientation applied to each complete logical frame.
# elapsed_ms: Active route time for the requested still.
# Raises the original native presentation or raster error.
def capture_question_flow(rotation, elapsed_ms):
    request_id, other_id = "1" * 32, "2" * 32
    question = EventPayload(
        EventKind.QUESTION, "Firmware settings", "Save device preferences?",
        "Where should preferences live when Cody reconnects to the PC?",
        (
            Choice("device", "Store on device", "Keep preferences on this Cody.", Tone.POSITIVE),
            Choice("sync", "Sync preferences to this PC and restore them automatically",
                   "Save preferences on this PC and sync them when Cody reconnects. "
                   "They will be restored on this device after a restart. "
                   "This applies to display and behavior settings only.", Tone.INFO),
            Choice("session", "Keep this session only", "Discard these changes after restart.", Tone.NEUTRAL),
        ),
        pending_question_count=2,
        pending_questions=(
            PendingQuestion(request_id, "Firmware settings", "Save device preferences?"),
            PendingQuestion(other_id, "Website theme", "Choose a site theme"),
        ),
    )
    pending = SimulatorSnapshot(request_id=request_id, event_state="pending",
                                event_payload=question, event_fresh=True)
    sent = replace(question, selected_id="sync")
    approval = replace(pending, request_id="3" * 32, event_payload=EventPayload(
        EventKind.APPROVAL_ON_PC, "", "Approval needed", "Review the preview changes"))
    frames = (
        ("01  Question notice", pending, "notice", 0, 0),
        ("02  Pending questions", pending, "questions", 0, 0),
        ("03  Choices 1 and 2", pending, "choices", 0, 0),
        ("04  Choice 3", pending, "choices", 1, 0),
        ("05  Review reply / page 1", pending, "confirm", 0, 0),
        ("06  Review reply / page 2", pending, "confirm", 0, 1),
        ("07  Awaiting delivery", replace(pending, event_state="received", event_payload=sent), "waiting", 0, 0),
        ("08  Recipient confirmed", replace(pending, event_state="delivered", event_payload=sent), "waiting", 0, 0),
        ("09  Approval on PC", approval, "notice", 0, 0),
    )
    gutter, heading = 24, 42
    stride = SIZE + gutter
    sheet = Image.new("RGB", (stride * 3 + gutter, (SIZE + heading) * 3 + gutter), (23, 28, 36))
    draw = ImageDraw.Draw(sheet)
    view = EventView()
    mask = Image.new("L", (SIZE, SIZE))
    ImageDraw.Draw(mask).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
    for index, (caption, snapshot, stage, choice_page, detail_page) in enumerate(frames):
        view.native.reset()
        view.stage = stage
        view.native.navigation.choice_page = choice_page
        view.native.navigation.detail_page = detail_page
        if stage == "confirm":
            view.selected_id = "sync"
        image = view.render_device(snapshot, 0, capture_elapsed_us=elapsed_ms * 1000)
        x = gutter + (index % 3) * stride
        y = gutter // 2 + (index // 3) * (SIZE + heading)
        label(draw, caption, y, 18, x=x + SIZE // 2, width=SIZE)
        sheet.paste(rotate(image, rotation), (x, y + heading - 8), mask)
    return sheet
