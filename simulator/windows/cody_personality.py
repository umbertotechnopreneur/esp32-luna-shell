"""Host-only preview of the native single indexed-alpha personality cue."""

from dataclasses import dataclass, field
from enum import IntEnum
import hashlib
import json
import math
from pathlib import Path

from PIL import Image
from scripts.common.cody_personality_contract import load_personality_rules


PERSONALITY_RULES = load_personality_rules(Path(__file__).resolve().parents[2] / "assets/cody/personality.yaml")
PersonalityCue = IntEnum("PersonalityCue", {"NONE": 0} | {
    item["name"].upper(): item["id"] for item in PERSONALITY_RULES["catalog"]["icons"]}, module=__name__)
CUE_RULES = {PersonalityCue(item["id"]): item for item in PERSONALITY_RULES["catalog"]["icons"]}


class CodySize(IntEnum):
    MICRO = 1
    TINY = 2
    PREFERENCES = 3
    LOOK = 4
    PAIRING = 5
    SMALL = 6
    INVITATION = 7
    MEDIUM = 8
    GREETING = 9
    FULL = 10


class PersonalityPresentation(IntEnum):
    DISABLED = 0
    MAIN = 1
    SLEEP = 2


class PersonalityPresentationStatus(IntEnum):
    HIDDEN = 0
    VISIBLE = 1
    BLOCKED_BY_LAYOUT = 2
    SUPPRESSED_BY_COOLDOWN = 3


class PersonalityPhase(IntEnum):
    HIDDEN = 0
    FADE_IN = 1
    VISIBLE = 2
    FADE_OUT = 3


# admitted_request: Typed synthetic cue; firmware gates device requests before this selection.
# semantic_sleep: Explicit lifecycle/preset permission, independent of eyelid values.
# affection_active: Whether a recognized contact still owns its temporary heart request.
# eye_reaction: Latest synthetic Fear/Frustration reaction, using the firmware priorities.
# Raises ValueError for invalid identity or untyped selection flags.
def select_personality_target(admitted_request, semantic_sleep, affection_active, eye_reaction=PersonalityCue.NONE):
    if (not isinstance(admitted_request, PersonalityCue) or type(semantic_sleep) is not bool or
            type(affection_active) is not bool or not isinstance(eye_reaction, PersonalityCue) or
            eye_reaction not in (PersonalityCue.NONE, PersonalityCue.FEAR, PersonalityCue.FRUSTRATION)):
        raise ValueError("Personality target selection rejected")
    if semantic_sleep:
        return PersonalityCue.SLEEP, True
    requested_priority = 0 if admitted_request == PersonalityCue.NONE else CUE_RULES[admitted_request]["priority"]
    if eye_reaction != PersonalityCue.NONE and CUE_RULES[eye_reaction]["priority"] > requested_priority:
        admitted_request = eye_reaction
        requested_priority = CUE_RULES[eye_reaction]["priority"]
    if affection_active and CUE_RULES[PersonalityCue.AFFECTION]["priority"] > requested_priority:
        return PersonalityCue.AFFECTION, False
    release_affection = affection_active and admitted_request != PersonalityCue.NONE
    return admitted_request, release_affection


SIZES = {CodySize.MICRO: 22, CodySize.TINY: 30, CodySize.PREFERENCES: 42,
         CodySize.LOOK: 44, CodySize.PAIRING: 46, CodySize.SMALL: 58,
         CodySize.INVITATION: 65, CodySize.MEDIUM: 105, CodySize.GREETING: 130,
         CodySize.FULL: 145}


# nominal_scale: Unanimated scene scale, matching the native rig contract.
# Raises ValueError for nonfinite or unsupported scales.
def size_for_scale(nominal_scale):
    if not math.isfinite(nominal_scale) or not 20 <= nominal_scale <= 200:
        raise ValueError(f"Personality nominal scale rejected: {nominal_scale}")
    if nominal_scale <= 22:
        return CodySize.MICRO
    return min((size for size in CodySize if size != CodySize.MICRO),
               key=lambda size: (abs(nominal_scale - SIZES[size]), int(size)))


@dataclass
class PersonalityFollowSample:
    integrals: tuple = (0., 0., 0., 0.)
    duration_us: int = 0
    age_us: int = 0


@dataclass
class PersonalityLag:
    key: tuple | None = None
    x: float = 0
    y: float = 0
    width: float = 0
    height: float = 0
    samples: list = field(default_factory=list)
    sample_index: int = 0
    previous_target: tuple | None = None
    position_stable_us: int = 0
    scale_stable_us: int = 0


@dataclass
class PersonalityChannel:
    cue: PersonalityCue = PersonalityCue.NONE
    desired: PersonalityCue = PersonalityCue.NONE
    phase: PersonalityPhase = PersonalityPhase.HIDDEN
    desired_age_us: int = 0
    phase_elapsed_us: int = 0
    opacity: float = 0
    fade_out_opacity: float = 0


@dataclass
class PersonalityFrame:
    cue: PersonalityCue = PersonalityCue.NONE
    left: int = 0
    top: int = 0
    width: int = 0
    height: int = 0
    opacity: int = 0
    status: PersonalityPresentationStatus = PersonalityPresentationStatus.HIDDEN


# root: Checkout containing the validated build-time personality YAML.
# Raises OSError for unreadable configuration and ValueError for missing/duplicate values.
def load_personality_config(root):
    rules = load_personality_rules(root / "assets/cody/personality.yaml")
    icons = rules["icons"]
    profiles = {name: dict(profile) for name, profile in icons["anchors"].items()}
    constants = {name.removesuffix("_ms") + "_us": icons["channel"][name] * 1000
                 for name in ("stability_ms", "fade_in_ms", "minimum_visible_ms", "fade_out_ms")}
    for name, value in icons["follow"].items():
        constants[name.removesuffix("_ms") + "_us" if name.endswith("_ms") else name] = (
            value * 1000 if name.endswith("_ms") else value)
    constants["history_bucket_count"] = constants["scale_window_us"] // constants["history_bucket_us"] + 1
    return profiles, constants


class PersonalityPainter:
    # self: Host-only image owner; decoded resources never enter firmware snapshots.
    # root: Repository root containing generated fixed-palette indices and coverage.
    # rig: Loaded rig with canonical named lobe anchors.
    # Raises OSError for unreadable assets and ValueError for mismatched identities/hashes.
    def __init__(self, root, rig):
        self.rig = rig
        self.lag = PersonalityLag()
        self.channel = PersonalityChannel()
        self.profiles, self.config = load_personality_config(root)
        rules = load_personality_rules(root / "assets/cody/personality.yaml")
        self.cue_rules = {PersonalityCue(item["id"]): item for item in rules["catalog"]["icons"]}
        self.active_elapsed_us = 0
        self.last_admitted_at_us = {cue: 0 for cue in PersonalityCue if cue != PersonalityCue.NONE}
        self.presentation = PersonalityPresentation.DISABLED
        self.release_sleep = False
        self.sampled_us = 0
        self.last_frame = PersonalityFrame()
        self.images = {}
        directory = root / "assets/cody/personality-icons/generated"
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        # Palette provenance normalizes text line endings across Windows and Unix;
        # sprite coverage and indexed binary hashes remain byte exact below.
        palette_bytes = (root / manifest["palette_source"]).read_text(encoding="utf-8").encode("utf-8")
        if hashlib.sha256(palette_bytes).hexdigest() != manifest["palette_sha256"]:
            raise ValueError("Personality palette source hash mismatch")
        entries = json.loads(palette_bytes)["entries"]
        if [entry["index"] for entry in entries] != list(range(256)):
            raise ValueError("Personality palette slot order mismatch")
        # Expand exactly the firmware's RGB565 palette, retaining its quantization.
        palette565 = [int(entry["rgb565"], 16) for entry in entries]
        colors = [((value >> 11 & 31) * 255 // 31, (value >> 5 & 63) * 255 // 63,
                   (value & 31) * 255 // 31) for value in palette565]
        for asset in manifest["icons"]:
            cue = PersonalityCue(asset["identity"])
            if cue == PersonalityCue.NONE or cue in self.images:
                raise ValueError(f"Duplicate/empty personality identity: {cue}")
            indices = (directory / f'{asset["id"]}.indices.bin').read_bytes()
            coverage = (directory / f'{asset["id"]}.coverage.bin').read_bytes()
            if len(indices) != 4096 or len(coverage) != 4096:
                raise ValueError(f"Personality asset size mismatch: {cue.name}")
            if hashlib.sha256(indices).hexdigest() != asset["indices_sha256"] or \
                    hashlib.sha256(coverage).hexdigest() != asset["coverage_sha256"]:
                raise ValueError(f"Personality asset hash mismatch: {cue.name}")
            rgba = bytearray()
            for slot, alpha in zip(indices, coverage):
                if alpha and entries[slot]["reserved"]:
                    raise ValueError(f"Reserved palette slot in personality cue: {cue.name}")
                rgba.extend((*colors[slot], alpha))
            self.images[cue] = Image.frombytes("RGBA", (64, 64), bytes(rgba))
        if set(self.images) != set(PersonalityCue) - {PersonalityCue.NONE}:
            raise ValueError("Personality symbol catalog is incomplete")

    # self: Geometry history owner; cue phase, opacity and hold remain untouched.
    # target: Current finite authored center and bounded sprite dimensions.
    def prime_follow(self, target):
        state = self.lag
        state.x, state.y, state.width, state.height = target
        state.previous_target = target
        state.position_stable_us = 0
        state.scale_stable_us = 0
        count = self.config["history_bucket_count"]
        duration_us = self.config["history_bucket_us"]
        state.sample_index = count - 1
        state.samples = []
        for index in range(count):
            if index == state.sample_index:
                state.samples.append(PersonalityFollowSample())
            else:
                integrals = tuple(value * duration_us for value in target)
                age_us = (count - 1 - index) * duration_us
                state.samples.append(PersonalityFollowSample(integrals, duration_us, age_us))

    # self: Fixed-duration geometry history and resulting artistic transform.
    # target: Latest evaluated center/dimensions held over the active frame interval.
    # elapsed_us: Positive active interval; a whole-window gap primes directly.
    def advance_follow(self, target, elapsed_us):
        if elapsed_us >= self.config["scale_window_us"]:
            self.prime_follow(target)
            return
        state = self.lag
        same_position = target[:2] == state.previous_target[:2]
        same_scale = target[2:] == state.previous_target[2:]
        state.position_stable_us = min(state.position_stable_us + elapsed_us,
            self.config["scale_window_us"]) if same_position else 0
        state.scale_stable_us = min(state.scale_stable_us + elapsed_us,
            self.config["scale_window_us"]) if same_scale else 0
        state.previous_target = target
        maximum_age_us = self.config["scale_window_us"] + self.config["history_bucket_us"] + 1
        for sample in state.samples:
            sample.age_us = min(sample.age_us + elapsed_us, maximum_age_us)
        remaining_us = elapsed_us
        while remaining_us:
            sample = state.samples[state.sample_index]
            accepted_us = min(remaining_us, self.config["history_bucket_us"] - sample.duration_us)
            sample.integrals = tuple(integral + value * accepted_us
                                     for integral, value in zip(sample.integrals, target))
            sample.duration_us += accepted_us
            remaining_us -= accepted_us
            if sample.duration_us == self.config["history_bucket_us"]:
                state.sample_index = (state.sample_index + 1) % len(state.samples)
                state.samples[state.sample_index] = PersonalityFollowSample(age_us=remaining_us)
        position_integrals = [0., 0.]
        scale_integrals = [0., 0.]
        position_duration_us = 0
        scale_duration_us = 0
        for sample in state.samples:
            # Discard the crossing bucket entirely so old data cannot survive beyond 400 ms.
            if sample.age_us <= self.config["position_window_us"]:
                position_integrals[0] += sample.integrals[0]
                position_integrals[1] += sample.integrals[1]
                position_duration_us += sample.duration_us
            if sample.age_us <= self.config["scale_window_us"]:
                scale_integrals[0] += sample.integrals[2]
                scale_integrals[1] += sample.integrals[3]
                scale_duration_us += sample.duration_us
        state.x = position_integrals[0] / position_duration_us
        state.y = position_integrals[1] / position_duration_us
        state.width = scale_integrals[0] / scale_duration_us
        state.height = scale_integrals[1] / scale_duration_us
        if state.position_stable_us >= self.config["position_window_us"]:
            state.x, state.y = target[:2]
        if state.scale_stable_us >= self.config["scale_window_us"]:
            state.width, state.height = target[2:]
        lag_x, lag_y = state.x - target[0], state.y - target[1]
        distance_squared = lag_x * lag_x + lag_y * lag_y
        maximum_lag = self.config["maximum_position_lag_pixels"]
        if distance_squared > maximum_lag * maximum_lag:
            reduction = maximum_lag / math.sqrt(distance_squared)
            state.x = target[0] + lag_x * reduction
            state.y = target[1] + lag_y * reduction

    # self: Single-symbol controller whose pending/visible state needs active ticks.
    @property
    def active(self):
        return self.channel.desired != PersonalityCue.NONE or self.channel.phase != PersonalityPhase.HIDDEN

    # self: Controller clearing the displayed cue but retaining its latest desired target.
    def hide_current(self):
        self.channel.cue = PersonalityCue.NONE
        self.channel.phase = PersonalityPhase.HIDDEN
        self.channel.phase_elapsed_us = 0
        self.channel.opacity = 0
        self.channel.fade_out_opacity = 0

    # self: Controller preserving the current opacity when starting a smooth release.
    def begin_fade_out(self):
        self.channel.phase = PersonalityPhase.FADE_OUT
        self.channel.phase_elapsed_us = 0
        self.channel.fade_out_opacity = self.channel.opacity

    # self: Session repeat history whose clock survives presentation and geometry resets.
    # elapsed_ms: Cumulative active animation clock supplied by the host controller.
    # paused: Exclude the interval while rebasing its sampled endpoint.
    # Raises ValueError for backwards clocks, overflow or invalid repeat timestamps.
    def advance_active_clock(self, elapsed_ms, paused=False):
        if not math.isfinite(elapsed_ms) or elapsed_ms < 0:
            raise ValueError(f"Personality clock rejected: {elapsed_ms}")
        sampled_us = math.floor(elapsed_ms * 1000 + .5)
        delta_us = sampled_us - self.sampled_us
        if delta_us < 0:
            raise ValueError("Personality active clock moved backwards")
        if paused:
            delta_us = 0
        maximum_clock = (1 << 63) - 2
        if not 0 <= self.active_elapsed_us <= maximum_clock or delta_us > maximum_clock - self.active_elapsed_us:
            raise ValueError("Personality repeat clock overflow")
        if any(not 0 <= timestamp <= self.active_elapsed_us + 1 for timestamp in self.last_admitted_at_us.values()):
            raise ValueError("Personality repeat timestamp exceeds its active clock")
        self.active_elapsed_us += delta_us
        self.sampled_us = sampled_us
        return delta_us

    # self: Session repeat timestamps and per-cue authored policy.
    # cue: Typed identity considered for a new zero-opacity admission.
    def repeat_ready(self, cue):
        if cue == PersonalityCue.NONE:
            return True
        previous = self.last_admitted_at_us[cue]
        cooldown_us = self.cue_rules[cue]["repeat_cooldown_seconds"] * 1_000_000
        return previous == 0 or self.active_elapsed_us - (previous - 1) >= cooldown_us

    # self: Single desired target whose admission starts both fade and repeat cooldown.
    def begin_fade_in(self):
        state = self.channel
        state.cue = state.desired
        state.phase = PersonalityPhase.FADE_IN
        state.phase_elapsed_us = 0
        self.last_admitted_at_us[state.cue] = self.active_elapsed_us + 1

    # self: Single desired target controller; intermediate requests have no queue.
    # elapsed_us: Nonnegative active time, excluding pause and non-rig presentations.
    # release_immediately: Semantic Sleep exit bypasses hold, including mid-fade release.
    def advance_channel(self, elapsed_us, release_immediately):
        state = self.channel
        semantic_sleep = self.presentation == PersonalityPresentation.SLEEP and state.desired == PersonalityCue.SLEEP
        desired_rule = self.cue_rules.get(state.desired)
        current_rule = self.cue_rules.get(state.cue)
        interrupts = desired_rule is not None and desired_rule["interrupts"] and (
            current_rule is None or desired_rule["priority"] > current_rule["priority"])
        priority_replacement = semantic_sleep or interrupts
        ready = priority_replacement or state.desired_age_us >= self.config["stability_us"]
        # Urgent state bypasses dwell while preserving the current opacity and smooth release.
        if priority_replacement and state.cue != state.desired and state.phase not in (
                PersonalityPhase.HIDDEN, PersonalityPhase.FADE_OUT):
            if state.opacity == 0:
                self.hide_current()
            else:
                self.begin_fade_out()
                return
        if release_immediately and state.cue == PersonalityCue.SLEEP and state.phase not in (
                PersonalityPhase.HIDDEN, PersonalityPhase.FADE_OUT):
            self.begin_fade_out()
            return
        if state.phase == PersonalityPhase.HIDDEN:
            if state.desired != PersonalityCue.NONE and ready and self.repeat_ready(state.desired):
                self.begin_fade_in()
            return
        duration = self.config["fade_in_us"] if state.phase == PersonalityPhase.FADE_IN else (
            self.config["minimum_visible_us"] if state.phase == PersonalityPhase.VISIBLE else self.config["fade_out_us"])
        state.phase_elapsed_us += min(elapsed_us, duration - state.phase_elapsed_us)
        # A zero authored hold is valid; only fades consume normalized progress.
        progress = 0 if state.phase == PersonalityPhase.VISIBLE else state.phase_elapsed_us / duration
        smooth = progress * progress * (3 - 2 * progress)
        if state.phase == PersonalityPhase.FADE_IN:
            state.opacity = smooth
            if state.phase_elapsed_us == duration:
                state.phase = PersonalityPhase.VISIBLE
                state.phase_elapsed_us = 0
        elif state.phase == PersonalityPhase.VISIBLE:
            if state.desired != state.cue and ready and state.phase_elapsed_us == duration:
                self.begin_fade_out()
        else:
            state.opacity = state.fade_out_opacity * (1 - smooth)
            if state.phase_elapsed_us == duration:
                self.hide_current()
                if state.desired != PersonalityCue.NONE and ready and self.repeat_ready(state.desired):
                    self.begin_fade_in()

    # self: Painter whose non-main scene relinquishes all symbol presentation ownership.
    # elapsed_ms: Active host clock at the scene switch, used only to rebase following time.
    # paused: Preserve cooldown time when an explicit paused redraw clears presentation.
    # Raises ValueError for an invalid clock.
    def disable(self, elapsed_ms, paused=False):
        self.advance_active_clock(elapsed_ms, paused)
        self.channel = PersonalityChannel()
        self.lag = PersonalityLag()
        self.presentation = PersonalityPresentation.DISABLED
        self.release_sleep = False
        self.last_frame = PersonalityFrame()

    # self: Fixed host artistic filter and decoded symbol masters.
    # scene: Actual evaluated world anchors and body paths shared with the character.
    # cue: One explicit symbol preview; device cues are visibly synthetic host choices.
    # nominal_scale: Stable authored character scale, independent of response/breathing.
    # route: Existing semantic screen identity.
    # rotation: Shared cardinal orientation applied by the renderer after composition.
    # elapsed_ms: Active animation clock; resizing/redraw does not advance it.
    # exclusions: Half-open permanently reserved route/chrome rectangles.
    # body_top: Actual jelly support top, or None for the evaluated vector contour.
    # presentation: Explicit scene permission and semantic Sleep preset identity.
    # paused: Freeze follow, fade, hold and debounce while accepting latest desired cue.
    # Raises ValueError for invalid clocks, geometry or configuration, never layout admission.
    def prepare(self, scene, cue, nominal_scale, route, rotation, elapsed_ms, exclusions, body_top=None,
                *, presentation=PersonalityPresentation.DISABLED, paused=False):
        cue = PersonalityCue(cue)
        presentation = PersonalityPresentation(presentation)
        size = size_for_scale(nominal_scale)
        if not math.isfinite(elapsed_ms) or elapsed_ms < 0 or rotation not in (0, 90, 180, 270):
            raise ValueError(f"Personality clock/orientation rejected: {elapsed_ms}, {rotation}")
        if not math.isfinite(scene["scale"]) or scene["scale"] <= 0 or len(exclusions) > 6:
            raise ValueError("Personality scene scale/exclusions rejected")
        if presentation == PersonalityPresentation.DISABLED or size == CodySize.MICRO:
            self.disable(elapsed_ms, paused)
            return self.last_frame
        delta_us = self.advance_active_clock(elapsed_ms, paused)
        if presentation == PersonalityPresentation.SLEEP:
            cue = PersonalityCue.SLEEP
            self.release_sleep = False
        elif self.presentation == PersonalityPresentation.SLEEP:
            self.release_sleep = True
        self.presentation = presentation
        state = self.channel
        if state.desired != cue:
            state.desired = cue
            state.desired_age_us = 0
        else:
            state.desired_age_us += min(delta_us, self.config["stability_us"] - state.desired_age_us)
        nose = scene["world"]["nose"]
        upper_left = scene["world"]["lobe.upper_left"]
        upper_right = scene["world"]["lobe.upper_right"]
        if not all(math.isfinite(value) for point in (nose, upper_left, upper_right) for value in point):
            raise ValueError("Personality nonfinite face/body anchor rejected")
        if body_top is None:
            body = next(path for path in scene["paths"]
                        if path["definition"]["name"] == self.rig["contour"]["path"])
            if not body["visible"] or not body["vertices"] or not all(
                    math.isfinite(point[1]) for point in body["vertices"]):
                raise ValueError("Personality visible body geometry rejected")
            body_top = min(point[1] for point in body["vertices"])
        if not math.isfinite(body_top):
            raise ValueError("Personality body bounds rejected")
        definitions = {point["name"]: point for point in self.rig["points"]}
        rest_span = (definitions["lobe.upper_right"]["position"][0] -
                     definitions["lobe.upper_left"]["position"][0]) * scene["scale"]
        if not math.isfinite(rest_span) or rest_span <= 0:
            raise ValueError("Personality rest lobe span rejected")
        profile = self.profiles[
            "sleep" if presentation == PersonalityPresentation.SLEEP else "main"]
        extent = max(self.config["minimum_extent"], min(self.config["maximum_base_extent"],
                                                       nominal_scale * profile["extent_per_scale"]))
        limit = extent * self.config["maximum_deformation_pixels"] / self.config["maximum_base_extent"]
        deformation = max(-limit, min(limit, ((upper_right[0] - upper_left[0]) / rest_span - 1) * extent))
        # Match native fixed-pixel translation and scaling around the same center.
        target = (nose[0] + profile["offset_x"] * scene["scale"] + profile["pixel_offset_x"],
                  nose[1] + profile["offset_y"] * scene["scale"] + profile["pixel_offset_y"],
                  (extent + deformation) * profile["size_multiplier"],
                  (extent - deformation) * profile["size_multiplier"])
        key = (size, route, rotation)
        if self.lag.key is None or (not paused and self.lag.key != key):
            self.prime_follow(target)
        elif delta_us > 0:
            self.advance_follow(target, delta_us)
        self.lag.key = key
        values = (self.lag.x, self.lag.y, self.lag.width, self.lag.height)
        if not all(math.isfinite(value) for value in values):
            raise ValueError("Personality filtered geometry rejected")
        x, y, width, height = values
        width, height = math.floor(width + .5), math.floor(height + .5)
        if not 1 <= width <= 231 or not 1 <= height <= 231:
            raise ValueError(f"Personality filtered extent rejected: {width} x {height}")
        self.last_frame = PersonalityFrame()
        if state.desired == PersonalityCue.NONE and state.phase == PersonalityPhase.HIDDEN:
            return self.last_frame
        float_left, float_top = x - width / 2, y - height / 2
        # C++ std::round rounds halves away from zero, including rejected negative bounds.
        left = math.floor(float_left + .5) if float_left >= 0 else math.ceil(float_left - .5)
        top = math.floor(float_top + .5) if float_top >= 0 else math.ceil(float_top - .5)
        admitted = 0 <= left and 0 <= top and left + width <= 466 and top + height <= 466
        if not profile["allow_body_overlap"]:
            admitted = admitted and top + height + self.config["body_clearance_pixels"] <= body_top
        if admitted:
            farthest_y = max(abs(top + .5 - 233), abs(top + height - .5 - 233))
            chord = math.sqrt(233 ** 2 - farthest_y ** 2)
            admitted = left + .5 >= 233 - chord and left + width - .5 <= 233 + chord
        if admitted:
            admitted = not any(left < right and lower_left < left + width and top < bottom and
                               upper_top < top + height and lower_left < right and upper_top < bottom
                               for lower_left, upper_top, right, bottom in exclusions)
        if not admitted:
            self.hide_current()
            self.last_frame = PersonalityFrame(cue=state.desired,
                status=PersonalityPresentationStatus.BLOCKED_BY_LAYOUT)
            return self.last_frame
        if not paused:
            self.advance_channel(delta_us, self.release_sleep)
            if state.cue != PersonalityCue.SLEEP or state.phase in (PersonalityPhase.HIDDEN, PersonalityPhase.FADE_OUT):
                self.release_sleep = False
        opacity = math.floor(state.opacity * 255 + .5)
        if opacity:
            self.last_frame = PersonalityFrame(state.cue, left, top, width, height, opacity,
                                               PersonalityPresentationStatus.VISIBLE)
        elif state.phase == PersonalityPhase.HIDDEN and state.desired != PersonalityCue.NONE and not self.repeat_ready(state.desired):
            self.last_frame = PersonalityFrame(cue=state.desired,
                status=PersonalityPresentationStatus.SUPPRESSED_BY_COOLDOWN)
        return self.last_frame

    # self: Immutable decoded fixed-palette symbol masters.
    # image: Host-only logical RGB scene receiving the single prepared cue.
    # frame: Prepared visible sprite or explicit Hidden/BlockedByLayout outcome.
    def paint(self, image, frame):
        if frame.status != PersonalityPresentationStatus.VISIBLE or frame.opacity == 0:
            return
        # Match native integer nearest sampling rather than Pillow's center-based resize.
        master = self.images[frame.cue]
        pixels = master.load()
        rgba = bytearray()
        for y in range(frame.height):
            for x in range(frame.width):
                red, green, blue, coverage = pixels[x * 64 // frame.width, y * 64 // frame.height]
                alpha = (coverage * frame.opacity + 127) // 255
                rgba.extend((red, green, blue, alpha))
        sprite = Image.frombytes("RGBA", (frame.width, frame.height), bytes(rgba))
        image.paste(sprite, (frame.left, frame.top), sprite)
