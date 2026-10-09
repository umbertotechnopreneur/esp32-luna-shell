"""SIMULATED interaction routes with the native layout, ASCII glyphs and explicit Send."""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import time

from PIL import Image, ImageDraw

from ccp_interactions import EventKind, Tone
from scripts.common.cody_rig_contract import evaluate


@dataclass(frozen=True)
class EventAction:
    identity: str
    label: str
    tone: Tone
    bounds: tuple[int, int, int, int]
    enabled: bool = True


# text: Validated printable ASCII content that must remain complete on the device.
# Returns lines fitting the shared 300-pixel native body slot without dropping content.
def wrapped(text: str) -> tuple[str, ...]:
    return tuple(text[offset:offset + 27] for offset in range(0, len(text), 27))


# width: Logical control width from the approved layout.
# height: Logical control height from the approved layout.
# tone: Semantic color shared by the card, border and icon.
# Returns a cached antialiased gradient surface; immutable geometry costs no repeated supersampling.
@lru_cache(maxsize=32)
def control_surface(width: int, height: int, tone: Tone) -> Image.Image:
    colors = {Tone.NEUTRAL: ((45, 48, 54), (16, 18, 23), (106, 111, 121)),
              Tone.POSITIVE: ((0, 72, 65), (0, 24, 25), (35, 222, 203)),
              Tone.NEGATIVE: ((83, 38, 29), (28, 15, 17), (247, 134, 90)),
              Tone.INFO: ((12, 50, 100), (5, 20, 43), (44, 151, 251))}
    top, bottom, border = colors[tone]
    scale = 4
    size = width * scale, height * scale
    mask = Image.new("L", size, 0)
    radius = min(28, height // 2) * scale
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    color = Image.new("RGB", size)
    draw = ImageDraw.Draw(color)
    for row in range(size[1]):
        weight = row / max(1, size[1] - 1)
        shade = tuple(round(first * (1 - weight) + last * weight) for first, last in zip(top, bottom))
        draw.line((0, row, size[0] - 1, row), fill=shade)
    draw.rounded_rectangle((scale, scale, size[0] - scale - 1, size[1] - scale - 1),
                           radius=max(0, radius - scale), outline=border, width=2 * scale)
    color.putalpha(mask)
    return color.resize((width, height), Image.Resampling.LANCZOS)


# image: Logical RGB framebuffer prior to rotation.
# painter: Native glyph coverage owner.
# action: One approved gradient card/capsule and its complete untimed caption.
# Raises ValueError if a control/caption leaves the declared slot or circle.
def paint_action(image, painter, action: EventAction) -> None:
    x, y, width, height = action.bounds
    tone = action.tone if action.enabled else Tone.NEUTRAL
    if any((px - 233) ** 2 + (py - 233) ** 2 > 233 ** 2
           for px in (x, x + width - 1) for py in (y, y + height - 1)):
        raise ValueError("SIMULATED event action leaves the circular chord")
    surface = control_surface(width, height, tone)
    image.paste(surface, (x, y), surface)
    card = height > 80
    icon_x = x + width // 2 if card else x + 31
    icon_y = y + 35 if card else y + height // 2 + 4
    draw = ImageDraw.Draw(image)
    color = (161, 244, 226) if tone == Tone.POSITIVE else (244, 244, 247)
    if action.tone == Tone.NEGATIVE:
        draw.line((icon_x - 7, icon_y - 7, icon_x + 7, icon_y + 7), fill=color, width=3)
        draw.line((icon_x + 7, icon_y - 7, icon_x - 7, icon_y + 7), fill=color, width=3)
    elif action.tone == Tone.POSITIVE:
        draw.line((icon_x - 9, icon_y, icon_x - 2, icon_y + 7, icon_x + 10, icon_y - 8),
                  fill=color, width=3, joint="curve")
    else:
        draw.line((icon_x - 9, icon_y, icon_x + 9, icon_y), fill=color, width=3)
        draw.line((icon_x + 2, icon_y - 7, icon_x + 9, icon_y, icon_x + 2, icon_y + 7),
                  fill=color, width=3, joint="curve")
    center = x + width // 2 if card else x + (width + 40) // 2
    top = y + 64 if card else y + (height - painter.font["body_font"]["height"]) // 2
    painter.paint_text(image, action.label, top, body=True, center_x=center,
                       max_width=width - (20 if card else 64))


class EventView:
    # self: Local reversible navigation; socket state remains owned by SimulatorEndpoint.
    def __init__(self) -> None:
        self.request_id = ""
        self.hidden_id = ""
        self.stage = "notice"
        self.selected_id = ""
        self.page = 0
        self._route = None
        self._started = time.monotonic()
        self._delivered_at: float | None = None
        self._actions: tuple[EventAction, ...] = ()
        self._back = False
        self._controls_visible = False
        self._press: tuple[float, float] | None = None

    # self: View that preserves a question path during radio loss and background activity changes.
    # snapshot: Complete isolated simulator observation.
    # Returns whether an undismissed authenticated event owns the display.
    def visible(self, snapshot) -> bool:
        if snapshot.event_payload is None or not snapshot.request_id:
            return False
        if snapshot.request_id != self.request_id:
            self.request_id, self.stage, self.selected_id, self.page = snapshot.request_id, "notice", "", 0
            self._delivered_at = None
        if snapshot.event_payload.kind == EventKind.CHECKS_PASSED:
            if self._delivered_at is not None and time.monotonic() - self._delivered_at >= 3.0:
                self.hidden_id = self.request_id
        return self.hidden_id != self.request_id

    # self: Local reversible stage selector with no transport side effects.
    # snapshot: Authenticated event content and explicit delivery state.
    # Returns the scene, complete narrative lines, approved actions and valid Back state.
    def plan(self, snapshot):
        event = snapshot.event_payload
        lines = []
        actions = []
        back = False
        footer = (113, 352, 240, 72)
        if snapshot.event_state in {"expired", "cancelled", "rejected"}:
            identity = "event-unavailable"
            status = {"expired": "Request expired", "cancelled": "Request cancelled", "rejected": "Request unavailable"}
            lines = [(status[snapshot.event_state], 196), ("Reply not confirmed", 226)]
            if snapshot.event_state == "expired" and event.selected_id:
                lines = [("Delivery unconfirmed", 196), ("Check on your PC", 226)]
            actions = [EventAction("dismiss", "Done", Tone.NEUTRAL, footer)]
        elif snapshot.event_state == "delivered":
            identity = "reply-sent"
            lines = [("Codex received", 196), ("your choice", 221)]
            actions = [EventAction("dismiss", "Done", Tone.POSITIVE, footer)]
        elif snapshot.event_state in {"sending", "received"}:
            identity = "decision-waiting"
            lines = [("Waiting for Codex", 196), ("to receive your reply", 221)]
            if not snapshot.event_fresh:
                lines.append(("Offline - reply kept", 270))
        elif event.kind == EventKind.QUESTION:
            if self.stage == "choices":
                identity, back = "decision-choices", True
                lines = [(event.title, 196)]
                bounds = ((73, 260, 154, 104), (239, 260, 154, 104), (113, 376, 240, 52))
                actions = [EventAction("choose:" + choice.choice_id, choice.label, choice.tone, bounds[index])
                           for index, choice in enumerate(event.choices)]
            elif self.stage == "confirm":
                identity, back = "decision-confirm", True
                choice = next(choice for choice in event.choices if choice.choice_id == self.selected_id)
                lines = [(choice.label, 196)] + [(line, 222 + index * 25) for index, line in enumerate(wrapped(choice.detail))]
                actions = [EventAction("send", "Send", choice.tone, footer, snapshot.event_fresh)]
            else:
                identity = "decision-notice"
                lines = [(event.title, 196)] + [(line, 222 + index * 25) for index, line in enumerate(wrapped(event.body))]
                actions = [EventAction("open", "Open", Tone.INFO, footer)]
        elif event.kind == EventKind.CHECKS_FAILED and self.stage == "failures":
            identity, back = "check-failures", True
            for row, name in enumerate(event.failures[self.page * 3:self.page * 3 + 3]):
                lines += [(line, 196 + row * 52 + index * 25) for index, line in enumerate(wrapped(name))]
            if len(event.failures) > 3:
                lines.append((f"Swipe up / down  {self.page + 1}/2", 354))
        else:
            identities = {EventKind.NOTICE: "codex-notice", EventKind.APPROVAL_ON_PC: "approval-on-pc",
                          EventKind.PR_PENDING: "pr-pending", EventKind.CHECKS_RUNNING: "checks-running",
                          EventKind.CHECKS_FAILED: "checks-failed", EventKind.CHECKS_PASSED: "checks-running"}
            identity = identities[event.kind]
            lines = [(event.title, 196)] + [(line, 222 + index * 25) for index, line in enumerate(wrapped(event.body))]
            if event.progress_valid:
                lines.append((f"{event.passed} of {event.total} passed", 310))
            if event.kind == EventKind.CHECKS_FAILED:
                actions = [EventAction("failures", "View failures", Tone.INFO, footer)]
            elif event.kind != EventKind.CHECKS_PASSED:
                label = "On your PC" if event.kind == EventKind.APPROVAL_ON_PC else "Done"
                actions = [EventAction("dismiss", label, Tone.NEUTRAL, footer)]
        if (not snapshot.event_fresh and snapshot.event_state == "pending"
                and self.stage != "failures"):
            lines.append(("Offline - request kept", 326))
        return identity, tuple(lines), tuple(actions), back

    # self: Preview renderer preserving native geometry, typewriter and complete untimed buttons.
    # snapshot: One immutable simulator observation.
    # window: Existing rig renderer and native ScenePainter owner.
    # rotation: Cardinal orientation handled by the caller for image and touch together.
    # Raises ValueError for invalid scene/glyph/action geometry.
    def render(self, snapshot, window, rotation):
        identity, lines, actions, back = self.plan(snapshot)
        painter = window.renderer.scene_painter
        definition = painter.definition(identity)
        title = definition["title"]["text"]
        if snapshot.event_payload.kind == EventKind.CHECKS_PASSED:
            title = "Checks passed"
        elif snapshot.event_state == "expired" and snapshot.event_payload.selected_id:
            title = "Reply unconfirmed"
        route = (snapshot.request_id, identity, self.stage, self.selected_id, self.page)
        if route != self._route:
            self._route, self._started = route, time.monotonic()
        elapsed = (time.monotonic() - self._started) * 1000
        total = len(title) + sum(len(line) for line, _ in lines)
        visible = max(0, int(elapsed / 45))
        self._controls_visible = visible >= total
        if (snapshot.event_payload.kind == EventKind.CHECKS_PASSED and self._controls_visible
                and self._delivered_at is None):
            self._delivered_at = time.monotonic()
        self._actions, self._back = actions, back
        cloud = definition["cloud"]
        scene = evaluate(window.renderer.rig, painter.face(identity), tuple(cloud[key] for key in ("x", "y", "scale")))
        image = window.renderer.draw_scene(scene, debug=False, navigation_visible=False,
                                           background_rgb565=definition["background_rgb565"]).convert("RGB")
        painter.paint_text(image, title, definition["title"]["top"], visible=min(len(title), visible))
        preceding = len(title)
        for text, top in lines:
            painter.paint_text(image, text, top, body=True, max_width=300,
                               visible=min(len(text), max(0, visible - preceding)))
            preceding += len(text)
        if self._controls_visible:
            for action in actions:
                paint_action(image, painter, action)
        if back:
            draw = ImageDraw.Draw(image)
            draw.ellipse((10, 203, 69, 262), fill=(22, 24, 29), outline=(63, 66, 73), width=2)
            draw.line((44, 222, 35, 233, 44, 244), fill=(239, 239, 243), width=4, joint="curve")
        # With three actions the lower capsule reaches y428; keep the synthetic
        # identity in the unobstructed top chord instead of covering that action.
        painter.paint_text(image, "SIMULATED", 22, color=64704, body=True)
        key = ("ccp-event", route, min(visible, total), lines, actions, snapshot.event_fresh, rotation)
        return image, scene, key

    # self: Pointer owner whose selection remains local until the explicit Send action.
    # point: Logical coordinates using the same rotation as the rendered framebuffer.
    # snapshot: Latest authenticated request and freshness.
    # endpoint: Socket owner called only after an enabled Send hit.
    # Raises ValueError, RuntimeError or OSError if the reviewed reply can no longer be sent.
    def press(self, point, snapshot, endpoint) -> None:
        self._press = point
        if point is None:
            return
        x, y = point
        if self._back and (x - 40) ** 2 + (y - 233) ** 2 <= 30 ** 2:
            self.stage = "choices" if self.stage == "confirm" else "notice"
            return
        if not self._controls_visible:
            return
        for action in self._actions:
            bx, by, width, height = action.bounds
            if not action.enabled or not (bx <= x < bx + width and by <= y < by + height):
                continue
            if action.identity.startswith("choose:"):
                self.selected_id, self.stage = action.identity.removeprefix("choose:"), "confirm"
            elif action.identity == "send":
                endpoint.reply(self.selected_id)
            elif action.identity == "open":
                self.stage = "choices"
            elif action.identity == "failures":
                self.stage, self.page = "failures", 0
            elif action.identity == "dismiss":
                self.hidden_id = self.request_id
            self._controls_visible = False
            return

    # self: Read-only failure-list navigation; no row is selectable.
    # point: Released logical coordinate, or None outside the preview.
    # snapshot: Current complete failure-name collection.
    def release(self, point, snapshot) -> None:
        origin, self._press = self._press, None
        if self.stage != "failures" or origin is None or point is None or snapshot.event_payload is None:
            return
        dx, dy = point[0] - origin[0], point[1] - origin[1]
        if abs(dy) >= 30 and abs(dy) > abs(dx):
            pages = (len(snapshot.event_payload.failures) + 2) // 3
            self.page = min(max(0, self.page + (1 if dy < 0 else -1)), pages - 1)
