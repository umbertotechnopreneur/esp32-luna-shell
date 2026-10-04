"""Host-only preview of the native single indexed-alpha personality cue."""

from dataclasses import dataclass
from enum import IntEnum
import hashlib
import json
import math

from PIL import Image


class PersonalityCue(IntEnum):
    NONE = 0
    SLEEP = 1
    FEAR = 2
    JOY = 3
    HUNGER = 4
    THIRST = 5
    HEAT = 6
    COLD = 7
    LOW_BATTERY = 8
    CHARGING = 9
    NO_WIFI = 10
    CURIOSITY = 11
    CONFUSION = 12
    AFFECTION = 13
    FRUSTRATION = 14


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
class PersonalityLag:
    key: tuple | None = None
    x: float = 0
    y: float = 0
    width: float = 0
    height: float = 0
    sampled_ms: float = 0


class PersonalityPainter:
    # self: Host-only image owner; decoded resources never enter firmware snapshots.
    # root: Repository root containing generated fixed-palette indices and coverage.
    # rig: Loaded rig with canonical named lobe anchors.
    # Raises OSError for unreadable assets and ValueError for mismatched identities/hashes.
    def __init__(self, root, rig):
        self.rig = rig
        self.lag = PersonalityLag()
        self.images = {}
        directory = root / "assets/cody/personality-icons/generated"
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
        palette_bytes = (root / manifest["palette_source"]).read_bytes()
        if hashlib.sha256(palette_bytes).hexdigest() != manifest["palette_sha256"]:
            raise ValueError("Personality palette source hash mismatch")
        entries = json.loads(palette_bytes)["entries"]
        if [entry["index"] for entry in entries] != list(range(256)):
            raise ValueError("Personality palette slot order mismatch")
        colors = [tuple(bytes.fromhex(entry["rgb888"].removeprefix("#"))) for entry in entries]
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

    # self: Fixed host artistic filter and decoded symbol masters.
    # scene: Actual evaluated world anchors and body paths shared with the character.
    # cue: One explicit symbol preview; device cues are visibly synthetic host choices.
    # nominal_scale: Stable authored character scale, independent of response/breathing.
    # route: Existing semantic screen identity.
    # rotation: Shared cardinal orientation applied by the renderer after composition.
    # elapsed_ms: Active animation clock; resizing/redraw does not advance it.
    # exclusions: Half-open route/chrome rectangles to preserve.
    # body_top: Actual jelly support top, or None for the evaluated vector contour.
    # Raises ValueError for invalid clocks/size or impossible circle-aware placement.
    def prepare(self, scene, cue, nominal_scale, route, rotation, elapsed_ms, exclusions, body_top=None):
        cue = PersonalityCue(cue)
        size = size_for_scale(nominal_scale)
        if not math.isfinite(elapsed_ms) or elapsed_ms < 0:
            raise ValueError(f"Personality clock rejected: {elapsed_ms}")
        if cue == PersonalityCue.NONE or size == CodySize.MICRO:
            self.lag = PersonalityLag()
            return None
        crown = scene["world"]["lobe.crown"]
        upper_left = scene["world"]["lobe.upper_left"]
        upper_right = scene["world"]["lobe.upper_right"]
        if body_top is None:
            body = next(path for path in scene["paths"]
                        if path["definition"]["name"] == self.rig["contour"]["path"])
            body_top = min(point[1] for point in body["vertices"])
        definitions = {point["name"]: point for point in self.rig["points"]}
        rest_span = (definitions["lobe.upper_right"]["position"][0] -
                     definitions["lobe.upper_left"]["position"][0]) * scene["scale"]
        squash = max(.9, min(1.1, (upper_right[0] - upper_left[0]) / rest_span))
        extent = max(12, min(58, scene["scale"] * .42))
        width, height = extent * squash, extent / squash
        target = (crown[0], body_top - 4 - height / 2, width, height)
        key = (cue, size, route, rotation)
        previous = self.lag
        delta = elapsed_ms - previous.sampled_ms
        if previous.key != key or delta < 0 or delta > 250:
            values = target
        else:
            blend = -math.expm1(-delta / 85)
            values = tuple(old + (wanted - old) * blend for old, wanted in
                           zip((previous.x, previous.y, previous.width, previous.height), target))
        next_lag = PersonalityLag(key, *values, elapsed_ms)
        x, y, width, height = values
        filtered_bottom = y + height / 2
        width, height = [max(12, min(64, math.floor(value + .5))) for value in (width, height)]
        clearance_top = 0
        for left, top, right, bottom in exclusions:
            if left < right and top < bottom and x - width / 2 < right and x + width / 2 > left and bottom < body_top - 4:
                available = body_top - 4 - bottom
                if available >= 12:
                    clearance_top = max(clearance_top, bottom)
                    if available < height:
                        width = max(12, math.floor(width * available / height))
                        height = math.floor(available)
        wanted_top = max(clearance_top, min(filtered_bottom - height, body_top - height - 4))
        for step in range(8):
            top = math.floor(wanted_top - step * (height / 2 + 2) + .5)
            for shift in (0, -width, width):
                left = math.floor(x + shift - width / 2 + .5)
                if top < 0 or left < 0 or top + height > 466 or left + width > 466:
                    continue
                farthest_y = max(abs(top + .5 - 233), abs(top + height - .5 - 233))
                chord = math.sqrt(233 ** 2 - farthest_y ** 2)
                chords_fit = left + .5 >= 233 - chord and left + width - .5 <= 233 + chord
                overlaps = any(left < right and lower_left < left + width and top < bottom and
                               upper_top < top + height and lower_left < right and upper_top < bottom
                               for lower_left, upper_top, right, bottom in exclusions)
                if chords_fit and not overlaps:
                    self.lag = next_lag
                    return (cue, left, top, width, height)
        raise ValueError(f"Personality placement unavailable: cue={cue.name} size={size.name} route={route}")

    # self: Immutable decoded fixed-palette symbol masters.
    # image: Host-only logical RGB scene receiving the single prepared cue.
    # frame: Prepared tuple, or None for intentionally suppressed Micro/None.
    def paint(self, image, frame):
        if frame is None:
            return
        cue, left, top, width, height = frame
        # Match native integer nearest sampling rather than Pillow's center-based resize.
        master = self.images[cue]
        pixels = master.load()
        rgba = bytearray()
        for y in range(height):
            for x in range(width):
                rgba.extend(pixels[x * 64 // width, y * 64 // height])
        sprite = Image.frombytes("RGBA", (width, height), bytes(rgba))
        image.paste(sprite, (left, top), sprite)
