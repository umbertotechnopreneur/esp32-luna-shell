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
from cody_rig_contract import animate, evaluate, load_contract, unique_object, validate_channels
from cody_simulator import CodyRenderer, CodyWindow, logical_touch, orient
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
            window.touch(SimpleNamespace(x=232,y=232))
            self.assertEqual(window.screen.get(),'rig')
            window.touch(SimpleNamespace(x=39,y=232))
            self.assertFalse(window.debug.get())
            window.touch(SimpleNamespace(x=39,y=232))
            self.assertTrue(window.debug.get())
            window.preset('sleep')
            self.assertEqual([value.get() for value in window.values][:2],[0,0])
            window.touch(SimpleNamespace(x=425,y=232))
            self.assertEqual(window.screen.get(),'boot-ready')
            for screen in ('boot-sleep','boot-waking','boot-ready','rig'):
                window.screen.set(screen)
                window.render_tick()
                self.assertIsNotNone(window.scene)
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
