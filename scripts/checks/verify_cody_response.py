"""Prepared deterministic response checks. Run only on an explicit validation request."""

from copy import deepcopy
from dataclasses import replace
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.common.cody_response import (
    InputOrigin, MotionSample, PowerSample, ResponseInputs, SampleHealth, SampleStamp,
    SpringState, advance_response, advance_spring, compose_response, logical_vector,
    make_response_state, rebase_response_motion,
)
from scripts.common.cody_response_contract import Axis, validate_response_config
from scripts.common.cody_rig_contract import contour_defaults, evaluate, load_contract, make_contour_state
from scripts.common.cody_rig_types import FaceChannel


# sampled_at_us: Synthetic successful measurement time, not a physical device sample.
def current_stamp(sampled_at_us):
    return SampleStamp(SampleHealth.CURRENT, InputOrigin.SYNTHETIC, sampled_at_us, 0)


# sampled_at_us: Monotonic sample timestamp shared by this synthetic input bundle.
# acceleration: Named board-axis vector, or the explicitly synthetic resting gravity vector.
# percentage: Synthetic charge level for independent battery scenarios.
# charging: Synthetic charger state that must not override the charge-driven fatigue curve.
def samples(sampled_at_us, acceleration=None, percentage=100, charging=False):
    if acceleration is None:
        acceleration = {Axis.X: 0.0, Axis.Y: 0.0, Axis.Z: 9.80665}
    stamp = current_stamp(sampled_at_us)
    return ResponseInputs(MotionSample(stamp, acceleration), PowerSample(stamp, percentage, charging))


class ResponseChecks(unittest.TestCase):
    # self: Test context with the real authored skin and its strict response configuration.
    def setUp(self):
        self.rig, self.palette = load_contract()
        self.config = self.rig["response"]
        self.initial = make_response_state(self.config)
        self.placement = tuple(self.rig["default_transform"][name] for name in ("x", "y", "scale"))

    # self: A resting accelerometer includes gravity but must not manufacture a startup kick.
    def test_gravity_primes_without_motion_or_stress(self):
        state = advance_response(self.config, self.initial, samples(1_000_000), 50, 1_000_000)
        self.assertTrue(state.motion_primed)
        self.assertEqual(state.stress, 0)
        for axis in Axis:
            self.assertEqual(state.transient_mps2[axis], 0)
            self.assertEqual(state.body[axis], SpringState())

    # self: Every board axis must drive its authored panel axis with opposing inertia.
    # Raises AssertionError when mapping, sign or independent-axis response is incorrect.
    def test_each_axis_drives_inertia_and_stress(self):
        primed = advance_response(self.config, self.initial, samples(1_000_000), 0, 1_000_000)
        mapping = self.config["motion"]["board_to_panel"]
        for panel_axis in Axis:
            board_axis = Axis(mapping[panel_axis]["axis"])
            sign = mapping[panel_axis]["sign"]
            with self.subTest(board_axis=board_axis, panel_axis=panel_axis, sign=sign):
                acceleration = {Axis.X: 0.0, Axis.Y: 0.0, Axis.Z: 9.80665}
                acceleration[board_axis] += 8
                state = advance_response(self.config, primed, samples(1_020_000, acceleration), 20, 1_020_000)
                self.assertLess(state.body[panel_axis].position * sign, 0)
                self.assertLess(state.body[panel_axis].velocity * sign, 0)
                for other_axis in Axis:
                    if other_axis != panel_axis:
                        self.assertEqual(state.body[other_axis], SpringState())
                self.assertGreater(state.stress, 0)

    # self: Re-rendering one published sample must not re-prime or re-filter its acceleration.
    # Raises AssertionError when a reused sample is consumed twice or its spring stops advancing.
    def test_duplicate_sample_is_consumed_only_once(self):
        primed = advance_response(self.config, self.initial, samples(1_000_000), 0, 1_000_000)
        board_axis = Axis(self.config["motion"]["board_to_panel"][Axis.X]["axis"])
        acceleration = {Axis.X: 0.0, Axis.Y: 0.0, Axis.Z: 9.80665}
        acceleration[board_axis] += 8
        impulse = samples(1_020_000, acceleration)
        state = advance_response(self.config, primed, impulse, 20, 1_020_000)
        repeated = advance_response(self.config, state, impulse, 20, 1_040_000)
        self.assertEqual(repeated.consumed_motion_at_us, state.consumed_motion_at_us)
        self.assertEqual(repeated.baseline_mps2, state.baseline_mps2)
        self.assertEqual(repeated.transient_mps2, state.transient_mps2)
        self.assertNotEqual(repeated.body[Axis.X], state.body[Axis.X])

    # self: Analytical springs should retain displacement/velocity across frame subdivision.
    def test_spring_step_is_independent_of_frame_subdivision(self):
        for damping in (.2, .78, .9999, 1, 1.0001, 2):
            with self.subTest(damping=damping):
                initial = SpringState(.07, -.13)
                single = advance_spring(initial, -.1, .5, 2.8, damping)
                subdivided = initial
                for _ in range(30):
                    subdivided = advance_spring(subdivided, -.1, .5 / 30, 2.8, damping)
                self.assertAlmostEqual(single.position, subdivided.position, places=10)
                self.assertAlmostEqual(single.velocity, subdivided.velocity, places=10)

    # self: Failed or stale motion removes new drive and preserves explicit error visibility.
    def test_unavailable_and_stale_motion_rebase_before_recovery(self):
        primed = advance_response(self.config, self.initial, samples(1_000_000), 0, 1_000_000)
        failed_motion = MotionSample(SampleStamp(SampleHealth.ERROR, InputOrigin.SYNTHETIC, 1_000_000, -1),
                                     {Axis.X: float("nan"), Axis.Y: 0, Axis.Z: 0})
        failed = advance_response(self.config, primed, replace(samples(1_020_000), motion=failed_motion), 20, 1_020_000)
        self.assertEqual(failed.motion_status.health, SampleHealth.ERROR)
        self.assertEqual(failed.motion_status.error, -1)
        self.assertFalse(failed.motion_primed)
        stale = advance_response(self.config, primed, samples(1_000_000), 20, 2_000_000)
        self.assertEqual(stale.motion_status.health, SampleHealth.STALE)
        self.assertFalse(stale.motion_primed)
        recovered = advance_response(self.config, stale, samples(2_020_000, {Axis.X: 9.80665, Axis.Y: 0, Axis.Z: 0}), 20, 2_020_000)
        self.assertEqual(recovered.transient_mps2, {axis: 0 for axis in Axis})

    # self: Lower valid charge yields monotonically greater fatigue; charging alone changes nothing.
    def test_charge_curve_and_charging_are_independent(self):
        levels = (100, 75, 50, 25, 0)
        fatigue_targets = []
        for percentage in levels:
            inputs = samples(1_000_000, percentage=percentage)
            plain = advance_response(self.config, self.initial, inputs, 100, 1_000_000)
            charging = advance_response(self.config, self.initial, replace(inputs, power=replace(inputs.power, charging=True)), 100, 1_000_000)
            self.assertEqual(plain.fatigue, charging.fatigue)
            fatigue_targets.append(plain.fatigue_target)
        self.assertEqual(fatigue_targets[0], 0)
        self.assertEqual(fatigue_targets[-1], 1)
        self.assertEqual(fatigue_targets, sorted(fatigue_targets))

    # self: Missing charge must remain unknown; losing a known battery must hold artistic fatigue.
    def test_unknown_battery_never_manufactures_full_charge(self):
        missing = ResponseInputs()
        unknown = advance_response(self.config, self.initial, missing, 1000, 1_000_000)
        self.assertFalse(unknown.fatigue_known)
        self.assertEqual(unknown.power_status.health, SampleHealth.UNAVAILABLE)
        tired = advance_response(self.config, self.initial, samples(1_000_000, percentage=10), 1500, 1_000_000)
        held = advance_response(self.config, tired, missing, 1000, 2_000_000)
        self.assertEqual(held.fatigue, tired.fatigue)
        self.assertEqual(held.fatigue_target, tired.fatigue_target)

    # self: Body motion must move anchors and every curve handle through the same root transform.
    def test_every_point_uses_the_composed_root(self):
        state = deepcopy(self.initial)
        state.body[Axis.X] = SpringState(.07, .1)
        state.body[Axis.Y] = SpringState(-.04, -.03)
        state.body[Axis.Z] = SpringState(.02, 0)
        face = self.rig["boot_channels"]["ready"]
        contour = make_contour_state(self.rig)
        frame = compose_response(self.rig, state, face, contour_defaults(self.rig), self.placement, 0, False)
        resting = evaluate(self.rig, face, self.placement, contour=contour)
        moved = evaluate(self.rig, frame.face, frame.transform, contour=contour)
        self.assertEqual(resting["local"], moved["local"])
        for point in self.rig["points"]:
            name = point["name"]
            with self.subTest(point=name):
                local_x, local_y = moved["local"][name]
                world_x, world_y = moved["world"][name]
                self.assertAlmostEqual(world_x, frame.current_root[0] + local_x * moved["scale"])
                self.assertAlmostEqual(world_y, frame.current_root[1] + local_y * moved["scale"])

    # self: A display rotation must not rotate the physical inertial direction twice.
    def test_cardinal_vectors_preserve_the_board_direction(self):
        physical = (16.0, -3.0)
        for rotation in (0, 90, 180, 270):
            logical_x, logical_y = logical_vector(*physical, rotation)
            if rotation == 0:
                result = logical_x, logical_y
            elif rotation == 90:
                result = -logical_y, logical_x
            elif rotation == 180:
                result = -logical_x, -logical_y
            else:
                result = logical_y, -logical_x
            self.assertEqual(result, physical)

    # self: Independent stress/fatigue contributions preserve nose/cheek size and paused clocks.
    def test_composition_and_resume_keep_independent_channels(self):
        state = deepcopy(self.initial)
        state.stress = .8
        state.fatigue = .6
        state.fatigue_known = True
        state.breath_phase = 1.2
        state.blink_elapsed_ms = 700
        face = self.rig["boot_channels"]["ready"]
        frame = compose_response(self.rig, state, face, contour_defaults(self.rig), self.placement, 0, False)
        self.assertLess(frame.face[FaceChannel.EYE_LEFT], face[FaceChannel.EYE_LEFT])
        self.assertGreater(frame.face[FaceChannel.BROW_LEFT], face[FaceChannel.BROW_LEFT])
        self.assertEqual(frame.face[FaceChannel.NOSE_SIZE], face[FaceChannel.NOSE_SIZE])
        self.assertEqual(frame.face[FaceChannel.CHEEK_SIZE], face[FaceChannel.CHEEK_SIZE])
        paused = advance_response(self.config, state, ResponseInputs(), 0, 1_000_000, False)
        rebased = rebase_response_motion(paused)
        self.assertEqual(rebased.breath_phase, state.breath_phase)
        self.assertEqual(rebased.blink_elapsed_ms, state.blink_elapsed_ms)
        self.assertEqual(rebased.fatigue, state.fatigue)

    # self: Ambiguous authoring and invalid current samples fail without mutating committed state.
    # Raises AssertionError when malformed authoring or invalid samples are accepted.
    def test_invalid_inputs_and_authoring_fail_without_repair(self):
        for invalid in (-1, 101, float("nan")):
            with self.subTest(percentage=invalid):
                before = deepcopy(self.initial)
                with self.assertRaises(ValueError):
                    advance_response(self.config, self.initial, samples(1_000_000, percentage=invalid), 20, 1_000_000)
                self.assertEqual(self.initial, before)
        bad_axis = deepcopy(self.config)
        bad_axis["motion"]["board_to_panel"][Axis.Y]["axis"] = (
            bad_axis["motion"]["board_to_panel"][Axis.X]["axis"]
        )
        with self.assertRaises(ValueError):
            validate_response_config(bad_axis)
        unknown_field = deepcopy(self.config)
        unknown_field["motion"]["silent_fallback"] = True
        with self.assertRaises(ValueError):
            validate_response_config(unknown_field)
        with self.assertRaises(ValueError):
            advance_response(self.config, self.initial, samples(1_000_001), 20, 1_000_000)
        primed = advance_response(self.config, self.initial, samples(1_000_000), 0, 1_000_000)
        with self.assertRaises(ValueError):
            advance_response(self.config, primed, samples(999_999), 20, 1_000_001)


if __name__ == "__main__":
    unittest.main()
