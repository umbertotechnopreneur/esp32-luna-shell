"""Real phase-F Studio interactions, selected by the public cody-studio check."""

from copy import deepcopy
import json
from unittest import TestSuite
from unittest.mock import patch

from cody_native_display import completed_studio_frame
from cody_studio_atlas import load_atlas
from cody_studio_runner import verify_current_build


# base_class: Existing real Tk/native fixture, with its established owner and geometry helpers.
# Returns only this phase-F workflow; inherited phase-D/E tests are not duplicated.
def suite(base_class):
    class AtlasUiTests(base_class):
        """Publish and cancel real isolated work while the interactive native frame is retained."""

        # No parameters: isolate restored history before the existing fixture creates its window.
        # Exceptions: Native/Tk/bootstrap failures propagate through the normal test fixture.
        def setUp(self):
            import cody_studio_window
            restore = patch.object(cody_studio_window, 'latest_atlas_path', return_value=None)
            restore.start()
            self.addCleanup(restore.stop)
            super().setUp()
            self.addCleanup(self.window.atlas_executor.shutdown, wait=True, cancel_futures=True)

        # No parameters: exercise native publication, cancellation, export, focus and opt-in watching.
        # Raises AssertionError for session contamination, lost baseline, hidden controls or missing results.
        def test_atlas_generation_publication_cancellation_and_export(self):
            window = self.window
            self.button('Preserve current as baseline').invoke()
            current, baseline = window.current, window.baseline
            retained = (baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes())
            frame, receipt = completed_studio_frame()
            snapshot, scenario = self.device.read_snapshot(), deepcopy(window.runner.scenario)
            nvs_path = self.directory / 'session.nvs'
            nvs = nvs_path.read_bytes()
            window.inputs['battery'].set('27')
            pending_form = window.form()
            window.search.set('P04')
            window.atlas_suite.set('Filtered catalog')
            window.persist()
            self.assertEqual(window.atlas_selection(), (['ui-v1:display'], 'all'))
            self.assertFalse(window.atlas_auto.get())
            self.assertIsNone(window.atlas_watcher)
            window.atlas_generate_button.invoke()
            job = window.atlas_job
            self.assertIsNotNone(job)
            self.assertEqual(window.atlas_generate_button.cget('state'), 'disabled')
            self.assertEqual(window.atlas_cancel_button.cget('state'), 'normal')
            self.wait_for(lambda: window.atlas_job is None, 'isolated atlas publication', timeout=120)
            self.assertIsNotNone(job.process.poll())
            self.assertIsNotNone(job.result, job.error)
            self.assertEqual(window.atlas_manifest_path, job.result)
            manifest = load_atlas(job.result)
            self.assertEqual(len(manifest['cases']), 1)
            self.assertEqual(manifest['cases'][0]['acquisition'], 'captured')
            self.assertIn('1 captured', window.atlas_status.get())
            self.assertIs(window.current, current)
            self.assertIs(window.baseline, baseline)
            self.assertEqual(window.form(), pending_form)
            self.assertEqual(window.runner.scenario, scenario)
            observed_frame, observed_receipt = completed_studio_frame()
            self.assertEqual(observed_frame.tobytes(), frame.tobytes())
            self.assertEqual(observed_receipt, receipt)
            self.assertEqual(self.device.read_snapshot(), snapshot)
            self.assertEqual(nvs_path.read_bytes(), nvs)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)
            automatic = [record for record in window.records if record is not current]
            self.assertEqual(len(automatic), 1)
            self.assertEqual(window.atlas_export_button.cget('state'), 'normal')
            self.assertIn('P04: captured', window.atlas_log.get('1.0', 'end'))

            published = window.atlas_manifest_path
            window.atlas_suite.set('Supported screens')
            window.atlas_generate_button.invoke()
            cancelled = window.atlas_job
            self.assertIsNotNone(cancelled)
            window.atlas_cancel_button.invoke()
            self.wait_for(lambda: window.atlas_job is None, 'suite cancellation and child reaping')
            self.assertIsNotNone(cancelled.process.poll())
            self.assertIsNone(cancelled.result)
            self.assertEqual(window.atlas_manifest_path, published)
            self.assertIn('last published atlas retained', window.atlas_status.get())
            self.assertIs(window.current, current)

            # A corrupt progress record must be handled before the main UI poll
            # callback exits, otherwise it would stop interactive updates as well.
            class BrokenProgressJob:
                done, result, error = False, None, None

                # event: Deliberately malformed child output; no process or native state is created.
                def __init__(self, event):
                    self.event, self.cancelled = event, False

                # No parameters: deliver one malformed event to the actual product boundary.
                def poll(self):
                    return [self.event]

                # No parameters: acknowledge that the UI rejects this diagnostic job.
                def cancel(self):
                    self.cancelled = self.done = True

            for event in ({'type': 'case-result', 'case': None, 'completed': 1, 'total': 1},
                          {'type': 'case-result', 'case': {'id': 'ui-v1:display', 'acquisition': 'captured'},
                           'completed': 'one', 'total': 1}):
                broken = BrokenProgressJob(event)
                previous_poll = window.poll_id
                window.atlas_job = broken
                self.wait_for(lambda: window.atlas_job is None, 'malformed progress rejection')
                self.assertTrue(broken.cancelled)
                self.assertNotEqual(window.poll_id, previous_poll, 'Main poll timer must continue after rejection')
                self.assertIn('Invalid atlas case-result progress payload', window.atlas_status.get())
                self.assertIs(window.current, current)
                self.assertIs(window.baseline, baseline)
                self.assertEqual(window.atlas_manifest_path, published)

            with patch.object(self.ui_module, 'verify_contract_inputs', side_effect=ValueError('Scenario changed')):
                window.load_atlas_result(published)
            self.assertIn('catalog/scenarios changed', window.atlas_status.get())
            self.assertIs(window.current, current)
            self.assertIs(window.baseline, baseline)

            window.atlas_auto.set(True)
            window.change_atlas_watch()
            window.poll_atlas()
            self.assertIsNotNone(window.atlas_watcher)
            self.assertIsNone(window.atlas_job, 'Enabling watch must only seed the current build')
            output = self.directory / 'atlas-ui-export.pdf'
            with patch.object(self.ui_module.filedialog, 'asksaveasfilename', return_value=str(output)):
                window.atlas_export_button.invoke()
            self.wait_for(lambda: not window.atlas_exporting, 'published atlas PDF export', timeout=60)
            self.assertTrue(output.is_file(), window.atlas_status.get())
            self.assertTrue(output.read_bytes().startswith(b'%PDF-'))
            self.assertIsNone(window.atlas_job, 'Atlas output must not trigger the native-build watcher')
            self.assertIs(window.current, current)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)

            # The watcher contracts separately check changed build fingerprints.
            # Inject just that notification here, retaining real job admission,
            # native subprocess preparation, publication and UI adoption.
            window.atlas_suite.set('Filtered catalog')
            window.atlas_watch_deadline = 0
            with patch.object(window.atlas_watcher, 'poll', return_value=verify_current_build()):
                window.poll_atlas()
            automatic_job = window.atlas_job
            self.assertIsNotNone(automatic_job, 'A validated build notification must start an automatic suite')
            self.wait_for(lambda: window.atlas_job is None, 'automatic native atlas publication', timeout=120)
            self.assertIsNotNone(automatic_job.process.poll())
            self.assertIsNotNone(automatic_job.result, automatic_job.error)
            automatic_manifest = load_atlas(automatic_job.result)
            self.assertIs(automatic_manifest['auto'], True)
            self.assertEqual(automatic_manifest['selection'], ['ui-v1:display'])
            self.assertEqual(window.atlas_manifest_path, automatic_job.result)
            self.assertIs(window.current, current)
            self.assertIs(window.baseline, baseline)
            self.assertEqual(window.form(), pending_form)
            self.assertEqual(window.runner.scenario, scenario)
            automatic_frame, automatic_receipt = completed_studio_frame()
            self.assertEqual(automatic_frame.tobytes(), frame.tobytes())
            self.assertEqual(automatic_receipt, receipt)
            self.assertEqual(self.device.read_snapshot(), snapshot)
            self.assertEqual(nvs_path.read_bytes(), nvs)
            self.assertEqual((baseline.image_path.read_bytes(), baseline.metadata_path.read_bytes()), retained)

            self.root.geometry('1440x960+0+0')
            self.root.update()
            for control in (window.atlas_suite_control, window.atlas_generate_button,
                            window.atlas_export_button, window.atlas_log):
                self.reveal_widget(control, window.bottom_canvas)
                control.focus_force()
                self.root.update()
                self.assertEqual(self.root.focus_get(), control)
            self.reveal_widget(window.atlas_generate_button, window.bottom_canvas)
            self.screenshot('studio-atlas-generation.png')
            window.left_visible.set(False)
            window.right_visible.set(False)
            window.columns()
            self.root.geometry('720x540+0+0')
            self.root.update()
            for control in (window.atlas_suite_control, window.atlas_generate_button,
                            window.atlas_export_button, window.atlas_log):
                self.reveal_widget(control, window.bottom_canvas)
            self.reveal_widget(window.atlas_generate_button, window.bottom_canvas)
            self.assert_widget_visible(window.canvas, self.root)
            self.assertTrue(window.canvas.winfo_ismapped(), 'The native preview must remain mapped at 720 x 540')
            self.assertGreaterEqual(window.canvas.winfo_height(), 210,
                                    'The visible preview must retain its caption and 180-pixel native frame')
            images = [item for item in window.canvas.find_all() if window.canvas.type(item) == 'image']
            self.assertEqual(len(images), 1)
            left, top, right, bottom = window.canvas.bbox(images[0])
            offset_x = window.canvas.winfo_rootx() - self.root.winfo_rootx()
            offset_y = window.canvas.winfo_rooty() - self.root.winfo_rooty()
            self.assertGreaterEqual(offset_x + left, 0)
            self.assertGreaterEqual(offset_y + top, 0)
            self.assertLessEqual(offset_x + right, self.root.winfo_width())
            self.assertLessEqual(offset_y + bottom, self.root.winfo_height())
            self.screenshot('studio-atlas-small.png')
            self.assert_frame_geometry(1, minimum_height=180)
            window.atlas_suite.set('Pilot (10)')
            window.persist()
            state = json.loads(self.state_path.read_text(encoding='utf-8'))
            self.assertEqual(state['atlas_suite'], 'Pilot (10)')
            window.atlas_auto.set(False)
            window.change_atlas_watch()
            self.assertIsNone(window.atlas_watcher)
    return TestSuite([AtlasUiTests('test_atlas_generation_publication_cancellation_and_export')])
