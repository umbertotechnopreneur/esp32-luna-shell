"""Shared named Debug navigation geometry and active-touch visibility policy."""

import math
from pathlib import Path
from scripts.common.cody_rig_contract import read_json

CONTROL_IDENTITIES = {
    "back": (1, "Back"),
    "overlay": (2, "Overlay"),
    "pause": (3, "Pause"),
    "screenshot": (4, "Screenshot"),
}


# root: Checkout containing the canonical navigation source, not runtime observations.
# Raises ValueError for ambiguous controls, unsupported geometry or invalid timing.
def load_navigation(root):
    document = read_json(Path(root) / "assets/cody/navigation.json")
    if set(document) != {"schema", "auto_hide_ms", "fade_ms", "colors_rgb565", "controls"} or document["schema"] != "cody.navigation.v1":
        raise ValueError("Invalid Cody navigation schema")
    colors = document["colors_rgb565"]
    if not isinstance(colors, dict) or set(colors) != {"top", "bottom", "ink", "selected"}:
        raise ValueError("Navigation requires explicit neutral material colors")
    if any(type(value) is not int or not 0 <= value <= 65535 for value in colors.values()):
        raise ValueError("Invalid navigation RGB565 color")
    for key in ("auto_hide_ms", "fade_ms"):
        if type(document[key]) is not int or not 1 <= document[key] <= 60000:
            raise ValueError(f"Invalid navigation timing: {key}")
    seen = set()
    for control in document["controls"]:
        identity = control.get("id")
        if (identity not in CONTROL_IDENTITIES or identity in seen or type(control.get("identity")) is not int
                or control["identity"] != CONTROL_IDENTITIES[identity][0]):
            raise ValueError(f"Duplicate or unknown navigation identity: {identity}")
        seen.add(identity)
        if type(control.get("visible_by_default")) is not bool:
            raise ValueError(f"Navigation visibility must be explicit: {identity}")
        common = {"id", "identity", "visible_by_default", "shape"}
        if control["shape"] == "circle":
            if set(control) != common | {"center", "radius"}:
                raise ValueError(f"Invalid circle fields: {identity}")
            if (not isinstance(control["center"], list) or len(control["center"]) != 2
                    or any(type(value) is not int for value in control["center"])
                    or type(control["radius"]) is not int or not 1 <= control["radius"] <= 30):
                raise ValueError(f"Invalid circular target: {identity}")
            if identity == "back" and (control["center"] != [40, 233] or control["radius"] != 30):
                raise ValueError("Back must retain its canonical circular target")
            center_x, center_y = control["center"]
            radius = control["radius"]
            if math.hypot(center_x-233, center_y-233) + radius > 223:
                raise ValueError(f"Circular target leaves the inset display rim: {identity}")
            if identity != "back" and center_y + radius > 73:
                raise ValueError(f"Debug target must remain above the safe square: {identity}")
            control.update(center=(center_x, center_y), icon_center=(center_x, center_y),
                inner_radius=0, outer_radius=radius, side_sign=0, slope_min=0, slope_max=0,
                bounds=(center_x-radius, center_y-radius, center_x+radius, center_y+radius))
        elif control["shape"] == "top-arc":
            if set(control) != common | {"side", "inner_radius", "outer_radius", "start_degrees", "end_degrees"}:
                raise ValueError(f"Invalid arc fields: {identity}")
            for key in ("inner_radius", "outer_radius", "start_degrees", "end_degrees"):
                value = control[key]
                if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
                    raise ValueError(f"Invalid arc value: {identity}.{key}")
            inner, outer = control["inner_radius"], control["outer_radius"]
            start, end = control["start_degrees"], control["end_degrees"]
            if control["side"] not in ("left", "right") or not 0 < inner < outer <= 223 or not 0 < start < end < 45:
                raise ValueError(f"Arc leaves the reviewed upper rim: {identity}")
            side = -1 if control["side"] == "left" else 1
            near_x = inner * math.sin(math.radians(start))
            far_x = outer * math.sin(math.radians(end))
            left, right = sorted((233 + side * near_x, 233 + side * far_x))
            top = 233 - outer * math.cos(math.radians(start))
            bottom = 233 - inner * math.cos(math.radians(end))
            middle_angle = math.radians((start + end) / 2)
            middle_radius = (inner + outer) / 2
            control.update(center=(233, 233), side_sign=side,
                slope_min=math.tan(math.radians(start)), slope_max=math.tan(math.radians(end)),
                icon_center=(233 + side * middle_radius * math.sin(middle_angle), 233 - middle_radius * math.cos(middle_angle)),
                bounds=(math.floor(left), math.floor(top), math.ceil(right), math.ceil(bottom)))
        else:
            raise ValueError(f"Unsupported navigation shape: {identity}")
    if seen != set(CONTROL_IDENTITIES):
        raise ValueError("Navigation requires all named control identities")
    return document


# control: One fully validated named geometry record.
# x: Logical horizontal pixel coordinate, shared with rendering.
# y: Logical vertical pixel coordinate, shared with rendering.
def control_contains(control, x, y):
    if not control["visible_by_default"]:
        return False
    center_x, center_y = control["center"]
    dx, dy = x-center_x, y-center_y
    squared = dx*dx + dy*dy
    if control["shape"] == "circle":
        return squared <= control["outer_radius"]**2
    along = dx * control["side_sign"]
    return (dy < 0 and control["inner_radius"]**2 <= squared <= control["outer_radius"]**2
            and -dy*control["slope_min"] <= along <= -dy*control["slope_max"])


class NavigationVisibility:
    # configuration: Validated authored fade and inactivity durations.
    def __init__(self, configuration):
        self.fade_us = configuration["fade_ms"] * 1000
        self.hide_us = configuration["auto_hide_ms"] * 1000
        self.primed = False
        self.last_touch_us = 0
        self.fade_started_us = 0
        self.start_opacity = 0
        self.target_opacity = 255

    # now_us: Monotonic presentation timestamp, independent of paused animation.
    def opacity(self, now_us):
        if not self.primed:
            return 0
        elapsed = min(max(now_us-self.fade_started_us, 0), self.fade_us)
        return (self.start_opacity*(self.fade_us-elapsed) + self.target_opacity*elapsed + self.fade_us//2)//self.fade_us

    # now_us: Successful touch report; startup alone never reveals the toolbar.
    def interact(self, now_us):
        current = self.opacity(now_us)
        if not self.primed or self.target_opacity != 255:
            self.start_opacity = current
            self.target_opacity = 255
            self.fade_started_us = now_us
        self.primed = True
        self.last_touch_us = now_us

    # now_us: Monotonic wall clock, including time while the character is paused.
    def update(self, now_us):
        if self.primed and self.target_opacity != 0 and now_us-self.last_touch_us >= self.hide_us:
            deadline = self.last_touch_us + self.hide_us
            self.start_opacity = self.opacity(deadline)
            self.fade_started_us = deadline
            self.target_opacity = 0
