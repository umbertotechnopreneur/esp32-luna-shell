"""One explicitly synthetic snapshot owner for the Windows stimulus controls."""

from dataclasses import replace
import math

from scripts.common.cody_response import (
    InputOrigin, MotionSample, PowerSample, MicrophoneSample, PlaybackSample,
    ResponseInputs, SampleHealth, SampleStamp,
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
        self.last_interaction_at_us = 0

    # self: Host-only source owner retaining the last explicit preview interaction.
    # now_us: Monotonic timestamp from the same clock used by snapshot().
    # Raises ValueError for a reversed interaction clock.
    def interact(self, now_us):
        if now_us < self.last_interaction_at_us:
            raise ValueError("Synthetic interaction clock reversed")
        self.last_interaction_at_us = now_us

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
    # microphone_levels: Explicit synthetic normalized absolute means for two channels.
    # microphone_health: Explicit synthetic microphone availability/error state.
    # playback_level: Synthetic normalized mean, never an actual codec reading.
    # playback_active: Explicit synthetic playback state.
    # playback_health: Explicit synthetic playback availability/error state.
    # Raises ValueError for unknown enum identities.
    def snapshot(self, now_us, acceleration, motion_health, level_percent, charging, power_health, shake, rotation,
                 *, microphone_levels=(0, 0), microphone_health=SampleHealth.UNAVAILABLE,
                 playback_level=0, playback_active=False, playback_health=SampleHealth.UNAVAILABLE):
        motion_health = SampleHealth(motion_health)
        power_health = SampleHealth(power_health)
        microphone_health = SampleHealth(microphone_health)
        playback_health = SampleHealth(playback_health)
        tick_us = now_us // 20_000 * 20_000
        motion = self.current.motion
        power = self.current.power
        microphone = self.current.microphone
        playback = self.current.playback
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
            if len(microphone_levels) != 2 or any(not math.isfinite(level) or not 0 <= level <= 1 for level in microphone_levels):
                raise ValueError("Synthetic microphone levels must be two normalized means")
            if not math.isfinite(playback_level) or not 0 <= playback_level <= 1 or type(playback_active) is not bool:
                raise ValueError("Synthetic playback level/state rejected")
            mean = tuple(round(level * 32768) for level in microphone_levels)
            microphone = MicrophoneSample(self.stamp(microphone.stamp, microphone_health, tick_us),
                2, mean, mean)  # Constant synthetic PCM has matching mean/peak.
            playback = PlaybackSample(self.stamp(playback.stamp, playback_health, tick_us),
                playback_active, round(playback_level * 32768))
            self.last_sample_tick_us = tick_us
        # Health changes take effect immediately, even between synthetic sample ticks.
        if motion_health != SampleHealth.CURRENT:
            motion = replace(motion, stamp=self.stamp(motion.stamp, motion_health, tick_us))
        if power_health != SampleHealth.CURRENT:
            power = replace(power, stamp=self.stamp(power.stamp, power_health, tick_us))
        if microphone_health != SampleHealth.CURRENT:
            microphone = replace(microphone, stamp=self.stamp(microphone.stamp, microphone_health, tick_us))
        if playback_health != SampleHealth.CURRENT:
            playback = replace(playback, stamp=self.stamp(playback.stamp, playback_health, tick_us))
        self.current = ResponseInputs(motion, power, rotation, microphone, playback, self.last_interaction_at_us)
        return self.current
