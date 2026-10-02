"""Explicit host-only Cody PoC checks and preview artifacts; never accesses firmware or hardware."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
import time
import unittest
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'simulator/windows'))
from cody_rig_contract import (
    advance_contour, animate, contour_defaults, evaluate, load_contract,
    make_contour_state, unique_object, validate_channels,
)
from cody_simulator import (
    LABEL_MARGIN, LABEL_PAGE_SIZE, CodyRenderer, CodyWindow,
    logical_touch, orient, physical_point,
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

    # No parameters beyond the test instance: generated tables must match the current authoring inputs.
    def test_generated_provenance(self):
        manifest=json.loads((ROOT/'assets/cody/rig/generated/manifest.json').read_text())
        self.assertEqual(manifest['source_sha256'],hashlib.sha256((ROOT/'assets/cody/cody.yaml').read_bytes()).hexdigest())
        self.assertEqual(manifest['palette_sha256'],hashlib.sha256((ROOT/'assets/cody/palette/palette256.json').read_bytes()).hexdigest())

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

    # No parameters beyond the test instance: closing one eye cannot override another facial channel.
    def test_independent_eye_and_mouth_channels(self):
        base=evaluate(self.rig,self.ready,self.transform)
        changed=list(self.ready); changed[0]=0
        altered=evaluate(self.rig,changed,self.transform)
        for point in self.rig['points']:
            if point['channel']!='eye_left': self.assertEqual(base['local'][point['name']],altered['local'][point['name']])
        self.assertNotEqual(base['local']['eye.left.p3'],altered['local']['eye.left.p3'])
        sleeping=animate([0,0,.5,-.4,.1,0],1000)
        self.assertEqual(sleeping[:4],[0,0,.5,-.4])
        self.assertGreater(sleeping[4],.1)

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
        for channels in (self.ready, [0, 1, -.5, .4, .8, -.5], [1, 0, .7, -.6, 0, 1]):
            with self.subTest(channels=channels):
                original = evaluate(self.rig, channels, self.transform)
                scene = evaluate(self.rig, channels, self.transform, contour=state)
                for name in original['world']:
                    if not name.startswith('lobe.'):
                        self.assertEqual(scene['local'][name], original['local'][name], name)
                        self.assertEqual(scene['world'][name], original['world'][name], name)
                for before, after in zip(original['paths'], scene['paths']):
                    if before['definition']['name'] != 'body':
                        self.assertEqual(after, before)
                self.assertEqual(scene['scale'], original['scale'])
                self.assertTrue(any(scene['world'][name] != original['world'][name]
                                    for name in original['world'] if name.startswith('lobe.')))

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
        channels[5] = .7
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

    # self receives the test context: orientation must retain subpixel precision for evaluated curve points.
    def test_evaluated_point_rotation_round_trips_without_rounding(self):
        state = make_contour_state(self.rig)
        state.update(fullness=.4, amplitude=.8, phase=1.23)
        scene = evaluate(self.rig, self.ready, (233.4, 235.8, 145.3), contour=state)
        self.assertTrue(any(value != round(value) for point in scene['world'].values() for value in point))
        for angle in (0, 90, 180, 270):
            for name, logical in scene['world'].items():
                with self.subTest(rotation=angle, point=name):
                    physical = physical_point(*logical, angle)
                    restored = logical_touch(*physical, angle)
                    self.assertAlmostEqual(restored[0], logical[0], places=10)
                    self.assertAlmostEqual(restored[1], logical[1], places=10)
        with self.assertRaises(ValueError):
            physical_point(233.4, 235.8, 45)

    # No parameters beyond the test instance: invalid values are rejected instead of silently repaired.
    def test_invalid_input_rejected(self):
        for bad in (-.01,1.01,math.nan,math.inf,True):
            values=list(self.ready); values[0]=bad
            with self.assertRaises(ValueError): evaluate(self.rig,values,self.transform)
        with self.assertRaises(ValueError): evaluate(self.rig,self.ready,(0,0,0))
        with self.assertRaises(ValueError): unique_object([('name','Cody'),('name','Other')])
        with self.assertRaises(ValueError): animate(self.ready,-1)

    # No parameters beyond the test instance: the renderer uses at most the prepared 256 RGB565 slots.
    def test_palette_and_debug_colors(self):
        renderer=CodyRenderer()
        scene=evaluate(self.rig,self.ready,self.transform)
        image=renderer.draw_scene(scene,True)
        self.assertEqual(image.mode,'P')
        self.assertEqual(len(image.getpalette()),768)
        used=set(image.getdata())
        self.assertLessEqual(len(used),256)
        self.assertTrue(set(self.rig['style']['debug'].values())- {self.rig['style']['debug']['selected']} <= used)

    # No parameters beyond the test instance: the actual Tk controls navigate and hit-test a rotated view.
    def test_tk_boot_back_and_point_inspection(self):
        root=tk.Tk(); root.withdraw()
        window=CodyWindow(root,CodyRenderer(),SimpleNamespace(screen='boot-ready',rotation=180,safe_guide=True))
        try:
            window.touch(SimpleNamespace(x=LABEL_MARGIN+232,y=232))
            self.assertEqual(window.screen.get(),'rig')
            window.touch(SimpleNamespace(x=LABEL_MARGIN+39,y=232))
            self.assertFalse(window.debug.get())
            window.touch(SimpleNamespace(x=LABEL_MARGIN+39,y=232))
            self.assertTrue(window.debug.get())
            window.preset('sleep')
            self.assertEqual([value.get() for value in window.values][:2],[0,0])
            window.touch(SimpleNamespace(x=LABEL_MARGIN+425,y=232))
            self.assertEqual(window.screen.get(),'boot-ready')
            for screen in ('boot-sleep','boot-waking','boot-ready','rig'):
                window.screen.set(screen)
                window.render_tick()
                self.assertIsNotNone(window.scene)
        finally:
            window.close()

    # self receives the test context: every name must stay attached to its rotated, animated scene point.
    def test_tk_name_leaders_follow_evaluated_points_and_rotation(self):
        root = tk.Tk()
        root.withdraw()
        window = CodyWindow(root, CodyRenderer(), SimpleNamespace(screen='rig', rotation=0, safe_guide=False))
        try:
            window.playing.set(False)
            window.contour_playing.set(False)
            window.names.set(True)
            window.label_scope.set('Contour')
            expected_names = {point['name'] for point in self.rig['points']
                              if point['role'] == 'anchor' and point['name'].startswith('lobe.')}
            self.assertEqual(set(window.overlay_names()), expected_names)
            state = make_contour_state(self.rig)
            state.update(fullness=.4, amplitude=.8)
            for phase in (.3, 2.1):
                state['phase'] = phase
                window.scene = evaluate(self.rig, self.ready, self.transform, contour=state)
                for angle in (0, 90, 180, 270):
                    window.rotation.set(angle)
                    window.draw_labels()
                    for name in expected_names:
                        with self.subTest(phase=phase, rotation=angle, point=name):
                            items = window.canvas.find_withtag(f'point:{name}')
                            leaders = [item for item in items if window.canvas.type(item) == 'line']
                            markers = [item for item in items if window.canvas.type(item) == 'oval']
                            labels = [item for item in items if window.canvas.type(item) == 'text']
                            self.assertEqual(len(leaders), 1)
                            self.assertEqual(len(markers), 1)
                            self.assertEqual(len(labels), 1)
                            physical = physical_point(*window.scene['world'][name], angle)
                            expected = (LABEL_MARGIN + physical[0], physical[1])
                            endpoint = window.canvas.coords(leaders[0])[-2:]
                            marker = window.canvas.coords(markers[0])
                            for axis in (0, 1):
                                self.assertAlmostEqual(endpoint[axis], expected[axis], places=8)
                                self.assertAlmostEqual((marker[axis] + marker[axis + 2]) / 2,
                                                       expected[axis], places=8)
                            self.assertEqual(window.canvas.itemcget(labels[0], 'text'), name)
                            self.assertEqual(float(window.canvas.itemcget(labels[0], 'angle')), 0)
            window.choose_point('eye.left.p1')
            self.assertIn('eye.left.p1', window.overlay_names())
            window.choose_point(None)
            window.label_scope.set('All points')
            all_names = {point['name'] for point in self.rig['points']}
            visited = set()
            for page in range(math.ceil(len(all_names) / LABEL_PAGE_SIZE)):
                window.label_page.set(page)
                names = window.overlay_names()
                self.assertLessEqual(len(names), LABEL_PAGE_SIZE)
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
