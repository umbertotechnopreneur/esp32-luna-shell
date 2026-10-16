# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""SIMULATED interaction routes with the native layout, ASCII glyphs and explicit Send."""

from __future__ import annotations

import time

from cody_native_debug import render_debug_controls
from cody_native_behavior import CharacterActivity

from cody_native_event_presentation import NativeEventPresentation, EventAction, ascii_field, wrapped, paint_native_actions


# image: Logical RGB framebuffer prior to rotation.
# painter: Native glyph coverage owner.
# action: One approved gradient card/capsule and its complete untimed caption.
# Raises ValueError or RuntimeError for invalid plan data, unavailable rendering or native failure.
def paint_action(image, painter, action: EventAction) -> None:
    paint_actions(image, painter, (action,))


# image: Logical RGB framebuffer prior to rotation.
# painter: Native glyph coverage owner.
# actions: Complete approved control set; all captions remain outside narrative timing.
# Raises ValueError or RuntimeError for invalid plan data, unavailable rendering or native failure.
def paint_actions(image, painter, actions: tuple[EventAction, ...]) -> None:
    if painter.native is None:
        raise RuntimeError("CCP actions require the actual Cody C++ graphics backend")
    paint_native_actions(image, actions, painter.native.api)


class EventView:
    # self: Local reversible navigation; socket state remains owned by SimulatorEndpoint.
    def __init__(self) -> None:
        self.native = NativeEventPresentation()
        self.request_id = ""
        self.hidden_id = ""
        self._opening_request_id = ""
        self.stage = "notice"
        self.selected_id = ""
        self.page = 0
        self._route = None
        self._started = time.monotonic()
        self._actions: tuple[EventAction, ...] = ()
        self._back = False
        self._controls_visible = False
        self._rendered_event = None
        self._press: tuple[float, float] | None = None
        self._pressed_event = None
        self._tap_cancelled = False

    @property
    # self: Semantic stage encoded only for the existing Studio control interface.
    def stage(self):
        return ("notice", "choices", "confirm", "waiting", "failures", "questions")[self.native.navigation.stage]

    @stage.setter
    # self: Local navigation ABI owner.
    # value: Explicit semantic stage for authored preview setup, not a user transition.
    # Raises ValueError for unsupported stage identities.
    def stage(self, value):
        self.native.navigation.stage = ("notice", "choices", "confirm", "waiting", "failures", "questions").index(value)

    @property
    # self: Current local stable choice identity.
    def selected_id(self):
        return self.native.navigation.selected_id.decode("ascii")

    @selected_id.setter
    # self: Native navigation ABI owner.
    # value: Stable semantic choice identity, never a raw card index.
    # Raises ValueError for an unrepresentable ASCII field.
    def selected_id(self, value):
        self.native.navigation.selected_id = ascii_field(value, 13)

    @property
    # self: Current readonly failure page identity.
    def page(self):
        return self.native.navigation.failure_page

    @page.setter
    # self: Native navigation ABI owner.
    # value: Explicit bounded page identity for preview state.
    # Raises ValueError for unsupported page identities.
    def page(self, value):
        if type(value) is not int or not 0 <= value <= 1:
            raise ValueError("Native failure page must be zero or one")
        self.native.navigation.failure_page = value

    # self: View that preserves a question path during radio loss and background activity changes.
    # snapshot: Complete isolated simulator observation.
    # endpoint: Actual dismissal owner for the shared readonly result dwell.
    # Raises original endpoint or native projection errors; retained Retry remains visible.
    # Returns whether an undismissed authenticated event owns the display.
    def visible(self, snapshot, endpoint=None) -> bool:
        if snapshot.event_payload is None or not snapshot.request_id:
            return False
        if snapshot.request_id != self.request_id:
            self.native.reset()
            self.request_id = snapshot.request_id
            if self.request_id == self._opening_request_id:
                self.stage = "choices"
            self._opening_request_id = ""
            self._controls_visible = False
            self._rendered_event = None
        if self.native.dismiss_due(snapshot, round(time.monotonic() * 1000000), self._controls_visible):
            if endpoint is None:
                raise RuntimeError("Native readonly result dismissal requires its endpoint owner")
            try:
                endpoint.dismiss_event(self.request_id)
            except (ValueError, RuntimeError, OSError) as error:
                self.native.result(3, getattr(error, 'esp_err_t', -1))
                raise
            self.native.result(3, 0)
            self.hidden_id = self.request_id
        return self.hidden_id != self.request_id

    # self: Local reversible stage selector with no transport side effects.
    # snapshot: Authenticated event content and explicit delivery state.
    # Returns the scene, complete narrative lines, approved actions and valid Back state.
    def plan(self, snapshot):
        return self.native.prepare(snapshot)

    # self: Pointer admission owner, binding input to exactly the controls last drawn.
    # snapshot: Current immutable synthetic observation including freshness and original error.
    def rendered_identity(self, snapshot):
        return (snapshot.request_id, snapshot.event_state, snapshot.event_payload,
                snapshot.event_fresh, snapshot.event_error, snapshot.selection_request_id,
                self.stage, self.selected_id, self.page,
                self.native.navigation.choice_page, self.native.navigation.detail_page,
                self.native.context.ui_error, self.native.context.reply_admitted,
                self.native.context.dismiss_admitted)

    # self: Preview renderer preserving native geometry, typewriter and complete untimed buttons.
    # snapshot: One immutable simulator observation.
    # window: Existing rig renderer and native ScenePainter owner.
    # rotation: Cardinal orientation handled by the caller for image and touch together.
    # personality: Native shared connection/scene cue preparation before the C++ raster.
    # compose: Shared C++ response owner supplying exactly the current displayed face.
    # activity: Coherence-qualified Codex observation; only the explicit PC approval admits acting.
    # Raises ValueError for invalid scene/glyph/action geometry.
    def render(self, snapshot, window, rotation, personality=None, compose=None, activity=CharacterActivity()):
        identity, lines, actions, back = self.plan(snapshot)
        painter = window.renderer.scene_painter
        definition = painter.definition(identity)
        title = definition["title"]["text"]
        title_color = definition["title"]["color_rgb565"]
        if self.native.output.title:
            title = self.native.output.title.decode("ascii")
            title_color = self.native.output.title_color
        route = (snapshot.request_id, identity, self.stage, self.selected_id, self.page,
                 self.native.navigation.choice_page, self.native.navigation.detail_page)
        if route != self._route:
            self._route, self._started = route, time.monotonic()
        elapsed = (time.monotonic() - self._started) * 1000
        total = len(title) + sum(len(line.text) for line in
            self.native.output.lines[:self.native.output.line_count] if not line.instant)
        if painter.native is None:
            raise RuntimeError("CCP event timing requires the actual Cody C++ scene owner")
        timing = painter.native.scene_timing(identity, elapsed)
        visible = painter.content_characters(identity, elapsed, total, 0)
        controls_visible = bool(timing[5]) and self.native.controls_ready(visible, len(title))
        cloud = definition["cloud"]
        if compose is None:
            raise RuntimeError("CCP event rendering requires the shared native response owner")
        approval = identity == "approval-on-pc"
        scene, values = compose(painter.face(identity), painter.cloud_transform(identity, elapsed), rotation,
            character_enabled=approval, activity=activity,
            source="ccp" if approval else "ccp-hidden")
        if personality is not None:
            top = min([definition["title"]["top"]] + [top for _, top in lines if top < 394])
            bottom = max([definition["title"]["top"] + 30] + [top + 25 for _, top in lines if top < 394])
            # The shared native cue context admits six exclusions. One content
            # rectangle reserves all text without dropping any action or Back.
            exclusions = [(73, top, 393, bottom), (73, 22, 393, 47)]
            if any(top >= 394 for _, top in lines):
                exclusions.append((99, 394, 367, 419))
            exclusions += [(x, y, x + width, y + height) for action in actions for x, y, width, height in (action.bounds,)]
            if back:
                exclusions.append((10, 203, 70, 263))
            personality(snapshot, identity, scene, cloud["scale"], rotation, elapsed, exclusions)
        image = window.renderer.draw_scene(scene, debug=False, navigation_visible=False,
                                           background_rgb565=definition["background_rgb565"]).convert("RGB")
        painter.paint_text(image, title, definition["title"]["top"], title_color,
                           visible=painter.content_characters(identity, elapsed, len(title), 0))
        preceding = len(title)
        for line in self.native.output.lines[:self.native.output.line_count]:
            text = line.text.decode("ascii")
            characters = (len(text) if timing[5] else 0) if line.instant else painter.content_characters(
                identity, elapsed, len(text), preceding)
            if line.delivery_pulse and not controls_visible:
                characters = 0
            painter.paint_text(image, text, line.top, body=True, max_width=300, visible=characters,
                               color=self.native.line_color(line, round(elapsed * 1000)))
            if not line.instant:
                preceding += len(text)
        if controls_visible:
            paint_actions(image, painter, actions)
        if back and controls_visible:
            image = render_debug_controls(window.renderer.require_native(), image)
        # Keep the synthetic identity in the unobstructed top chord rather
        # than covering the answer paging hint or explicit Send action.
        painter.paint_text(image, "SIMULATED", 22, color=64704, body=True)
        key = ("ccp-event", route, min(visible, total), lines, actions, snapshot.event_fresh, rotation, round(elapsed / 33))
        self._controls_visible = controls_visible
        self._actions, self._back = actions, back
        self._rendered_event = self.rendered_identity(snapshot)
        return image, scene, key

    # self: Pointer owner retaining the exact fully drawn observation until release.
    # point: Logical coordinates using the same rotation as the rendered framebuffer.
    # snapshot: Latest authenticated request and freshness.
    # endpoint: Existing transport owner, never called on touch down.
    def press(self, point, snapshot, endpoint) -> None:
        self._press = None
        self._pressed_event = None
        self._tap_cancelled = False
        if point is not None and self._controls_visible and self._rendered_event == self.rendered_identity(snapshot):
            self._press = point
            self._pressed_event = self._rendered_event

    # self: Gesture owner preserving excursions even when the pointer returns to its origin.
    # point: Logical pressed sample or None when contact leaves the device canvas.
    def drag(self, point) -> None:
        if self._press is None:
            return
        if point is None:
            self._press = None
            self._pressed_event = None
        elif (point[0] - self._press[0]) ** 2 + (point[1] - self._press[1]) ** 2 >= 144:
            self._tap_cancelled = True

    # self: Gesture owner using the same production release policy as firmware.
    # point: Released logical point, or None outside the preview.
    # snapshot: Latest complete authenticated event; changes cancel an in-flight touch.
    # endpoint: Actual reply, dismissal and question-selection owner.
    # Raises ValueError, RuntimeError or OSError for the original native or endpoint rejection.
    def release(self, point, snapshot, endpoint) -> None:
        origin, self._press = self._press, None
        pressed_event, self._pressed_event = self._pressed_event, None
        if (origin is None or point is None or snapshot.event_payload is None or
                pressed_event != self.rendered_identity(snapshot) or self._rendered_event != pressed_event):
            return
        if self._tap_cancelled and (point[0] - origin[0]) ** 2 + (point[1] - origin[1]) ** 2 < 144:
            return
        effect, request_id = self.native.release(snapshot, origin, point, self._controls_visible)
        if effect == 0:
            return
        if effect in (2, 3):
            try:
                if effect == 2:
                    endpoint.reply(self.selected_id)
                else:
                    endpoint.dismiss_event(self.request_id)
            except (ValueError, RuntimeError, OSError) as error:
                # Preserve the original endpoint exception and make a reviewed Retry
                # available in the native model; never infer successful delivery.
                self.native.result(effect, getattr(error, 'esp_err_t', -1))
                self._controls_visible = False
                raise
            self.native.result(effect, 0)
            if effect == 3:
                self.hidden_id = self.request_id
        elif effect == 4:
            self.native.context.observation_error = 0
        elif effect == 5:
            try:
                endpoint.select_question(request_id)
            except (ValueError, RuntimeError, OSError) as error:
                self.native.result(effect, getattr(error, 'esp_err_t', -1))
                self._controls_visible = False
                raise
            self.native.result(effect, 0)
            self._opening_request_id = request_id
        self._controls_visible = False
