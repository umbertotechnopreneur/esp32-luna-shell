"""Named semantics shared by skin authoring, animation and the desktop preview."""

from enum import Enum, IntEnum


class FaceChannel(IntEnum):
    """Storage order is internal; animation accesses channels through these names."""

    EYE_LEFT = 0
    EYE_RIGHT = 1
    BROW_LEFT = 2
    BROW_RIGHT = 3
    MOUTH_OPEN = 4
    BREATH = 5
    MOUTH_WIDTH = 6
    NOSE_SIZE = 7
    CHEEK_SIZE = 8
    CHEEK_OPACITY = 9
    GAZE_X = 10
    GAZE_Y = 11
    EYE_WIDTH_LEFT = 12
    EYE_WIDTH_RIGHT = 13
    EYE_CURVE_LEFT = 14
    EYE_CURVE_RIGHT = 15
    EYE_UPPER_LEFT = 16
    EYE_UPPER_RIGHT = 17
    EYE_LOWER_LEFT = 18
    EYE_LOWER_RIGHT = 19
    BROW_HEIGHT_LEFT = 20
    BROW_HEIGHT_RIGHT = 21
    BROW_CURVE_LEFT = 22
    BROW_CURVE_RIGHT = 23
    MOUTH_CORNER_LEFT = 24
    MOUTH_CORNER_RIGHT = 25
    MOUTH_CURVE = 26
    MOUTH_X = 27
    MOUTH_Y = 28
    NOSE_X = 29
    NOSE_Y = 30
    CHEEK_RAISE_LEFT = 31
    CHEEK_RAISE_RIGHT = 32
    TONGUE_OPACITY = 33


class ContourControl(str, Enum):
    FULLNESS = "fullness"
    AMPLITUDE = "amplitude"
    SPEED = "speed"
    PHASE_SPREAD = "phase_spread"


class PointRole(str, Enum):
    ANCHOR = "anchor"
    KNOT = "knot"
    HANDLE = "handle"


class FillAntialiasing(str, Enum):
    DISABLED = "disabled"
    COVERAGE = "coverage"


class CurvePoint(IntEnum):
    START = 0
    OUTGOING_HANDLE = 1
    INCOMING_HANDLE = 2
    END = 3


class Screen(str, Enum):
    RIG = "rig"
    GREETING = "greeting"
    BOOT_SLEEP = "boot-sleep"
    BOOT_WAKING = "boot-waking"
    BOOT_READY = "boot-ready"
    BOOT_VIBEWARE = "boot-vibeware"


class LabelScope(str, Enum):
    ANCHORS = "Anchors"
    CONTOUR = "Contour"
    ENTITY = "Selected entity"
    POINT = "Selected point"
    ALL = "All points"


# channel: Stable channel enum, independent of a skin's YAML declaration order.
def channel_name(channel):
    return channel.name.lower()


# name: Authored channel name to resolve at the storage boundary.
# Raises ValueError when the channel is not supported by the facial engine.
def face_channel(name):
    try:
        return FaceChannel[name.upper()]
    except (AttributeError, KeyError) as error:
        raise ValueError(f"Unknown facial channel: {name!r}") from error
