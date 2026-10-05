"""Host mirror of CODY_response: observations -> dynamics -> independent channels.

Synthetic inputs exist only in the Windows preview. Model values are artistic
state, never sensor readings or diagnoses. Equations match the native engine.
"""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field, replace
from enum import Enum
import math
import struct

from scripts.common.cody_response_contract import Axis
from scripts.common.cody_rig_types import ContourControl, FaceChannel


class SampleHealth(str, Enum):
    UNAVAILABLE = "UNAVAILABLE"
    CURRENT = "CURRENT"
    STALE = "STALE"
    ERROR = "ERROR"


class InputOrigin(str, Enum):
    HARDWARE = "HARDWARE"
    SYNTHETIC = "SYNTHETIC"


class EyeId(str, Enum):
    NONE = "none"
    LEFT = "left"
    RIGHT = "right"


class IdleSceneId(int, Enum):
    NONE = 0
    YAWN = 0xC002
    WHISTLE = 0xC003
    GREETING = 0xC004


@dataclass(frozen=True)
class SampleStamp:
    health: SampleHealth = SampleHealth.UNAVAILABLE
    origin: InputOrigin = InputOrigin.SYNTHETIC
    sampled_at_us: int = 0
    error: int = 0x103  # ESP_ERR_INVALID_STATE; absence is explicit.


# Create storage keyed by semantic axes, without assigning meaning to list positions.
def zero_axes():
    return {axis: 0.0 for axis in Axis}


@dataclass(frozen=True)
class MotionSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    acceleration_mps2: dict = field(default_factory=zero_axes)


@dataclass(frozen=True)
class PowerSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    level_percent: float = 0.0
    charging: bool = False


@dataclass(frozen=True)
class MicrophoneSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    channel_count: int = 0
    mean_level: tuple = (0, 0)
    peak_level: tuple = (0, 0)


@dataclass(frozen=True)
class PlaybackSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    active: bool = False
    mean_level: int = 0
    cue_id: int = 0


@dataclass(frozen=True)
class EyeTapSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    eye: EyeId = EyeId.NONE


@dataclass(frozen=True)
class BodyTouchSample:
    stamp: SampleStamp = field(default_factory=SampleStamp)
    pressed: bool = False
    displacement_px: tuple = (0, 0)
    rendering_rotation_degrees: int = 0
    gesture_started_at_us: int = 0


@dataclass(frozen=True)
class ResponseInputs:
    motion: MotionSample = field(default_factory=MotionSample)
    power: PowerSample = field(default_factory=PowerSample)
    rendering_rotation_degrees: int = 0
    microphone: MicrophoneSample = field(default_factory=MicrophoneSample)
    playback: PlaybackSample = field(default_factory=PlaybackSample)
    last_interaction_at_us: int = 0
    eye_tap: EyeTapSample = field(default_factory=EyeTapSample)
    body_touch: BodyTouchSample = field(default_factory=BodyTouchSample)
    idle_scene_allowed: bool = False


@dataclass(frozen=True)
class SpringState:
    position: float = 0.0
    velocity: float = 0.0


# Each independent body spring retains its velocity between frames.
def resting_body():
    return {axis: SpringState() for axis in Axis}


@dataclass
class ResponseState:
    baseline_mps2: dict = field(default_factory=zero_axes)
    transient_mps2: dict = field(default_factory=zero_axes)
    body: dict = field(default_factory=resting_body)
    motion_status: SampleStamp = field(default_factory=SampleStamp)
    power_status: SampleStamp = field(default_factory=SampleStamp)
    consumed_motion_at_us: int = 0
    consumed_power_at_us: int = 0
    motion_primed: bool = False
    fatigue_known: bool = False
    stress: float = 0.0
    fatigue: float = 0.0
    fatigue_target: float = 0.0
    breath_phase: float = 0.0
    breath_rate_hz: float = 0.0
    blink_elapsed_ms: float = 0.0
    microphone_status: SampleStamp = field(default_factory=SampleStamp)
    playback_status: SampleStamp = field(default_factory=SampleStamp)
    consumed_microphone_at_us: int = 0
    consumed_playback_at_us: int = 0
    consumed_interaction_at_us: int = 0
    microphone_target: float = 0.0
    playback_target: float = 0.0
    attention: float = 0.0
    speaking: float = 0.0
    idle_seconds: float = 0.0
    yawn_seconds: float = 0.0
    yawn_cooldown_seconds: float = 0.0
    yawn_weight: float = 0.0
    yawning: bool = False
    idle_scene: IdleSceneId = IdleSceneId.NONE
    last_idle_scene: IdleSceneId = IdleSceneId.NONE
    idle_scene_generation: int = 0
    idle_random_state: int = 0x6D2B79F5
    idle_wait_fraction: float = 0.0
    idle_scene_seconds: float = 0.0
    idle_scene_weight: float = 0.0
    idle_remaining_scenes: int = 0
    eye_tap_status: SampleStamp = field(default_factory=SampleStamp)
    consumed_eye_tap_at_us: int = 0
    eye_hold_seconds: float = 0.0
    protective_eye_closure: float = 0.0
    annoyance: float = 0.0
    body_touch_status: SampleStamp = field(default_factory=SampleStamp)
    consumed_body_touch_at_us: int = 0
    consumed_body_press_at_us: int = 0
    drag_x: SpringState = field(default_factory=SpringState)
    drag_y: SpringState = field(default_factory=SpringState)
    touch_pressure: SpringState = field(default_factory=SpringState)


@dataclass(frozen=True)
class ResponseFrame:
    face: list
    contour: dict
    transform: tuple
    resting_root: tuple
    current_root: tuple
    spring_velocity_px_per_second: tuple
    depth_scale: float


# config: Strict response section already checked by load_contract.
def make_response_state(config):
    return ResponseState(breath_rate_hz=config["expression"]["base_breath_hz"])


# state: Previous model; discard motion history across standby without resetting phases.
def rebase_response_motion(state):
    next_state = deepcopy(state)
    next_state.motion_primed = False
    next_state.transient_mps2 = zero_axes()
    return next_state


# value: Runtime scalar at a public input boundary.
# label: Source/field name attached to a rejection.
# Raises ValueError for nonnumeric or nonfinite values.
def finite_number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label}: rejected nonfinite/nonnumeric value {value!r}")
    return float(value)


# stamp: Source metadata; a failed source is handled explicitly, not turned into success.
# now_us: Current monotonic observation time.
# timeout_seconds: Declared maximum lifetime of a published sample.
# source: Stable source name used in error messages.
# Raises ValueError for inconsistent metadata or a future timestamp.
def effective_stamp(stamp, now_us, timeout_seconds, source):
    if not isinstance(stamp.health, SampleHealth) or not isinstance(stamp.origin, InputOrigin):
        raise ValueError(f"{source}: unknown source health/origin")
    if type(stamp.sampled_at_us) is not int or not 0 <= stamp.sampled_at_us <= now_us:
        raise ValueError(f"{source}: invalid sample timestamp {stamp.sampled_at_us!r}")
    if type(stamp.error) is not int:
        raise ValueError(f"{source}: error must retain the original numeric code")
    if stamp.health == SampleHealth.ERROR and stamp.error == 0:
        raise ValueError(f"{source}: ERROR requires an original failure code")
    if stamp.health == SampleHealth.CURRENT:
        if stamp.error != 0 or stamp.sampled_at_us == 0:
            raise ValueError(f"{source}: CURRENT requires a successful timestamped sample")
        if now_us - stamp.sampled_at_us >= timeout_seconds * 1_000_000:
            return replace(stamp, health=SampleHealth.STALE)
    return stamp


# state: One scalar spring's displacement and velocity.
# target: Bounded artistic equilibrium for the current derived motion cue.
# seconds: Active elapsed time, with no fixed-step catch-up loop.
# frequency_hz: Spring natural frequency from authoring.
# damping_ratio: Reviewed damping ratio, including under/critical/overdamped cases.
def advance_spring(state, target, seconds, frequency_hz, damping_ratio):
    if seconds == 0:
        return state
    omega = math.tau * frequency_hz
    displacement = state.position - target
    velocity = state.velocity
    decay_rate = damping_ratio * omega
    if damping_ratio < 1:
        damped_frequency = omega * math.sqrt(1 - damping_ratio * damping_ratio)
        decay = math.exp(-decay_rate * seconds)
        cosine = math.cos(damped_frequency * seconds)
        sine = math.sin(damped_frequency * seconds)
        companion = (velocity + decay_rate * displacement) / damped_frequency
        position = target + decay * (displacement * cosine + companion * sine)
        speed = decay * (velocity * cosine - (decay_rate * velocity + omega * omega * displacement) / damped_frequency * sine)
    elif damping_ratio == 1:
        decay = math.exp(-omega * seconds)
        companion = velocity + omega * displacement
        position = target + decay * (displacement + companion * seconds)
        speed = decay * (velocity - omega * companion * seconds)
    else:
        root = math.sqrt(damping_ratio * damping_ratio - 1)
        slow_rate = -omega / (damping_ratio + root)
        fast_rate = -omega * (damping_ratio + root)
        slow_weight = (velocity - fast_rate * displacement) / (slow_rate - fast_rate)
        fast_weight = displacement - slow_weight
        slow_decay = math.exp(slow_rate * seconds)
        fast_decay = math.exp(fast_rate * seconds)
        position = target + slow_weight * slow_decay + fast_weight * fast_decay
        speed = slow_rate * slow_weight * slow_decay + fast_rate * fast_weight * fast_decay
    return SpringState(position, speed)


# previous: Current normalized artistic state.
# target: Requested normalized state.
# seconds: Active elapsed time.
# attack: Time constant when the target increases.
# release: Time constant when the target decreases.
def blend_state(previous, target, seconds, attack, release):
    time_constant = attack if target > previous else release
    return previous + (target - previous) * -math.expm1(-seconds / time_constant)


# value: Requested artistic output, not a sensor measurement.
# limit: Explicit maximum excursion; zero disables this effect.
def soft_limit(value, limit):
    return limit * math.tanh(value / limit) if limit > 0 else 0.0


# state: Previously committed artistic state whose bounds protect the rig composer.
# Raises ValueError for invalid axes, timestamps, nonfinite dynamics or normalized state.
def validate_response_state(state):
    ranges = {
        "stress": (0, 1), "fatigue": (0, 1), "fatigue_target": (0, 1),
        "breath_phase": (0, math.tau), "breath_rate_hz": (0, 3),
        "blink_elapsed_ms": (0, 4800),
        "attention": (0, 1), "speaking": (0, 1),
        "microphone_target": (0, 1), "playback_target": (0, 1),
        "idle_seconds": (0, 1200), "yawn_seconds": (0, 10),
        "idle_wait_fraction": (0, 1), "idle_scene_seconds": (0, 10), "idle_scene_weight": (0, 1),
        "yawn_cooldown_seconds": (0, 600), "yawn_weight": (0, 1),
        "eye_hold_seconds": (0, 3), "protective_eye_closure": (0, 1), "annoyance": (0, 1),
    }
    if not isinstance(state.idle_scene, IdleSceneId) or not isinstance(state.last_idle_scene, IdleSceneId):
        raise ValueError("response.state.idle_scene: unknown identity")
    if type(state.idle_random_state) is not int or not 1 <= state.idle_random_state <= 0xFFFFFFFF:
        raise ValueError("response.state.idle_random_state: expected nonzero uint32")
    previous_bit = {IdleSceneId.NONE: 0, IdleSceneId.YAWN: 1,
                    IdleSceneId.WHISTLE: 2, IdleSceneId.GREETING: 4}[state.last_idle_scene]
    if (type(state.idle_remaining_scenes) is not int or not 0 <= state.idle_remaining_scenes <= 7 or
            state.idle_remaining_scenes & previous_bit):
        raise ValueError("response.state.idle_remaining_scenes: invalid shuffle bag")
    if type(state.idle_scene_generation) is not int or not 0 <= state.idle_scene_generation <= 0xFFFFFFFF:
        raise ValueError("response.state.idle_scene_generation: expected uint32")
    for name, (minimum, maximum) in ranges.items():
        value = finite_number(getattr(state, name), f"response.state.{name}")
        if not minimum <= value <= maximum:
            raise ValueError(f"response.state.{name}: rejected {value}")
    if state.breath_phase >= math.tau or state.blink_elapsed_ms >= 4800:
        raise ValueError("Response phases must use half-open cycle bounds")
    for name in ("baseline_mps2", "transient_mps2", "body"):
        if set(getattr(state, name)) != set(Axis):
            raise ValueError(f"response.state.{name}: requires exactly x, y and z")
    for axis in Axis:
        finite_number(state.baseline_mps2[axis], f"response.baseline.{axis.value}")
        finite_number(state.transient_mps2[axis], f"response.transient.{axis.value}")
        finite_number(state.body[axis].position, f"response.body.{axis.value}.position")
        finite_number(state.body[axis].velocity, f"response.body.{axis.value}.velocity")
    for name in ("drag_x", "drag_y", "touch_pressure"):
        spring = getattr(state, name)
        finite_number(spring.position, f"response.{name}.position")
        finite_number(spring.velocity, f"response.{name}.velocity")
    for name in ("consumed_motion_at_us", "consumed_power_at_us", "consumed_microphone_at_us",
                 "consumed_playback_at_us", "consumed_interaction_at_us", "consumed_eye_tap_at_us",
                 "consumed_body_touch_at_us", "consumed_body_press_at_us"):
        timestamp = getattr(state, name)
        if type(timestamp) is not int or timestamp < 0:
            raise ValueError(f"response.state.{name}: rejected timestamp {timestamp!r}")


# state: Transaction-local state whose PRNG advances only at a scheduling decision.
def next_idle_random(state):
    value = state.idle_random_state
    value ^= (value << 13) & 0xFFFFFFFF
    value ^= value >> 17
    value ^= (value << 5) & 0xFFFFFFFF
    state.idle_random_state = value
    return value


# state: Session shuffle bag; consume all three named scenes before refilling.
# random_value: One uint32 draw with the same mapping as the native engine.
def select_idle_scene(state, random_value):
    if state.idle_remaining_scenes == 0:
        state.idle_remaining_scenes = 7
    options = [(scene, bit) for scene, bit in (
        (IdleSceneId.YAWN, 1), (IdleSceneId.WHISTLE, 2), (IdleSceneId.GREETING, 4))
        if state.idle_remaining_scenes & bit and scene != state.last_idle_scene]
    if not options:
        raise ValueError("Idle shuffle bag has no eligible scene")
    scene, bit = options[random_value % len(options)]
    state.idle_remaining_scenes &= ~bit
    return scene


# value: A native float operation whose rounding affects idle-scene admission.
# Raises OverflowError when the value cannot be represented by the firmware float.
def native_float(value):
    return struct.unpack("f", struct.pack("f", value))[0]


# config: Existing authored delay, duration and cooldown.
# busy: Presentation/activity priority gate, independent of sensor validity.
# seconds: Active elapsed time; zero cannot schedule a new scene.
# next_state: Transaction-local Windows model; never published into firmware.
def advance_idle_scene(config, busy, seconds, next_state):
    idle = config["idle"]
    # Firmware accumulates float32 clocks; double precision changes boundary frames.
    seconds = native_float(seconds)
    if busy:
        if next_state.idle_scene != IdleSceneId.NONE:
            next_state.yawn_cooldown_seconds = idle["cooldown_seconds"]
        next_state.idle_scene = IdleSceneId.NONE
        next_state.idle_seconds = 0
        next_state.idle_scene_seconds = 0
        next_state.idle_scene_weight = 0
        next_state.yawning = False
        next_state.yawn_seconds = 0
        next_state.yawn_weight = 0
        return
    if seconds == 0:
        return
    next_state.yawn_cooldown_seconds = max(0, native_float(next_state.yawn_cooldown_seconds - seconds))
    if next_state.idle_scene == IdleSceneId.NONE:
        if next_state.yawn_cooldown_seconds > 0:
            return
        if next_state.idle_seconds == 0:
            next_state.idle_wait_fraction = native_float((next_idle_random(next_state) & 0xFFFF) / 65535)
        delay = native_float(native_float(idle["after_seconds"]) * native_float(1 + next_state.idle_wait_fraction))
        next_state.idle_seconds = min(delay, native_float(next_state.idle_seconds + seconds))
        if next_state.idle_seconds < delay:
            return
        next_state.idle_scene = select_idle_scene(next_state, next_idle_random(next_state))
        next_state.last_idle_scene = next_state.idle_scene
        next_state.idle_scene_generation = (next_state.idle_scene_generation + 1) & 0xFFFFFFFF
        next_state.idle_scene_seconds = 0
        next_state.idle_scene_weight = 0
        return
    next_state.idle_scene_seconds = native_float(next_state.idle_scene_seconds + seconds)
    if next_state.idle_scene_seconds >= native_float(idle["duration_seconds"]):
        next_state.idle_scene = IdleSceneId.NONE
        next_state.idle_seconds = 0
        next_state.idle_scene_seconds = 0
        next_state.idle_scene_weight = 0
        next_state.yawn_cooldown_seconds = idle["cooldown_seconds"]
    else:
        next_state.idle_scene_weight = math.sin(math.pi * next_state.idle_scene_seconds / idle["duration_seconds"]) ** 2
    next_state.yawning = next_state.idle_scene == IdleSceneId.YAWN
    next_state.yawn_seconds = next_state.idle_scene_seconds if next_state.yawning else 0
    next_state.yawn_weight = next_state.idle_scene_weight if next_state.yawning else 0


# config: Validated authored audio and inactivity settings.
# state: Previous committed artistic model.
# inputs: Single synthetic snapshot in the Windows preview, never firmware telemetry.
# seconds: Active animation time, excluding parked time.
# now_us: Current monotonic observation time.
# next_state: Transaction-local model changed only after source checks succeed.
# Raises ValueError for invalid PCM metadata, levels or interaction timestamps.
def advance_audio_and_idle(config, state, inputs, seconds, now_us, next_state):
    audio, idle = config["audio"], config["idle"]
    next_state.microphone_status = effective_stamp(inputs.microphone.stamp, now_us,
        audio["sample_timeout_seconds"], "audio.microphone")
    next_state.playback_status = effective_stamp(inputs.playback.stamp, now_us,
        audio["sample_timeout_seconds"], "audio.playback")
    microphone = inputs.microphone
    if next_state.microphone_status.health == SampleHealth.CURRENT:
        if type(microphone.channel_count) is not int or not 1 <= microphone.channel_count <= 2:
            raise ValueError("audio.microphone: expected one or two measured channels")
        if len(microphone.mean_level) != 2 or len(microphone.peak_level) != 2:
            raise ValueError("audio.microphone: two explicit channel storage slots required")
        for channel in range(microphone.channel_count):
            mean, peak = microphone.mean_level[channel], microphone.peak_level[channel]
            if type(mean) is not int or type(peak) is not int or not 0 <= mean <= peak <= 32768:
                raise ValueError("audio.microphone: rejected PCM mean/peak level")
        if microphone.stamp.sampled_at_us < state.consumed_microphone_at_us:
            raise ValueError("audio.microphone: sample timestamp went backwards")
        if microphone.stamp.sampled_at_us > state.consumed_microphone_at_us:
            normalized = sum(microphone.mean_level[:microphone.channel_count]) / (32768 * microphone.channel_count)
            next_state.microphone_target = -math.expm1(-normalized / audio["microphone_reference"])
            next_state.consumed_microphone_at_us = microphone.stamp.sampled_at_us
    else:
        next_state.microphone_target = 0
    playback = inputs.playback
    if next_state.playback_status.health == SampleHealth.CURRENT:
        if type(playback.mean_level) is not int or not 0 <= playback.mean_level <= 32768 or type(playback.active) is not bool:
            raise ValueError("audio.playback: rejected PCM level or activity flag")
        if playback.stamp.sampled_at_us < state.consumed_playback_at_us:
            raise ValueError("audio.playback: sample timestamp went backwards")
        if playback.stamp.sampled_at_us > state.consumed_playback_at_us:
            next_state.playback_target = -math.expm1(-playback.mean_level / (32768 * audio["playback_reference"])) if playback.active else 0
            next_state.consumed_playback_at_us = playback.stamp.sampled_at_us
    else:
        next_state.playback_target = 0
    playing = next_state.playback_status.health == SampleHealth.CURRENT and playback.active
    own_whistle = playing and playback.cue_id == IdleSceneId.WHISTLE
    attention_target = 0 if playing else next_state.microphone_target
    next_state.attention = blend_state(state.attention, attention_target, seconds, audio["attack_seconds"], audio["release_seconds"])
    next_state.speaking = blend_state(state.speaking, 0 if own_whistle else next_state.playback_target, seconds, audio["attack_seconds"], audio["release_seconds"])
    interaction_at = inputs.last_interaction_at_us
    if type(interaction_at) is not int or not state.consumed_interaction_at_us <= interaction_at <= now_us:
        raise ValueError("interaction: rejected timestamp")
    interaction = interaction_at > state.consumed_interaction_at_us
    next_state.consumed_interaction_at_us = interaction_at
    if type(inputs.idle_scene_allowed) is not bool:
        raise ValueError("response.idle_scene_allowed: expected bool")
    touching = next_state.body_touch_status.health == SampleHealth.CURRENT and inputs.body_touch.pressed
    busy = (not inputs.idle_scene_allowed or interaction or touching or
            next_state.stress >= idle["activity_stress_threshold"] or
            (not own_whistle and (next_state.microphone_target >= idle["audio_activity_threshold"] or playing)))
    advance_idle_scene(config, busy, seconds, next_state)


# config: Validated immutable response settings.
# state: Previous state; host copies mirror the native transaction boundary.
# inputs: Typed observations with source metadata and board acceleration in m/s2.
# delta_ms: Nonnegative active elapsed time, independent of sample age.
# now_us: Current monotonic time for stale detection.
# animate_rhythm: Whether the breath/blink clocks should advance.
# Raises ValueError for invalid time, configuration use or rejected current samples.
def advance_response(config, state, inputs, delta_ms, now_us, animate_rhythm=True):
    delta_ms = finite_number(delta_ms, "response.delta_ms")
    if delta_ms < 0 or type(now_us) is not int or now_us <= 0:
        raise ValueError("Response requires nonnegative elapsed time and positive monotonic time")
    if type(animate_rhythm) is not bool or inputs.rendering_rotation_degrees not in (0, 90, 180, 270):
        raise ValueError("Response requires a boolean rhythm switch and cardinal display orientation")
    validate_response_state(state)
    seconds = delta_ms / 1000
    next_state = deepcopy(state)
    motion_config = config["motion"]
    next_state.motion_status = effective_stamp(inputs.motion.stamp, now_us, motion_config["sample_timeout_seconds"], "motion.acceleration")
    next_state.power_status = effective_stamp(inputs.power.stamp, now_us, config["power"]["sample_timeout_seconds"], "power.charge")
    motion_current = next_state.motion_status.health == SampleHealth.CURRENT
    if motion_current:
        if set(inputs.motion.acceleration_mps2) != set(Axis):
            raise ValueError("motion.acceleration: exactly three named axes are required")
        raw = {}
        for axis in Axis:
            value = finite_number(inputs.motion.acceleration_mps2[axis], f"motion.acceleration.{axis.value}")
            if abs(value) > 256:
                raise ValueError(f"motion.acceleration.{axis.value}: rejected {value}; reviewed bound +/-256 m/s2")
            raw[axis] = value
        sampled_at_us = next_state.motion_status.sampled_at_us
        if sampled_at_us < state.consumed_motion_at_us:
            raise ValueError("motion.acceleration: sample timestamp went backwards")
        if sampled_at_us > state.consumed_motion_at_us:
            sample_seconds = (sampled_at_us - state.consumed_motion_at_us) / 1_000_000
            # Prime at rest and after gaps: gravity is not an artificial first impulse.
            if not state.motion_primed or sample_seconds >= motion_config["sample_timeout_seconds"]:
                next_state.baseline_mps2 = raw.copy()
                next_state.transient_mps2 = zero_axes()
            else:
                blend = -math.expm1(-sample_seconds / motion_config["baseline_seconds"])
                for axis in Axis:
                    baseline = state.baseline_mps2[axis] + (raw[axis] - state.baseline_mps2[axis]) * blend
                    next_state.baseline_mps2[axis] = baseline
                    next_state.transient_mps2[axis] = raw[axis] - baseline
            next_state.motion_primed = True
            next_state.consumed_motion_at_us = sampled_at_us
    else:
        next_state.motion_primed = False
        next_state.transient_mps2 = zero_axes()

    # A derived cue can drive a spring between samples; the sample is never reconsumed.
    sensitivity = motion_config["sensitivity"]
    mapping = motion_config["board_to_panel"]
    for panel_axis in Axis:
        axis_map = mapping[panel_axis]
        cue = next_state.transient_mps2[Axis(axis_map["axis"])] * axis_map["sign"]
        gain = motion_config["depth_gain"] if panel_axis == Axis.Z else motion_config["translation_gain"]
        limit = motion_config["depth_limit"] if panel_axis == Axis.Z else motion_config["translation_limit"]
        target = soft_limit(-cue * sensitivity * gain, limit)
        next_state.body[panel_axis] = advance_spring(state.body[panel_axis], target, seconds, motion_config["spring_hz"], motion_config["damping_ratio"])
    magnitude = math.sqrt(sum(value * value for value in next_state.transient_mps2.values()))
    normalized_motion = sensitivity * magnitude / motion_config["stress_reference_mps2"]
    stress_target = -math.expm1(-normalized_motion * normalized_motion)
    state_config = config["state"]
    next_state.stress = blend_state(state.stress, stress_target, seconds, state_config["stress_attack_seconds"], state_config["stress_release_seconds"])

    if next_state.power_status.health == SampleHealth.CURRENT:
        percentage = finite_number(inputs.power.level_percent, "power.charge")
        if not 0 <= percentage <= 100 or type(inputs.power.charging) is not bool:
            raise ValueError(f"power.charge: rejected level/charging {percentage}/{inputs.power.charging!r}")
        sampled_at_us = next_state.power_status.sampled_at_us
        if sampled_at_us < state.consumed_power_at_us:
            raise ValueError("power.charge: sample timestamp went backwards")
        if sampled_at_us > state.consumed_power_at_us:
            next_state.fatigue_target = (1 - percentage / 100) ** config["power"]["fatigue_curve"]
            next_state.fatigue_known = True
            next_state.consumed_power_at_us = sampled_at_us
        next_state.fatigue = blend_state(state.fatigue, next_state.fatigue_target, seconds, state_config["fatigue_attack_seconds"], state_config["fatigue_release_seconds"])
    # Unknown charge freezes artistic fatigue. It never invents a full battery or recovery.
    touch = config["touch"]
    next_state.eye_tap_status = effective_stamp(inputs.eye_tap.stamp, now_us,
        touch["event_timeout_seconds"], "touch.eye")
    active_seconds = seconds if animate_rhythm else 0
    next_state.eye_hold_seconds = max(0, state.eye_hold_seconds - active_seconds)
    next_state.annoyance = blend_state(state.annoyance, 0, active_seconds,
        touch["annoyance_release_seconds"], touch["annoyance_release_seconds"])
    if next_state.eye_tap_status.health == SampleHealth.CURRENT:
        if inputs.eye_tap.eye not in (EyeId.LEFT, EyeId.RIGHT):
            raise ValueError("touch.eye: invalid semantic eye identity")
        if inputs.eye_tap.stamp.sampled_at_us < state.consumed_eye_tap_at_us:
            raise ValueError("touch.eye: sample timestamp went backwards")
        if inputs.eye_tap.stamp.sampled_at_us > state.consumed_eye_tap_at_us:
            next_state.consumed_eye_tap_at_us = inputs.eye_tap.stamp.sampled_at_us
            next_state.eye_hold_seconds = touch["eye_hold_seconds"]
            next_state.annoyance = 1
            next_state.stress = max(next_state.stress, touch["stress_gain"])
    closure_target = 1 if next_state.eye_hold_seconds > 0 else 0
    next_state.protective_eye_closure = blend_state(state.protective_eye_closure, closure_target,
        active_seconds, touch["close_seconds"], touch["reopen_seconds"])
    next_state.body_touch_status = effective_stamp(inputs.body_touch.stamp, now_us,
        touch["event_timeout_seconds"], "touch.body")
    drag_target = (0, 0)
    body_pressed = False
    gesture = inputs.body_touch
    if next_state.body_touch_status.health == SampleHealth.CURRENT:
        if len(gesture.displacement_px) != 2:
            raise ValueError("touch.body: expected two logical displacement components")
        displacement = tuple(finite_number(value, "touch.body") for value in gesture.displacement_px)
        if (any(abs(value) > 465 for value in displacement) or
                type(gesture.pressed) is not bool or
                type(gesture.gesture_started_at_us) is not int or
                not 0 < gesture.gesture_started_at_us <= gesture.stamp.sampled_at_us or
                gesture.rendering_rotation_degrees not in (0, 90, 180, 270)):
            raise ValueError("touch.body: rejected coordinates, gesture time or rotation")
        if gesture.stamp.sampled_at_us < state.consumed_body_touch_at_us:
            raise ValueError("touch.body: sample timestamp went backwards")
        next_state.consumed_body_touch_at_us = gesture.stamp.sampled_at_us
        if gesture.rendering_rotation_degrees == inputs.rendering_rotation_degrees:
            body_pressed = gesture.pressed
            if body_pressed:
                drag_target = tuple(soft_limit(value * touch["drag_gain"], touch["drag_limit_px"])
                    for value in displacement)
            if gesture.gesture_started_at_us > state.consumed_body_press_at_us:
                next_state.consumed_body_press_at_us = gesture.gesture_started_at_us
                # Short clicks still leave an artistic pulse even when release
                # arrives before the next frame. The published event stays intact.
                next_state.touch_pressure = replace(next_state.touch_pressure,
                    velocity=next_state.touch_pressure.velocity + touch["press_depth"] * 12)
        else:
            next_state.body_touch_status = replace(next_state.body_touch_status, health=SampleHealth.STALE)
    next_state.drag_x = advance_spring(state.drag_x, drag_target[0], active_seconds,
        touch["spring_hz"], touch["damping_ratio"])
    next_state.drag_y = advance_spring(state.drag_y, drag_target[1], active_seconds,
        touch["spring_hz"], touch["damping_ratio"])
    next_state.touch_pressure = advance_spring(next_state.touch_pressure,
        touch["press_depth"] if body_pressed else 0, active_seconds,
        touch["spring_hz"], touch["damping_ratio"])
    advance_audio_and_idle(config, state, inputs, active_seconds, now_us, next_state)

    if animate_rhythm:
        expression = config["expression"]
        target_rate = expression["base_breath_hz"] * (1 + expression["stress_breath_gain"] * next_state.stress) * (1 - expression["fatigue_breath_slowdown"] * next_state.fatigue)
        time_constant = expression["rhythm_blend_seconds"]
        blend = -math.expm1(-seconds / time_constant)
        integrated_rate = target_rate * seconds + (state.breath_rate_hz - target_rate) * time_constant * blend
        next_state.breath_phase = (state.breath_phase + math.tau * integrated_rate) % math.tau
        next_state.breath_rate_hz = state.breath_rate_hz + (target_rate - state.breath_rate_hz) * blend
        next_state.blink_elapsed_ms = (state.blink_elapsed_ms + delta_ms) % 4800
    validate_response_state(next_state)
    return next_state


# scene: Exactly the shared evaluated geometry presented to the user.
# point: Logical pointer coordinate after cardinal rotation mapping.
# radius: Authored normalized radius, scaled with the same evaluated character.
# Raises ValueError for nonfinite coordinates/radius or missing named eye anchors.
def hit_test_eye(scene, point, radius):
    x, y = (finite_number(value, "eye hit point") for value in point)
    scale = finite_number(scene["scale"], "eye hit scale")
    radius = finite_number(radius, "eye hit radius")
    if scale <= 0 or not .05 <= radius <= .25:
        raise ValueError("Eye hit geometry rejected")
    nearest = EyeId.NONE
    nearest_squared = (radius * scale) ** 2
    for eye, name in ((EyeId.LEFT, "eye.left"), (EyeId.RIGHT, "eye.right")):
        px, py = scene["world"][name]
        squared = (x - px) ** 2 + (y - py) ** 2
        if not math.isfinite(squared):
            raise ValueError("Eye hit anchor rejected")
        if squared <= nearest_squared:
            nearest, nearest_squared = eye, squared
    return nearest


# scene: Evaluated body polygon shared with the visible artwork.
# point: Logical pointer position, with navigation already excluded by the caller.
# Raises ValueError for missing body geometry or nonfinite coordinates.
def hit_test_body(scene, point):
    x, y = (finite_number(value, "body hit point") for value in point)
    body = next((path for path in scene["paths"] if path["definition"]["name"] == "body"), None)
    if body is None:
        raise ValueError("Body hit geometry is missing")
    vertices = body["vertices"]
    if len(vertices) < 3 or not 0 <= x < 466 or not 0 <= y < 466:
        raise ValueError("Body hit geometry rejected")
    inside = False
    previous = vertices[-1]
    for current in vertices:
        ax, ay = (finite_number(value, "body hit vertex") for value in previous)
        bx, by = (finite_number(value, "body hit vertex") for value in current)
        if (ay > y) != (by > y):
            intersection = ax + (y - ay) * (bx - ax) / (by - ay)
            if x < intersection:
                inside = not inside
        previous = current
    return inside


# x: Physical panel-frame horizontal vector component.
# y: Physical panel-frame vertical vector component.
# rotation: Clockwise orientation of the display; translation vectors have no pixel origin.
# Raises ValueError for an unsupported rotation.
def logical_vector(x, y, rotation):
    if rotation == 0:
        return x, y
    if rotation == 90:
        return y, -x
    if rotation == 180:
        return -x, -y
    if rotation == 270:
        return -y, x
    raise ValueError(f"Unsupported response rotation: {rotation}")


# rig: Skin with strict semantic channel/contour bounds and response configuration.
# state: Updated continuous response state.
# base_face: Independent facial controls before artistic response contributions.
# base_contour: Independent contour targets before response contributions.
# placement: Resting logical root x, y and scale.
# rotation: Cardinal orientation used by image presentation.
# animate_rhythm: Whether breath/blink are included in the composed face.
# Raises ValueError for invalid base controls or placement.
def compose_response(rig, state, base_face, base_contour, placement, rotation, animate_rhythm=True):
    from scripts.common.cody_rig_contract import apply_mouth_breathing, validate_channels, validate_contour_values
    validate_channels(rig, base_face)
    validate_contour_values(rig, base_contour)
    validate_response_state(state)
    face = list(base_face)
    contour = dict(base_contour)
    expression = rig["response"]["expression"]
    stress, fatigue = state.stress, state.fatigue
    for eye in (FaceChannel.EYE_LEFT, FaceChannel.EYE_RIGHT):
        face[eye] *= 1 - fatigue * expression["fatigue_eye_closure"]
    for brow in (FaceChannel.BROW_LEFT, FaceChannel.BROW_RIGHT):
        face[brow] += (1 - face[brow]) * stress * expression["stress_brow"]
    face[FaceChannel.MOUTH_OPEN] += (1 - face[FaceChannel.MOUTH_OPEN]) * stress * expression["stress_mouth_open"]
    audio, idle = rig["response"]["audio"], rig["response"]["idle"]
    mouth = FaceChannel.MOUTH_OPEN
    face[mouth] += (1 - face[mouth]) * state.speaking * audio["playback_mouth"]
    face[mouth] += (1 - face[mouth]) * state.yawn_weight * idle["mouth_open"]
    for eye in (FaceChannel.EYE_LEFT, FaceChannel.EYE_RIGHT):
        face[eye] *= 1 - state.yawn_weight * idle["eye_closure"]
        face[eye] *= 1 - state.protective_eye_closure
    for brow in (FaceChannel.BROW_LEFT, FaceChannel.BROW_RIGHT):
        face[brow] += (1 - face[brow]) * state.attention * audio["attention_brow"]
        face[brow] += (1 - face[brow]) * state.annoyance * rig["response"]["touch"]["brow_gain"]
    mouth_min = rig["channels"][FaceChannel.MOUTH_WIDTH]["minimum"]
    face[FaceChannel.MOUTH_WIDTH] -= (face[FaceChannel.MOUTH_WIDTH] - mouth_min) * stress * expression["stress_mouth_narrowing"]
    face[FaceChannel.CHEEK_OPACITY] *= 1 - fatigue * expression["fatigue_cheek_fade"]
    contour[ContourControl.AMPLITUDE] += (1 - contour[ContourControl.AMPLITUDE]) * stress * expression["stress_wave_gain"]
    drag_limit = rig["response"]["touch"]["drag_limit_px"]
    touch_drag = math.hypot(state.drag_x.position, state.drag_y.position) / drag_limit if drag_limit > 0 else 0
    touch_activity = soft_limit(abs(state.touch_pressure.position) * 6 + touch_drag, 1)
    contour[ContourControl.AMPLITUDE] += (1 - contour[ContourControl.AMPLITUDE]) * touch_activity
    contour[ContourControl.SPEED] += (2 - contour[ContourControl.SPEED]) * stress * expression["stress_wave_speed_gain"]
    contour[ContourControl.SPEED] += (2 - contour[ContourControl.SPEED]) * touch_activity * .5
    contour[ContourControl.SPEED] *= 1 - fatigue * expression["fatigue_wave_slowdown"]
    contour[ContourControl.FULLNESS] -= (contour[ContourControl.FULLNESS] + 1) * fatigue * expression["fatigue_fullness"]
    contour[ContourControl.FULLNESS] -= (contour[ContourControl.FULLNESS] + 1) * soft_limit(abs(state.touch_pressure.position), .15)
    if animate_rhythm:
        breathing = math.sin(state.breath_phase)
        blink = .5 - .5 * math.cos(math.tau * state.blink_elapsed_ms / 280) if state.blink_elapsed_ms < 280 else 0
        for eye in (FaceChannel.EYE_LEFT, FaceChannel.EYE_RIGHT):
            face[eye] *= 1 - blink
        apply_mouth_breathing(face, breathing)
        breath = FaceChannel.BREATH
        face[breath] += (1 - abs(face[breath])) * breathing

    gesture = state.idle_scene_weight
    if state.idle_scene == IdleSceneId.WHISTLE:
        face[mouth] += (.24 - face[mouth]) * gesture
        face[FaceChannel.MOUTH_WIDTH] += (.42 - face[FaceChannel.MOUTH_WIDTH]) * gesture
    elif state.idle_scene == IdleSceneId.GREETING:
        face[mouth] += (.65 - face[mouth]) * gesture
        face[FaceChannel.MOUTH_WIDTH] += (1.1 - face[FaceChannel.MOUTH_WIDTH]) * gesture
        face[FaceChannel.EYE_RIGHT] *= 1 - .65 * gesture
        cheek = FaceChannel.CHEEK_OPACITY
        face[cheek] += (1 - face[cheek]) * .25 * gesture

    if len(placement) != 3:
        raise ValueError("Response placement requires x, y and scale")
    x, y, scale = [finite_number(value, "response.placement") for value in placement]
    if not 0 <= x < 466 or not 0 <= y < 466 or not 20 <= scale <= 200:
        raise ValueError(f"Response placement rejected: {placement!r}")
    motion = rig["response"]["motion"]
    horizontal, vertical = state.body[Axis.X].position, state.body[Axis.Y].position
    magnitude = math.hypot(horizontal, vertical)
    # Saturation belongs to the artistic output. Raw observations remain untouched.
    multiplier = soft_limit(magnitude, motion["translation_limit"]) / magnitude if magnitude > 0 else 1
    offset = logical_vector(horizontal * multiplier * scale, vertical * multiplier * scale, rotation)
    velocity = logical_vector(state.body[Axis.X].velocity * scale, state.body[Axis.Y].velocity * scale, rotation)
    drag_magnitude = math.hypot(state.drag_x.position, state.drag_y.position)
    drag_multiplier = soft_limit(drag_magnitude, rig["response"]["touch"]["drag_limit_px"]) / drag_magnitude if drag_magnitude > 0 else 1
    # Pointer displacement is already logical; only IMU vectors need rotation.
    offset = (offset[0] + state.drag_x.position * drag_multiplier,
              offset[1] + state.drag_y.position * drag_multiplier)
    velocity = (velocity[0] + state.drag_x.velocity, velocity[1] + state.drag_y.velocity)
    depth_scale = (1 + soft_limit(state.body[Axis.Z].position, motion["depth_limit"])) * (1 - soft_limit(state.touch_pressure.position, .15))
    transform = (x + offset[0], y + offset[1], scale * depth_scale)
    if not 0 <= transform[0] < 466 or not 0 <= transform[1] < 466 or not 20 <= transform[2] <= 200:
        raise ValueError(f"Response root exceeds rig placement bounds: {transform!r}")
    return ResponseFrame(face, contour, transform, (x, y), transform[:2], velocity, depth_scale)
