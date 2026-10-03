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
