"""Explicit host-only Cody PoC checks and preview artifacts; never accesses firmware or hardware."""

from __future__ import annotations

from pathlib import Path
import sys

# Direct command execution resolves the shared repository package explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import argparse
import copy
import json
import math
import statistics
import time
import tempfile
import unittest
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'simulator/windows'))
from scripts.common.cody_rig_contract import (
    advance_contour, advance_face, animate, contour_defaults, evaluate, load_contract,
    make_contour_state, unique_object, validate_channels, validate_contour_definition,
    validate_face_definition,
)
from scripts.common.cody_rig_types import FaceChannel, FillAntialiasing, face_channel
from scripts.common.asset_provenance import text_source_hash
from cody_coverage import FULL_COVERAGE, coverage_mask
from cody_preview_layout import PreviewLayout
from cody_simulator import (
    CodyRenderer, CodyWindow, logical_touch, orient, physical_point, rgb565,
)
from PIL import Image
import tkinter as tk


class RigContractTests(unittest.TestCase):
    # cls receives the shared immutable source documents for host checks.
    @classmethod
    def setUpClass(cls):
        cls.rig,cls.palette=load_contract()
        cls.transform=tuple(cls.rig['default_transform'][key] for key in ('x','y','scale'))
        cls.ready=cls.rig['boot_channels']['ready']

    # self supplies the authored ready expression and stable channel-name mapping.
    # overrides contains only the facial channels changed by the current scenario.
    # Raises KeyError for a misspelled or unsupported test channel.
    def face_channels(self, **overrides):
        values = list(self.ready)
        for name, value in overrides.items():
            values[face_channel(name)] = value
        return values

    # self: Test context owning the compiled path and curve identities.
    # path_names: Explicit path IDs whose authored controls are required.
    # Raises KeyError for a path missing from the fixture.
    def path_points(self, path_names):
        paths = {path['name']: path for path in self.rig['paths']}
        points = set()
        for name in path_names:
            path = paths[name]
            first_curve = path['first_curve']
            for curve in self.rig['curves'][first_curve:first_curve + path['curve_count']]:
                points.update(curve['points'])
        return points

    # window owns the viewport and scheduled callback used by a manually requested test frame.
    # Raises tk.TclError if the preview has already been destroyed.
    @staticmethod
    def render_preview_frame(window):
        if window.after_id is not None:
            window.root.after_cancel(window.after_id)
            window.after_id = None
        # Windows queues WM resize/configure events outside Tk's idle geometry work.
        # Drain those events before asserting against the newly presented layout.
        window.root.update()
        window.root.update_idletasks()
        window.render_tick()

    # self: Generated tables must match the current authoring inputs, regardless of Git line endings.
    # Raises AssertionError or source-load errors for mismatched provenance.
    def test_generated_provenance(self):
        manifest=json.loads((ROOT/'assets/cody/rig/generated/manifest.json').read_text())
        self.assertEqual(manifest['source_sha256'], text_source_hash(ROOT/'assets/cody/cody.yaml'))
        self.assertEqual(manifest['palette_sha256'], text_source_hash(ROOT/'assets/cody/palette/palette256.json'))

    # self: LF and CRLF are equivalent, but meaningful changes must invalidate provenance.
    # Raises AssertionError or filesystem errors when the shared hash contract is incorrect.
    def test_text_provenance_normalizes_only_line_endings(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'artifacts/tmp') as directory:
            source = Path(directory)/'source.yaml'
            source.write_bytes(b'name: Cody\nscale: 1\n')
            original_hash = text_source_hash(source)
            source.write_bytes(b'name: Cody\r\nscale: 1\r\n')
            self.assertEqual(text_source_hash(source), original_hash)
            source.write_bytes(b'name: Cody\nscale: 2\n')
            self.assertNotEqual(text_source_hash(source), original_hash)

    # No parameters beyond the test instance: the local schema preserves every required stable anchor.
    def test_named_anchors_and_curve_ownership(self):
        names={point['name'] for point in self.rig['points']}
        self.assertTrue({'center','lobe.crown','eye.left','eye.right','brow.left','brow.right','cheek.left','cheek.right','mouth'}<=names)
        scene=evaluate(self.rig,self.ready,self.transform)
        for path in scene['paths']:
            definition=path['definition']
            first=self.rig['curves'][definition['first_curve']]['points'][0]
            last=self.rig['curves'][definition['first_curve']+definition['curve_count']-1]['points'][3]
            self.assertEqual(path['vertices'][0],scene['world'][first])
            self.assertEqual(path['vertices'][-1],scene['world'][last])

    # No parameters beyond the test instance: transforms apply identically to every feature and handle.
    def test_translation_and_scale_preserve_geometry(self):
        first=evaluate(self.rig,self.ready,(100,100,60))
        second=evaluate(self.rig,self.ready,(200,210,120))
        for name,(x,y) in first['world'].items():
            self.assertAlmostEqual(second['world'][name][0],(x-100)*2+200)
            self.assertAlmostEqual(second['world'][name][1],(y-100)*2+210)
            self.assertEqual(first['local'][name],second['local'][name])

    # No parameters beyond the test instance: moving a real anchor must carry all descendant curve controls.
    def test_anchor_moves_its_subtree(self):
        changed=copy.deepcopy(self.rig)
        for point in changed['points']:
            if point['name']=='eye.left': point['position'][0]+=.1
        before=evaluate(self.rig,self.ready,self.transform)
        after=evaluate(changed,self.ready,self.transform)
        for point in self.rig['points']:
            name=point['name']
            expected=.1 if name=='eye.left' or name.startswith('eye.left.') else 0
            self.assertAlmostEqual(after['local'][name][0]-before['local'][name][0],expected)
            self.assertAlmostEqual(after['local'][name][1],before['local'][name][1])

    # self receives the test context: closing one eye cannot override another facial channel.
    def test_independent_eye_and_mouth_channels(self):
        base=evaluate(self.rig,self.ready,self.transform)
        changed=list(self.ready); changed[FaceChannel.EYE_LEFT]=0
        altered=evaluate(self.rig,changed,self.transform)
        for point in self.rig['points']:
            if point['channel']!='eye_left': self.assertEqual(base['local'][point['name']],altered['local'][point['name']])
        self.assertNotEqual(base['local']['eye.left.p3'],altered['local']['eye.left.p3'])
        sleeping_base = self.face_channels(eye_left=0, eye_right=0, brow_left=.5,
                                           brow_right=-.4, mouth_open=.1, breath=0)
        sleeping=animate(sleeping_base,1000)
        self.assertEqual([sleeping[channel] for channel in (FaceChannel.EYE_LEFT, FaceChannel.EYE_RIGHT,
                                                                    FaceChannel.BROW_LEFT, FaceChannel.BROW_RIGHT)],[0,0,.5,-.4])
        self.assertGreater(sleeping[FaceChannel.MOUTH_OPEN],.1)
        self.assertEqual(sleeping[6:], sleeping_base[6:])

    # self receives the test context: shape controls resize only their own descendants around unchanged anchors.
    def test_face_shape_controls_preserve_anchors_and_other_features(self):
        original = evaluate(self.rig, self.ready, self.transform)
        cases = (
            ('mouth_width', 1.4, ('mouth',), False),
            ('nose_size', 1.35, ('nose',), True),
            ('cheek_size', .7, ('cheek.left', 'cheek.right'), True),
        )
        for control, value, anchors, scale_vertical in cases:
            with self.subTest(control=control):
                channels = self.face_channels(**{control: value})
                scene = evaluate(self.rig, channels, self.transform)
                ratio = value / self.ready[face_channel(control)]
                changed = False
                for name, point in original['world'].items():
                    owner = next((anchor for anchor in anchors if name.startswith(anchor + '.')), None)
                    if owner is None:
                        self.assertEqual(scene['world'][name], point, name)
                        continue
                    anchor = original['world'][owner]
                    after = scene['world'][name]
                    self.assertAlmostEqual(after[0] - anchor[0], (point[0] - anchor[0]) * ratio)
                    self.assertAlmostEqual(after[1] - anchor[1],
                                           (point[1] - anchor[1]) * (ratio if scale_vertical else 1))
                    changed |= after != point
                self.assertTrue(changed)

    # self receives the test context: facial targets must blend continuously and preserve elapsed-time equivalence.
    def test_face_transition_preserves_timing_and_rejects_invalid_targets(self):
        initial = self.face_channels(mouth_width=.6, nose_size=1.5, cheek_size=.5, cheek_opacity=.1)
        target = self.face_channels(mouth_width=1.5, nose_size=.65, cheek_size=1.65, cheek_opacity=.9)
        original_state, original_target = list(initial), list(target)
        frozen = advance_face(self.rig, initial, target, 0)
        self.assertEqual(frozen, initial)
        self.assertIsNot(frozen, initial)
        direct = advance_face(self.rig, initial, target, 500)
        subdivided = initial
        for delta_ms in (0, 19, 131, 350):
            subdivided = advance_face(self.rig, subdivided, target, delta_ms)
        for channel, before, after, requested, split in zip(self.rig['channels'], initial, direct, target, subdivided):
            self.assertAlmostEqual(after, split, places=10, msg=channel['name'])
            if before != requested:
                self.assertGreater(after, min(before, requested))
                self.assertLess(after, max(before, requested))
            else:
                self.assertEqual(after, before)
        validate_channels(self.rig, direct)
        self.assertEqual(initial, original_state)
        self.assertEqual(target, original_target)
        self.assertEqual(advance_face(self.rig, self.ready, self.ready, 17), self.ready)
        for bad_target in (self.face_channels(mouth_width=math.nan),
                           self.face_channels(cheek_opacity=1.01)):
            with self.assertRaises(ValueError):
                advance_face(self.rig, initial, bad_target, 0)
        with self.assertRaises(ValueError):
            advance_face(self.rig, initial, target, -1)

    # self receives the test context: cheek opacity must composite over the body without moving facial geometry.
    def test_cheek_opacity_composites_and_quantizes_partial_color(self):
        renderer = CodyRenderer()
        original = evaluate(self.rig, self.ready, self.transform)
        sample = tuple(round(value) for value in original['world']['cheek.left'])
        pixels = {}
        for opacity in (0, .5, 1):
            scene = evaluate(self.rig, self.face_channels(cheek_opacity=opacity), self.transform)
            self.assertEqual(scene['world'], original['world'])
            for path in scene['paths']:
                expected = opacity if path['definition']['name'].startswith('cheek.') else 1
                self.assertEqual(path['opacity'], expected)
            image = renderer.draw_scene(scene, debug=False)
            # The body's edge coverage requires derived native colors at every cheek opacity.
            self.assertEqual(image.mode, 'RGB')
            pixels[opacity] = image.convert('RGB').getpixel(sample)
        self.assertNotEqual(pixels[0], pixels[1])
        self.assertNotEqual(pixels[.5], pixels[0])
        self.assertNotEqual(pixels[.5], pixels[1])
        for axis, shift, maximum, tolerance in ((0, 3, 31, 9), (1, 2, 63, 5), (2, 3, 31, 9)):
            channel = pixels[.5][axis]
            self.assertLessEqual(abs(channel - (pixels[0][axis] + pixels[1][axis]) / 2), tolerance)
            self.assertEqual(channel, (channel >> shift) * 255 // maximum)

    # No parameters beyond the test instance: speed depends on elapsed time, never on frame count.
    def test_animation_determinism_smoothness_and_bounds(self):
        direct=animate(self.ready,4801)
        for t in (0,17,55,200,999,4500): animate(self.ready,t)
        self.assertEqual(direct,animate(self.ready,4801))
        previous=animate(self.ready,0)
        for t in range(1,9601):
            current=animate(self.ready,t)
            validate_channels(self.rig,current)
            self.assertLess(max(abs(a-b) for a,b in zip(current,previous)),.012)
            previous=current

    # self receives the test context: zero deformation must recover the authored silhouette exactly.
    def test_contour_zero_deformation_preserves_authored_pose(self):
        original = evaluate(self.rig, self.ready, self.transform)
        state = make_contour_state(self.rig)
        state.update(fullness=0, amplitude=0)
        for phase in (0, .7, math.pi, math.tau - .001):
            for spread in (0, .5, 1):
                with self.subTest(phase=phase, phase_spread=spread):
                    state.update(phase=phase, phase_spread=spread)
                    scene = evaluate(self.rig, self.ready, self.transform, contour=state)
                    for field in ('local', 'world', 'paths', 'scale', 'rest_outline'):
                        self.assertEqual(scene[field], original[field])

    # self receives the test context: contour deformation must never move any facial control or path.
    def test_contour_keeps_face_and_center_independent(self):
        state = make_contour_state(self.rig)
        state.update(fullness=.6, amplitude=.8, phase=1.2, phase_spread=.7)
        contour_paths = {self.rig['contour']['path'], *self.rig['contour']['surface_paths']}
        contour_points = self.path_points(contour_paths)
        for channels in (self.ready,
                         self.face_channels(eye_left=0, eye_right=1, brow_left=-.5, brow_right=.4, mouth_open=.8, breath=-.5),
                         self.face_channels(eye_left=1, eye_right=0, brow_left=.7, brow_right=-.6, mouth_open=0, breath=1)):
            with self.subTest(channels=channels):
                original = evaluate(self.rig, channels, self.transform)
                scene = evaluate(self.rig, channels, self.transform, contour=state)
                for name in original['world']:
                    if name not in contour_points:
                        self.assertEqual(scene['local'][name], original['local'][name], name)
                        self.assertEqual(scene['world'][name], original['world'][name], name)
                for before, after in zip(original['paths'], scene['paths']):
                    if before['definition']['name'] not in contour_paths:
                        self.assertEqual(after, before)
                self.assertEqual(scene['scale'], original['scale'])
                self.assertTrue(any(scene['world'][name] != original['world'][name]
                                    for name in original['world'] if name.startswith('lobe.')))

    # self: Test context whose surface controls must inherit their named lobe exactly once.
    def test_contour_surface_controls_follow_lobes_without_facial_transforms(self):
        original = evaluate(self.rig, self.ready, self.transform)
        state = make_contour_state(self.rig)
        state.update(fullness=.8, amplitude=1, phase=2.1)
        deformed = evaluate(self.rig, self.ready, self.transform, contour=state)
        points = {point['name']: point for point in self.rig['points']}
        lobes = {lobe['point'] for lobe in self.rig['contour']['lobes']}
        surface_points = self.path_points(self.rig['contour']['surface_paths'])
        self.assertTrue(surface_points)
        for name in surface_points:
            ancestor = points[name]['parent']
            while ancestor is not None and ancestor not in lobes:
                ancestor = points[ancestor]['parent']
            self.assertIn(ancestor, lobes, name)
            self.assertEqual(self.rig['point_face_transforms'][name], (None, None))
            for axis in (0, 1):
                actual = deformed['local'][name][axis] - original['local'][name][axis]
                expected = deformed['local'][ancestor][axis] - original['local'][ancestor][axis]
                self.assertAlmostEqual(actual, expected, msg=name)
        for path in deformed['paths']:
            if path['definition']['name'] in self.rig['contour']['surface_paths']:
                self.assertEqual(path['opacity'], 1)
                self.assertTrue(path['visible'])

    # self: Test context proving that surface declarations cannot admit facial animation or ambiguous ownership.
    def test_contour_surface_contract_rejects_unsafe_declarations(self):
        for declared in ('body', ['body'], ['missing.surface'], ['body.shadow.lower', 'body.shadow.lower'], ['cheek.left']):
            with self.subTest(surface_paths=declared):
                changed = copy.deepcopy(self.rig)
                changed['contour']['surface_paths'] = declared
                with self.assertRaises(ValueError):
                    validate_contour_definition(changed)

        surface_name = 'body.shadow.lower'
        surface_point = 'body.shadow.lower.left_tip'
        for field, value in (('stroke', .02), ('visible', 'eye_left'), ('opacity_channel', 'cheek_opacity')):
            with self.subTest(surface_field=field):
                changed = copy.deepcopy(self.rig)
                path = next(path for path in changed['paths'] if path['name'] == surface_name)
                path[field] = value
                with self.assertRaises(ValueError):
                    validate_contour_definition(changed)

        changed = copy.deepcopy(self.rig)
        point = next(point for point in changed['points'] if point['name'] == surface_point)
        point['channel'] = 'mouth_open'
        with self.assertRaises(ValueError):
            validate_contour_definition(changed)

        changed = copy.deepcopy(self.rig)
        point = next(point for point in changed['points'] if point['name'] == surface_point)
        point['parent'] = 'cheek.left'
        validate_face_definition(changed)
        with self.assertRaises(ValueError):
            validate_contour_definition(changed)

        changed = copy.deepcopy(self.rig)
        point = next(point for point in changed['points'] if point['name'] == 'eye.left')
        point['parent'] = 'lobe.crown'
        with self.assertRaises(ValueError):
            validate_contour_definition(changed)

        changed = copy.deepcopy(self.rig)
        path = next(path for path in changed['paths'] if path['name'] == surface_name)
        curve = changed['curves'][path['first_curve']]
        curve['points'][1] = 'lobe.crown.out'
        with self.assertRaises(ValueError):
            validate_contour_definition(changed)

    # self receives the test context: fullness changes the static body even when waves are disabled.
    def test_contour_fullness_changes_body_without_time_or_face_motion(self):
        outlines = []
        for fullness in (-1, 0, 1):
            state = make_contour_state(self.rig)
            state.update(fullness=fullness, amplitude=0, speed=0)
            scene = evaluate(self.rig, self.ready, self.transform, contour=state)
            outlines.append(next(path['vertices'] for path in scene['paths']
                                 if path['definition']['name'] == 'body'))
        self.assertNotEqual(outlines[0], outlines[1])
        self.assertNotEqual(outlines[1], outlines[2])
        self.assertNotEqual(outlines[0], outlines[2])

    # self receives the test context: equivalent elapsed time must give the same phase and controls.
    def test_contour_frame_subdivision_preserves_motion(self):
        initial = make_contour_state(self.rig)
        initial.update(fullness=-.4, amplitude=.15, speed=.05, phase_spread=.2, phase=6)
        target = contour_defaults(self.rig)
        target.update(fullness=.8, amplitude=.9, speed=1.4, phase_spread=.85)
        original_state, original_target = dict(initial), dict(target)
        direct = advance_contour(self.rig, initial, target, 2000)
        subdivided = initial
        for delta_ms in (0, 17, 83, 401, 1499):
            subdivided = advance_contour(self.rig, subdivided, target, delta_ms)
        for key in direct:
            self.assertAlmostEqual(direct[key], subdivided[key], places=10, msg=key)
        self.assertEqual(initial, original_state)
        self.assertEqual(target, original_target)
        self.assertIsNot(direct, initial)
        self.assertGreaterEqual(direct['phase'], 0)
        self.assertLess(direct['phase'], math.tau)
        direct_scene = evaluate(self.rig, self.ready, self.transform, contour=direct)
        split_scene = evaluate(self.rig, self.ready, self.transform, contour=subdivided)
        for name, point in direct_scene['world'].items():
            for axis in (0, 1):
                self.assertAlmostEqual(point[axis], split_scene['world'][name][axis], places=8)

    # self receives the test context: a new speed target must preserve phase and change motion smoothly.
    def test_contour_speed_change_preserves_phase_continuity(self):
        initial = make_contour_state(self.rig)
        initial.update(speed=.2, phase=.83)
        target = {name: initial[name] for name in contour_defaults(self.rig)}
        target['speed'] = 1.6
        frozen = advance_contour(self.rig, initial, target, 0)
        self.assertEqual(frozen, initial)
        self.assertIsNot(frozen, initial)
        advanced = advance_contour(self.rig, initial, target, 1)
        self.assertGreater(advanced['speed'], initial['speed'])
        self.assertLess(advanced['speed'], target['speed'])
        phase_step = (advanced['phase'] - initial['phase']) % math.tau
        self.assertGreaterEqual(phase_step, math.tau * initial['speed'] / 1000)
        self.assertLessEqual(phase_step, math.tau * advanced['speed'] / 1000)
        before = evaluate(self.rig, self.ready, self.transform, contour=initial)
        after = evaluate(self.rig, self.ready, self.transform, contour=advanced)
        displacement = max(math.dist(before['world'][name], point)
                           for name, point in after['world'].items())
        self.assertGreater(displacement, 0)
        self.assertLess(displacement, .1)

    # self receives the test context: settled zero speed must hold the current silhouette indefinitely.
    def test_contour_stationary_speed_and_zero_time_freeze(self):
        initial = make_contour_state(self.rig)
        initial.update(fullness=.3, amplitude=.7, speed=0, phase_spread=.8, phase=1.1)
        target = {name: initial[name] for name in contour_defaults(self.rig)}
        original_scene = evaluate(self.rig, self.ready, self.transform, contour=initial)
        for delta_ms in (0, 16, 20000):
            with self.subTest(delta_ms=delta_ms):
                advanced = advance_contour(self.rig, initial, target, delta_ms)
                self.assertEqual(advanced, initial)
                self.assertIsNot(advanced, initial)
                self.assertEqual(evaluate(self.rig, self.ready, self.transform, contour=advanced),
                                 original_scene)

    # self receives the test context: the reference outline stays authored while debug points follow deformation.
    def test_contour_reference_outline_and_evaluated_geometry_agree(self):
        channels = list(self.ready)
        channels[FaceChannel.BREATH] = .7
        transform = (210, 240, 100)
        original = evaluate(self.rig, channels, transform)
        original_body = next(path['vertices'] for path in original['paths']
                             if path['definition']['name'] == 'body')
        self.assertEqual(original['rest_outline'], original_body)
        original_outline = list(original['rest_outline'])
        for phase in (0, 1.3, math.pi):
            state = make_contour_state(self.rig)
            state.update(fullness=.4, amplitude=.8, phase=phase)
            scene = evaluate(self.rig, channels, transform, contour=state)
            body = next(path for path in scene['paths'] if path['definition']['name'] == 'body')
            self.assertEqual(scene['rest_outline'], original_outline)
            self.assertNotEqual(body['vertices'], original_outline)
            self.assertEqual(body['vertices'][0], body['vertices'][-1])
            definition = body['definition']
            curves = self.rig['curves'][definition['first_curve']:
                                        definition['first_curve'] + definition['curve_count']]
            self.assertEqual(len(body['vertices']), len(curves) * self.rig['subdivisions'] + 1)
            for index, curve in enumerate(curves):
                start = index * self.rig['subdivisions']
                end = (index + 1) * self.rig['subdivisions']
                self.assertEqual(body['vertices'][start], scene['world'][curve['points'][0]])
                self.assertEqual(body['vertices'][end], scene['world'][curve['points'][3]])
            for name, point in scene['local'].items():
                self.assertAlmostEqual(scene['world'][name][0], transform[0] + point[0] * scene['scale'])
                self.assertAlmostEqual(scene['world'][name][1], transform[1] + point[1] * scene['scale'])
        self.assertEqual(original['rest_outline'], original_outline)

    # self receives the test context: bad controls, phase or elapsed time must fail without mutation or repair.
    def test_contour_invalid_inputs_are_rejected(self):
        initial = make_contour_state(self.rig)
        defaults = contour_defaults(self.rig)
        for name in defaults:
            for value in (-1e9, 1e9, math.nan, math.inf, -math.inf, True, '0', None):
                with self.subTest(control=name, value=value):
                    target = dict(defaults)
                    target[name] = value
                    with self.assertRaises(ValueError):
                        advance_contour(self.rig, initial, target, 0)
                    state = dict(initial)
                    state[name] = value
                    with self.assertRaises(ValueError):
                        evaluate(self.rig, self.ready, self.transform, contour=state)
                    with self.assertRaises(ValueError):
                        advance_contour(self.rig, state, defaults, 0)
        for phase in (-.01, math.tau, math.nan, math.inf, True):
            with self.subTest(phase=phase):
                state = dict(initial)
                state['phase'] = phase
                with self.assertRaises(ValueError):
                    evaluate(self.rig, self.ready, self.transform, contour=state)
                with self.assertRaises(ValueError):
                    advance_contour(self.rig, state, defaults, 0)
        for delta_ms in (-1, math.nan, math.inf, True, '0', None):
            with self.subTest(delta_ms=delta_ms):
                with self.assertRaises(ValueError):
                    advance_contour(self.rig, initial, defaults, delta_ms)
        for malformed in ({name: value for name, value in defaults.items() if name != 'amplitude'},
                          dict(defaults, unknown_control=0)):
            with self.assertRaises(ValueError):
                advance_contour(self.rig, initial, malformed, 16)
        self.assertEqual(initial, make_contour_state(self.rig))
        self.assertEqual(defaults, contour_defaults(self.rig))

    # No parameters beyond the test instance: rotation round trips both normal points and the Back center.
    def test_rotation_and_back_target(self):
        for angle in (0,90,180,270):
            marker=Image.new('L',(466,466))
            for x,y in ((40,233),(73,73),(392,392),(100,300)):
                marker.putpixel((x,y),255)
            physical=orient(marker,angle)
            for y in range(466):
                for x in range(466):
                    if physical.getpixel((x,y)):
                        lx,ly=logical_touch(x,y,angle)
                        self.assertEqual(marker.getpixel((lx,ly)),255)

    # self receives the test context: fitting and rotating a preview must preserve its aspect and point precision.
    def test_resized_preview_preserves_aspect_and_fractional_point_mapping(self):
        state = make_contour_state(self.rig)
        state.update(fullness=.4, amplitude=.8, phase=1.23)
        scene = evaluate(self.rig, self.ready, (233.4, 235.8, 145.3), contour=state)
        self.assertTrue(any(value != round(value) for point in scene['world'].values() for value in point))
        for width, height in ((701, 397), (1301, 733), (373, 811)):
            for show_labels in (False, True):
                layout = PreviewLayout.fit(width, height, show_labels)
                self.assertGreater(layout.image_size, 0)
                self.assertGreaterEqual(layout.left, 0)
                self.assertGreaterEqual(layout.top, 0)
                self.assertLessEqual(layout.left + layout.image_size, width)
                self.assertLessEqual(layout.top + layout.image_size, height)
                self.assertIsNone(layout.to_logical(layout.left - 1, layout.top + layout.image_size / 2, 0))
                self.assertIsNone(layout.to_logical(layout.left + layout.image_size / 2,
                                                    layout.top + layout.image_size + 1, 0))
                for angle in (0, 90, 180, 270):
                    for name, logical in scene['world'].items():
                        with self.subTest(viewport=(width, height), labels=show_labels,
                                          rotation=angle, point=name):
                            canvas = layout.to_canvas(*logical, angle)
                            restored = layout.to_logical(*canvas, angle)
                            self.assertIsNotNone(restored)
                            self.assertAlmostEqual(restored[0], logical[0], places=10)
                            self.assertAlmostEqual(restored[1], logical[1], places=10)
                    origin = layout.to_canvas(100.25, 100.25, angle)
                    horizontal = layout.to_canvas(300.75, 100.25, angle)
                    vertical = layout.to_canvas(100.25, 300.75, angle)
                    self.assertAlmostEqual(math.dist(origin, horizontal), math.dist(origin, vertical), places=10)
        with self.assertRaises(ValueError):
            physical_point(233.4, 235.8, 45)

    # No parameters beyond the test instance: invalid values are rejected instead of silently repaired.
    def test_invalid_input_rejected(self):
        for bad in (-.01,1.01,math.nan,math.inf,True):
            values=list(self.ready); values[FaceChannel.EYE_LEFT]=bad
            with self.assertRaises(ValueError): evaluate(self.rig,values,self.transform)
        with self.assertRaises(ValueError): evaluate(self.rig,self.ready,(0,0,0))
        with self.assertRaises(ValueError): unique_object([('name','Cody'),('name','Other')])
        with self.assertRaises(ValueError): animate(self.ready,-1)

    # self: Test context checking immutable palette sources and quantized derived boundary colors.
    def test_palette_and_debug_colors(self):
        renderer = CodyRenderer()
        original_palette = list(renderer.native_palette)
        scene = evaluate(self.rig, self.ready, self.transform)
        image = renderer.draw_scene(scene, True)
        self.assertEqual(image.mode, 'RGB')
        self.assertEqual(len(renderer.native_palette), 256)
        self.assertEqual(renderer.native_palette, original_palette)
        used = set(image.getdata())
        debug_slots = set(self.rig['style']['debug'].values()) - {self.rig['style']['debug']['selected']}
        self.assertTrue({renderer.rgb_palette[slot] for slot in debug_slots} <= used)
        for color in used:
            red, green, blue = color
            native = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            self.assertEqual(color, rgb565(native))

    # self: Test context checking analytic rectangle coverage, Q8 boundaries and thin horizontal caps.
    def test_coverage_tracks_fractional_edges_and_caps(self):
        rectangle = [(0.25, 0.25), (2.75, 0.25), (2.75, 2.75), (0.25, 2.75)]
        mask = coverage_mask(rectangle, (4, 4))
        self.assertEqual(mask.mode, 'I')
        self.assertEqual(mask.getpixel((0, 0)), 576)
        self.assertEqual(mask.getpixel((1, 0)), 768)
        self.assertEqual(mask.getpixel((1, 1)), FULL_COVERAGE)
        self.assertEqual(mask.getpixel((3, 1)), 0)
        self.assertEqual(sum(mask.getdata()), 6400)
        cap = coverage_mask([(0, .1), (2, .1), (2, .4), (0, .4)], (3, 2))
        self.assertEqual(cap.getpixel((0, 0)), FULL_COVERAGE // 2)
        quantized = coverage_mask([(1 / 512, 0), (1, 0), (1, 1), (1 / 512, 1)], (2, 2))
        self.assertEqual(quantized.getpixel((0, 0)), 1020)

    # self: Test context checking one rounded native RGB565 blend after opacity and coverage combine.
    def test_coverage_composes_opacity_once_in_native_rgb565(self):
        renderer = CodyRenderer()
        white = next(entry['index'] for entry in self.palette['entries'] if entry['id'] == 'fixed.highlight')
        image = renderer.indexed().convert('RGB')
        rectangle = [(0.25, 0.25), (2.75, 0.25), (2.75, 2.75), (0.25, 2.75)]
        renderer.fill(image, rectangle, (white, white), 128, FillAntialiasing.COVERAGE)
        self.assertEqual(image.getpixel((0, 0)), (74, 72, 74))
        self.assertEqual(image.getpixel((1, 0)), (98, 97, 98))
        self.assertEqual(image.getpixel((1, 1)), (131, 129, 131))
        self.assertEqual(image.getpixel((3, 1)), (0, 0, 0))

        legacy = renderer.indexed()
        renderer.fill(legacy, rectangle, (white, white), 255, FillAntialiasing.DISABLED)
        self.assertEqual(legacy.mode, 'P')
        self.assertEqual(legacy.getpixel((1, 1)), white)

    # self receives the test context: navigation must hit the controls in the last displayed viewport.
    def test_tk_boot_back_and_point_inspection(self):
        root=tk.Tk(); root.withdraw()
        window=CodyWindow(root,CodyRenderer(),SimpleNamespace(screen='boot-ready',rotation=180,safe_guide=True))
        try:
            root.geometry('1100x760')
            root.deiconify()
            self.render_preview_frame(window)
            x, y = window.layout.to_canvas(233, 233, window.display_rotation)
            window.touch(SimpleNamespace(x=x,y=y))
            self.assertEqual(window.screen.get(),'rig')
            self.render_preview_frame(window)
            x, y = window.layout.to_canvas(426, 233, window.display_rotation)
            window.rotation.set(0)
            window.touch(SimpleNamespace(x=x,y=y))
            self.assertFalse(window.debug.get())
            self.assertEqual(window.display_rotation, 180)
            window.touch(SimpleNamespace(x=x,y=y))
            self.assertTrue(window.debug.get())
            window.preset('sleep')
            self.assertEqual([value.get() for value in window.values][:2],[0,0])
            x, y = window.layout.to_canvas(40, 233, window.display_rotation)
            # The selector can request a new route before its first frame is presented.
            window.screen.set('boot-waking')
            window.touch(SimpleNamespace(x=x,y=y))
            self.assertEqual(window.screen.get(),'boot-ready')
            for screen in ('boot-sleep','boot-waking','boot-ready','rig'):
                window.screen.set(screen)
                self.render_preview_frame(window)
                self.assertIsNotNone(window.scene)
        finally:
            window.close()

    # self receives the test context: names and touch must follow the displayed points after resize and rotation.
    def test_tk_name_leaders_follow_evaluated_points_and_rotation(self):
        root = tk.Tk()
        root.withdraw()
        window = CodyWindow(root, CodyRenderer(), SimpleNamespace(screen='rig', rotation=0, safe_guide=False))
        try:
            window.playing.set(False)
            window.contour_playing.set(False)
            window.face_state = list(self.ready)
            for variable, value in zip(window.values, self.ready):
                variable.set(value)
            window.names.set(True)
            window.label_scope.set('Contour')
            expected_names = {point['name'] for point in self.rig['points']
                              if point['role'] == 'anchor' and point['name'].startswith('lobe.')}
            state = make_contour_state(self.rig)
            state.update(fullness=.4, amplitude=.8)
            displayed_sizes = []
            displayed_modes = []
            root.deiconify()
            for geometry, phase in (('920x780', .3), ('1280x780', 2.1)):
                root.geometry(geometry)
                state['phase'] = phase
                window.contour_state = dict(state)
                for angle in (0, 90, 180, 270):
                    window.rotation.set(angle)
                    self.render_preview_frame(window)
                    shown_names = window.overlay_names()
                    self.assertTrue(shown_names)
                    self.assertTrue(set(shown_names) <= expected_names)
                    for name in shown_names:
                        with self.subTest(geometry=geometry, phase=phase, rotation=angle, point=name):
                            items = window.canvas.find_withtag(f'point:{name}')
                            leaders = [item for item in items if window.canvas.type(item) == 'line']
                            markers = [item for item in items if window.canvas.type(item) == 'oval']
                            labels = [item for item in items if window.canvas.type(item) == 'text']
                            self.assertEqual(len(leaders), 1)
                            self.assertEqual(len(markers), 1)
                            self.assertEqual(len(labels), 1)
                            expected = window.layout.to_canvas(*window.scene['world'][name], angle)
                            endpoint = window.canvas.coords(leaders[0])[-2:]
                            marker = window.canvas.coords(markers[0])
                            for axis in (0, 1):
                                self.assertAlmostEqual(endpoint[axis], expected[axis], places=8)
                                self.assertAlmostEqual((marker[axis] + marker[axis + 2]) / 2,
                                                       expected[axis], places=8)
                            visible_name = window.canvas.itemcget(labels[0], 'text').rstrip('.…')
                            self.assertTrue(visible_name)
                            self.assertTrue(name.startswith(visible_name))
                            self.assertEqual(float(window.canvas.itemcget(labels[0], 'angle')), 0)
                    target = window.scene['world']['lobe.crown']
                    x, y = window.layout.to_canvas(*target, window.display_rotation)
                    window.canvas.event_generate('<Motion>', x=round(x), y=round(y))
                    window.touch(SimpleNamespace(x=x, y=y))
                    self.assertEqual(window.selected, 'lobe.crown')
                    window.choose_point(None)
                displayed_sizes.append(window.layout.image_size)
                displayed_modes.append(window.layout_mode)
            self.assertNotEqual(displayed_sizes[0], displayed_sizes[1])
            self.assertEqual(displayed_modes, ['stacked', 'wide'])
            window.choose_point('eye.left.p1')
            self.assertIn('eye.left.p1', window.overlay_names())
            window.choose_point(None)
            window.label_scope.set('All points')
            all_names = {point['name'] for point in self.rig['points']}
            visited = set()
            capacity = window.layout.label_capacity(window.label_row_height)
            for page in range(math.ceil(len(all_names) / capacity)):
                window.label_page.set(page)
                names = window.overlay_names()
                self.assertLessEqual(len(names), capacity)
                self.assertTrue(visited.isdisjoint(names))
                visited.update(names)
            self.assertEqual(visited, all_names)
            window.names.set(False)
            window.draw_labels()
            self.assertFalse(window.canvas.find_withtag('rig_label'))
        finally:
            window.close()


# output receives artifacts only under the checkout; renderer provides current authoring tables.
def previews(output,renderer):
    placement=tuple(renderer.rig['default_transform'][key] for key in ('x','y','scale'))
    ready=renderer.rig['boot_channels']['ready']
    for screen in ('boot-sleep','boot-waking','boot-ready','rig'):
        image,_=renderer.render(screen,animate(ready,1000),placement)
        renderer.capture_sheet(image,screen+' / HOST PREVIEW').save(output/f'{screen}.png')
    for angle in (90,180,270):
        image,_=renderer.render('rig',ready,placement,rotation=angle,guide=True)
        renderer.capture_sheet(image,f'rig / {angle} deg / HOST PREVIEW').save(output/f'rig-{angle}.png')
    samples=[]; frames=[]
    # Twenty-four seconds is the exact common period of the breathing and blink clocks.
    for elapsed in range(0,24000,40):
        started=time.perf_counter()
        image,_=renderer.render('rig',animate(ready,elapsed),placement,debug=False)
        samples.append((time.perf_counter()-started)*1000)
        frames.append(renderer.capture_sheet(image,'ANIMATION / 25 FPS GIF / HOST ONLY'))
    frames[0].save(output/'cody-animation.gif',save_all=True,append_images=frames[1:],duration=40,loop=0,optimize=False)
    return {'host_render_frames':len(samples),'host_render_mean_ms':statistics.mean(samples),'host_render_p95_ms':sorted(samples)[math.ceil(len(samples)*.95)-1],
            'gif_duration_ms':24000,'gif_playback_fps':25,'firmware_performance':'UNMEASURED','native_raster_parity':'NOT VERIFIED'}


# No parameters: run only the focused PoC host package explicitly requested by the user.
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previews',action='store_true')
    args=parser.parse_args()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RigContractTests))
    if not result.wasSuccessful(): raise SystemExit(1)
    output=ROOT/'artifacts/cody-poc'; output.mkdir(parents=True,exist_ok=True)
    report={'host_checks_passed':result.testsRun,'firmware_build':'NOT RUN','hardware':'NOT ACCESSED'}
    if args.previews:
        render_report=previews(output,CodyRenderer())
        (output/'host-render-review.json').write_text(json.dumps(render_report,indent=2)+'\n',encoding='utf-8')
        report.update(render_report)
    (output/'host-review.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
