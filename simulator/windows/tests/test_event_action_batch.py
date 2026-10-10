"""Batch transport/publication boundaries and actual production action raster parity."""
from __future__ import annotations

import ctypes as C
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

from PIL import Image

WINDOWS_DIR = Path(__file__).resolve().parents[1]
ROOT = WINDOWS_DIR.parents[1]
sys.path.insert(0, str(WINDOWS_DIR))
sys.path.insert(0, str(ROOT / "plugins/cody-codex-plugin/mcp"))

from ccp_interactions import Choice, EventKind, EventPayload, Tone  # noqa: E402
from cody_ccp_simulator import SimulatorSnapshot  # noqa: E402
from cody_native_event_presentation import (  # noqa: E402
    Action, EventAction, NativeEventPresentation, paint_native_actions,
)


class ControlledPaint:
    # self: Controlled transport boundary, separate from the production raster parity tests.
    # error: Exact native error returned after mutating only the temporary driver buffer.
    def __init__(self, error=0):
        self.error = error
        self.calls = []

    # self: Recording native callable configured through the same ctypes driver surface.
    # pixels: Temporary RGB565 frame whose mutation must never publish on failure.
    # capacity: Complete pixel count supplied once for the entire action set.
    # actions: Contiguous bounded ABI controls.
    # count: Exact number of controls in this single native call.
    def __call__(self, pixels, capacity, actions, count):
        self.calls.append((capacity, count, tuple(actions[index].label for index in range(count))))
        pixels[0] = 0xFFFF
        return self.error


# No parameters: returns a bounded explicit ABI fixture for transport-only assertions.
def transport_action():
    native = Action()
    native.identity = 1
    native.label = b"Open"
    native.enabled = True
    wire = C.string_at(C.byref(native), C.sizeof(native))
    return EventAction("open", "Open", Tone.INFO, (113, 352, 240, 72), True, wire)


class EventActionBatchBoundaryTests(unittest.TestCase):
    # self: Three controls must share one input conversion, one native call and one publication.
    # Raises AssertionError if the driver reintroduces per-control full-frame work.
    def test_three_actions_publish_one_native_batch(self):
        image = Image.new("RGB", (466, 466), (32, 64, 96))
        function = ControlledPaint()
        api = SimpleNamespace(cody_host_event_paint_actions=function)
        actions = (transport_action(),) * 3
        with patch.object(image, "getdata", wraps=image.getdata) as reads, \
                patch.object(image, "paste", wraps=image.paste) as publications:
            paint_native_actions(image, actions, api)
            self.assertEqual(reads.call_count, 1)
            self.assertEqual(publications.call_count, 1)
        self.assertEqual(function.calls, [(466 * 466, 3, (b"Open",) * 3)])
        self.assertEqual(image.getpixel((0, 0)), (255, 255, 255))

    # self: Native failure after temporary pixel mutation must preserve the displayed frame.
    # Raises AssertionError if partial raster output publishes or the original cause is lost.
    def test_native_failure_never_publishes_partial_frame(self):
        image = Image.new("RGB", (466, 466), (32, 64, 96))
        before = image.tobytes()
        function = ControlledPaint(0x104)
        api = SimpleNamespace(cody_host_event_paint_actions=function)
        with patch.object(image, "paste", wraps=image.paste) as publications:
            with self.assertRaisesRegex(RuntimeError, "paint_actions failed: 0x00000104"):
                paint_native_actions(image, (transport_action(),) * 3, api)
            self.assertEqual(publications.call_count, 0)
        self.assertEqual(len(function.calls), 1)
        self.assertEqual(image.tobytes(), before)

    # self: A malformed later action must reject the complete batch before any conversion or call.
    # Raises AssertionError if validation admits a partial control set.
    def test_malformed_later_action_rejects_whole_batch(self):
        image = Image.new("RGB", (466, 466))
        function = ControlledPaint()
        api = SimpleNamespace(cody_host_event_paint_actions=function)
        invalid = EventAction("open", "Open", Tone.INFO, (113, 352, 240, 72), True, b"bad")
        with patch.object(image, "getdata", wraps=image.getdata) as reads:
            with self.assertRaises(ValueError):
                paint_native_actions(image, (transport_action(), invalid), api)
            self.assertEqual(reads.call_count, 0)
        self.assertEqual(function.calls, [])


class EventActionBatchNativeTests(unittest.TestCase):
    # self: Two- and three-control fixtures use actual shared plans and production C++ glyph/raster code.
    # Raises original missing/stale-library error or AssertionError for AA/layout/count divergence.
    def test_real_plan_batch_matches_native_single_control_calls(self):
        native = NativeEventPresentation()
        function = native.api.cody_host_event_paint_actions
        function.argtypes = [C.POINTER(C.c_uint16), C.c_uint32, C.POINTER(Action), C.c_uint32]
        function.restype = C.c_int
        choices = (Choice("accept", "Accept", "Proceed after review.", Tone.POSITIVE),
                   Choice("reject", "No thanks", "Retain the current state.", Tone.NEGATIVE),
                   Choice("details", "More", "Review details on your PC.", Tone.INFO))
        for count in (2, 3):
            with self.subTest(count=count):
                payload = EventPayload(EventKind.QUESTION, "", "Review request", "Choose a response.",
                                       choices=choices[:count])
                snapshot = SimulatorSnapshot(request_id="a" * 32, event_state="pending",
                                             event_payload=payload, event_fresh=True)
                native.navigation.stage = 1
                _, _, plan_actions, _ = native.prepare(snapshot)
                actions = (Action * count)(*(Action.from_buffer_copy(action.native_action) for action in plan_actions))
                pixel_count = 466 * 466
                # A nonuniform RGB565 background exposes AA differences at rounded
                # edges and glyphs; comparisons never add intermediate RGB conversions.
                background = tuple((index * 37) & 0xFFFF for index in range(pixel_count))
                batch = (C.c_uint16 * pixel_count)(*background)
                separate = (C.c_uint16 * pixel_count)(*background)
                self.assertEqual(function(batch, pixel_count, actions, count), 0)
                for action in actions:
                    self.assertEqual(function(separate, pixel_count, C.byref(action), 1), 0)
                self.assertEqual(bytes(batch), bytes(separate))
                self.assertNotEqual(bytes(batch), bytes((C.c_uint16 * pixel_count)(*background)))

    # self: An invalid second caption is rejected before any native pixel mutation.
    # Raises original native-load error or AssertionError if preparation becomes partial publication.
    def test_native_rejects_invalid_later_control_before_raster(self):
        native = NativeEventPresentation()
        function = native.api.cody_host_event_paint_actions
        function.argtypes = [C.POINTER(C.c_uint16), C.c_uint32, C.POINTER(Action), C.c_uint32]
        function.restype = C.c_int
        payload = EventPayload(EventKind.QUESTION, "", "Review request", "Choose a response.", choices=(
            Choice("yes", "Yes", "Proceed after review.", Tone.POSITIVE),
            Choice("no", "No", "Retain the current state.", Tone.NEGATIVE)))
        snapshot = SimulatorSnapshot(request_id="b" * 32, event_state="pending", event_payload=payload,
                                     event_fresh=True)
        native.navigation.stage = 1
        _, _, plan_actions, _ = native.prepare(snapshot)
        actions = (Action * 2)(*(Action.from_buffer_copy(action.native_action) for action in plan_actions))
        actions[1].label = b""
        pixels = (C.c_uint16 * (466 * 466))(*([0x1234] * (466 * 466)))
        before = bytes(pixels)
        self.assertEqual(function(pixels, len(pixels), actions, 2), 0x104)
        self.assertEqual(bytes(pixels), before)
