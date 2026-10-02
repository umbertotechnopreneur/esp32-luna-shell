"""One explicitly synthetic snapshot owner for the Windows stimulus controls."""

from dataclasses import replace
import math

from scripts.common.cody_response import (
    InputOrigin, MotionSample, PowerSample, ResponseInputs, SampleHealth, SampleStamp,
)
from scripts.common.cody_response_contract import Axis


class SyntheticStimulusSource:
    # self: This host-only owner of the single published synthetic input bundle.
    # An interactive host source; firmware has no import or equivalent synthetic adapter.
    def __init__(self):
        self.current = ResponseInputs()
        self.last_sample_tick_us = 0
        self.pulse_axis = Axis.X
        self.pulse_until_us = 0

    # self: Existing synthetic source; the pulse changes neither model state nor clocks.
    # axis: Named board axis that receives the short synthetic acceleration pulse.
    # now_us: Monotonic host time from the same clock used by snapshot().
    # Raises ValueError for an unsupported axis identity.
    def pulse(self, axis, now_us):
        self.pulse_axis = Axis(axis)
        self.pulse_until_us = now_us + 180_000

    # self: Synthetic owner that keeps source error/availability explicit.
    # old: Last published successful source timestamp, even after an unavailable interval.
    # health: Explicit selected synthetic source health.
    # sampled_at_us: Synthetic measurement timestamp for this publication.
    def stamp(self, old, health, sampled_at_us):
        if health == SampleHealth.CURRENT:
            return SampleStamp(health, InputOrigin.SYNTHETIC, sampled_at_us, 0)
        error = -1 if health == SampleHealth.ERROR else 0x103
        if health == SampleHealth.STALE:
            error = old.error
        return SampleStamp(health, InputOrigin.SYNTHETIC, old.sampled_at_us, error)

    # self: Synthetic snapshot owner, never shared with firmware or hardware services.
    # now_us: Monotonic host time; a slower frame never manufactures skipped samples.
    # acceleration: Synthetic named axis offsets in m/s2, added to a Z gravity baseline.
    # motion_health: Explicit selected source availability/error state.
    # level_percent: Synthetic battery percentage, not a device reading.
    # charging: Synthetic charger flag; it does not reset fatigue.
    # power_health: Explicit selected charge availability/error state.
    # shake: Adds continuous synthetic motion on all three axes.
    # rotation: Current cardinal preview orientation; board axes remain physical.
    # Raises ValueError for unknown enum identities.
    def snapshot(self, now_us, acceleration, motion_health, level_percent, charging, power_health, shake, rotation):
        motion_health = SampleHealth(motion_health)
        power_health = SampleHealth(power_health)
        tick_us = now_us // 20_000 * 20_000
        motion = self.current.motion
        power = self.current.power
        if tick_us > self.last_sample_tick_us:
            values = {axis: float(acceleration[axis]) for axis in Axis}
            values[Axis.Z] += 9.80665
            seconds = tick_us / 1_000_000
            if shake:
                values[Axis.X] += 6 * math.sin(math.tau * 2 * seconds)
                values[Axis.Y] += 4 * math.sin(math.tau * 2.7 * seconds + .8)
                values[Axis.Z] += 4 * math.sin(math.tau * 3.3 * seconds + 1.2)
            if tick_us < self.pulse_until_us:
                values[self.pulse_axis] += 8
            motion_stamp = self.stamp(motion.stamp, motion_health, tick_us)
            power_stamp = self.stamp(power.stamp, power_health, tick_us)
            motion = MotionSample(motion_stamp, values)
            power = PowerSample(power_stamp, float(level_percent), bool(charging))
            self.last_sample_tick_us = tick_us
        # Health changes take effect immediately, even between synthetic sample ticks.
        if motion_health != SampleHealth.CURRENT:
            motion = replace(motion, stamp=self.stamp(motion.stamp, motion_health, tick_us))
        if power_health != SampleHealth.CURRENT:
            power = replace(power, stamp=self.stamp(power.stamp, power_health, tick_us))
        self.current = ResponseInputs(motion, power, rotation)
        return self.current
