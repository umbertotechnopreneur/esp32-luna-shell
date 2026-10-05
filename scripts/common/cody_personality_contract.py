"""Strict build-time personality authoring, including dormant work-session wellbeing rules."""

import math
from pathlib import Path
from urllib.parse import urlsplit
import yaml

from scripts.common.cody_rig_contract import RigLoader

CATALOG = {
    "events": {
        1: "caress", 2: "work_session_start", 3: "work_session_pause", 4: "work_session_resume",
        5: "work_session_end", 6: "reminder_acknowledge", 7: "reminder_snooze",
    },
    "zones": {1: "above_eyes", 2: "below_mouth"},
    "metrics": {1: "happiness", 2: "sadness", 3: "fatigue_minutes"},
    "reminders": {1: "screen_break", 2: "movement_break", 3: "eye_rest", 4: "drink_water"},
    "icons": {
        1: "sleep", 2: "fear", 3: "joy", 4: "hunger", 5: "thirst", 6: "heat", 7: "cold",
        8: "low_battery", 9: "charging", 10: "no_wifi", 11: "curiosity", 12: "confusion",
        13: "affection", 14: "frustration",
    },
}


# mapping: Authored section whose field set must be exact.
# expected: Supported named fields, independent of YAML order.
# label: Human-readable schema location.
# Raises ValueError for missing, unknown or incorrectly typed sections.
def fields(mapping, expected, label):
    if not isinstance(mapping, dict) or set(mapping) != set(expected):
        raise ValueError(f"{label}: requires exactly {sorted(expected)}")


# value: Authored integer, never silently corrected.
# minimum: Inclusive supported lower bound.
# maximum: Inclusive supported upper bound.
# label: Named authoring field used in errors.
# Raises ValueError for booleans, nonintegers or unsupported values.
def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{label}: expected integer {minimum}..{maximum}, got {value!r}")
    return value


# value: Authored finite numeric value, without Boolean coercion.
# minimum: Inclusive supported lower bound.
# maximum: Inclusive supported upper bound.
# label: Exact authoring location included in a rejection.
# Raises ValueError for nonnumeric, nonfinite or unsupported values.
def number(value, minimum, maximum, label):
    if type(value) not in (int, float) or not minimum <= value <= maximum:
        raise ValueError(f"{label}: expected finite number {minimum}..{maximum}, got {value!r}")
    try:
        finite = math.isfinite(value)
    except OverflowError as error:
        raise ValueError(f"{label}: numeric value is outside the finite authoring range") from error
    if not finite:
        raise ValueError(f"{label}: expected a finite numeric value, got {value!r}")
    return value


# value: Authored enable/interrupt policy, never inferred from numbers or strings.
# label: Exact authoring location included in a rejection.
# Raises ValueError unless the YAML value is an actual Boolean.
def boolean(value, label):
    if type(value) is not bool:
        raise ValueError(f"{label}: expected Boolean, got {value!r}")
    return value


# catalog: Complete explicitly named event, zone, metric and icon collections.
# Raises ValueError for missing, duplicate, reassigned or unknown identities and malformed policies.
def validate_catalog(catalog):
    fields(catalog, CATALOG, "catalog")
    for kind, identities in CATALOG.items():
        entries = catalog[kind]
        if not isinstance(entries, list) or len(entries) != len(identities):
            raise ValueError(f"catalog.{kind}: expected exactly {len(identities)} identity entries")
        actual = {}
        names = set()
        expected_fields = ("id", "name", "priority", "interrupts", "repeat_cooldown_seconds", "sfx_enabled") \
            if kind == "icons" else ("id", "name")
        for index, entry in enumerate(entries):
            label = f"catalog.{kind}[{index}]"
            fields(entry, expected_fields, label)
            identity = integer(entry["id"], 1, 255, f"{label}.id")
            name = entry["name"]
            if not isinstance(name, str) or identities.get(identity) != name:
                raise ValueError(f"{label}: unknown or reassigned identity ({identity!r}, {name!r})")
            if identity in actual or name in names:
                raise ValueError(f"{label}: duplicate identity ({identity}, {name})")
            actual[identity] = name
            names.add(name)
            if kind == "icons":
                integer(entry["priority"], 0, 1000, f"{label}.priority")
                boolean(entry["interrupts"], f"{label}.interrupts")
                integer(entry["repeat_cooldown_seconds"], 0, 3600, f"{label}.repeat_cooldown_seconds")
                boolean(entry["sfx_enabled"], f"{label}.sfx_enabled")
        if actual != identities:
            raise ValueError(f"catalog.{kind}: identity/name bijection is incomplete")


# caress: Exact gesture authoring fields referring to the fixed event and anatomical zones.
# Raises ValueError for incorrect references, invalid intervals, or nonpositive gesture thresholds.
def validate_caress(caress):
    fields(caress, ("event", "zones", "minimum_contact_ms", "maximum_contact_ms", "maximum_sample_gap_ms",
        "cooldown_seconds", "affection_seconds", "upper_margin_per_scale", "lower_margin_per_scale",
        "minimum_path_per_scale", "minimum_displacement_per_scale", "maximum_speed_per_scale", "jitter_pixels"),
        "caress")
    if caress["event"] != "caress":
        raise ValueError("caress.event: requires the named caress event")
    zones = caress["zones"]
    if not isinstance(zones, list) or len(zones) != 2 or not all(isinstance(zone, str) for zone in zones) or \
            set(zones) != set(CATALOG["zones"].values()):
        raise ValueError("caress.zones: requires above_eyes and below_mouth exactly once")
    minimum = integer(caress["minimum_contact_ms"], 1, 10000, "caress.minimum_contact_ms")
    maximum = integer(caress["maximum_contact_ms"], 1, 10000, "caress.maximum_contact_ms")
    if minimum > maximum:
        raise ValueError("caress: minimum_contact_ms cannot exceed maximum_contact_ms")
    integer(caress["maximum_sample_gap_ms"], 1, 1000, "caress.maximum_sample_gap_ms")
    integer(caress["cooldown_seconds"], 0, 3600, "caress.cooldown_seconds")
    integer(caress["affection_seconds"], 1, 60, "caress.affection_seconds")
    for name in ("upper_margin_per_scale", "lower_margin_per_scale"):
        number(caress[name], 0, 1, f"caress.{name}")
    for name in ("minimum_path_per_scale", "minimum_displacement_per_scale"):
        number(caress[name], .001, 5, f"caress.{name}")
    number(caress["maximum_speed_per_scale"], .001, 20, "caress.maximum_speed_per_scale")
    number(caress["jitter_pixels"], 0, 10, "caress.jitter_pixels")


# icons: Exact visual timing, finite following, face anchors and truthful low-battery thresholds.
# Raises ValueError for unsupported resource bounds or contradictory visual authoring settings.
def validate_icons(icons):
    fields(icons, ("channel", "follow", "anchors", "low_battery"), "icons")
    channel = icons["channel"]
    fields(channel, ("stability_ms", "fade_in_ms", "minimum_visible_ms", "fade_out_ms", "repeat_clock"), "icons.channel")
    integer(channel["stability_ms"], 0, 5000, "icons.channel.stability_ms")
    integer(channel["fade_in_ms"], 1, 10000, "icons.channel.fade_in_ms")
    integer(channel["minimum_visible_ms"], 0, 60000, "icons.channel.minimum_visible_ms")
    integer(channel["fade_out_ms"], 1, 10000, "icons.channel.fade_out_ms")
    if channel["repeat_clock"] != "active":
        raise ValueError("icons.channel.repeat_clock: requires active animation time")
    follow = icons["follow"]
    fields(follow, ("position_window_ms", "scale_window_ms", "history_bucket_ms", "maximum_position_lag_pixels",
        "minimum_extent", "maximum_base_extent", "maximum_deformation_pixels", "body_clearance_pixels"), "icons.follow")
    position = integer(follow["position_window_ms"], 1, 400, "icons.follow.position_window_ms")
    scale = integer(follow["scale_window_ms"], 1, 400, "icons.follow.scale_window_ms")
    bucket = integer(follow["history_bucket_ms"], 1, 400, "icons.follow.history_bucket_ms")
    if not bucket <= position <= scale or scale % bucket != 0 or scale // bucket + 1 > 17:
        raise ValueError("icons.follow: requires bucket <= position <= scale, exact scale buckets and at most17 buckets")
    number(follow["maximum_position_lag_pixels"], 0, 4, "icons.follow.maximum_position_lag_pixels")
    minimum = number(follow["minimum_extent"], 1, 58, "icons.follow.minimum_extent")
    maximum = number(follow["maximum_base_extent"], 1, 58, "icons.follow.maximum_base_extent")
    if minimum > maximum:
        raise ValueError("icons.follow: minimum_extent cannot exceed maximum_base_extent")
    deformation = number(follow["maximum_deformation_pixels"], 0, 2, "icons.follow.maximum_deformation_pixels")
    # The native follow state requires both dimensions to remain at least one pixel.
    # Reject authoring combinations that would violate that bound at maximum squash.
    if minimum * (1 - deformation / maximum) < 1:
        raise ValueError("icons.follow: minimum extent and maximum deformation allow dimensions below one pixel")
    number(follow["body_clearance_pixels"], 0, 20, "icons.follow.body_clearance_pixels")
    anchors = icons["anchors"]
    fields(anchors, ("main", "sleep"), "icons.anchors")
    for name, profile in anchors.items():
        fields(profile, ("offset_x", "offset_y", "extent_per_scale", "pixel_offset_x", "pixel_offset_y",
                         "size_multiplier", "allow_body_overlap"), f"icons.anchors.{name}")
        number(profile["offset_x"], -4, 4, f"icons.anchors.{name}.offset_x")
        number(profile["offset_y"], -4, 4, f"icons.anchors.{name}.offset_y")
        number(profile["extent_per_scale"], .001, 1, f"icons.anchors.{name}.extent_per_scale")
        number(profile["pixel_offset_x"], -466, 466, f"icons.anchors.{name}.pixel_offset_x")
        number(profile["pixel_offset_y"], -466, 466, f"icons.anchors.{name}.pixel_offset_y")
        number(profile["size_multiplier"], 1, 3, f"icons.anchors.{name}.size_multiplier")
        boolean(profile["allow_body_overlap"], f"icons.anchors.{name}.allow_body_overlap")
    battery = icons["low_battery"]
    fields(battery, ("enter_percent", "exit_percent"), "icons.low_battery")
    enter = integer(battery["enter_percent"], 0, 100, "icons.low_battery.enter_percent")
    exit_threshold = integer(battery["exit_percent"], 0, 100, "icons.low_battery.exit_percent")
    if enter >= exit_threshold:
        raise ValueError("icons.low_battery: enter_percent must be less than exit_percent")


# wellbeing: Explicit-session timing policy and four fixed reminder identities; presentation stays disabled.
# Raises ValueError for missing/unknown rules, unsupported timing, incorrect references or malformed source metadata.
def validate_wellbeing(wellbeing):
    fields(wellbeing, ("enabled", "clock", "fatigue", "delivery", "reminders"), "wellbeing")
    boolean(wellbeing["enabled"], "wellbeing.enabled")
    if wellbeing["clock"] != "active_work_session":
        raise ValueError("wellbeing.clock: requires explicit active_work_session time")
    fatigue = wellbeing["fatigue"]
    fields(fatigue, ("metric", "unit", "reset_on"), "wellbeing.fatigue")
    if fatigue["metric"] != "fatigue_minutes" or fatigue["unit"] != "minutes" or \
            fatigue["reset_on"] != "acknowledged_screen_break":
        raise ValueError("wellbeing.fatigue: requires fatigue_minutes in minutes, reset by acknowledged_screen_break")
    delivery = wellbeing["delivery"]
    fields(delivery, ("enabled", "combine_due", "repeat_until_acknowledged", "snooze_minutes"), "wellbeing.delivery")
    enabled = boolean(delivery["enabled"], "wellbeing.delivery.enabled")
    combine = boolean(delivery["combine_due"], "wellbeing.delivery.combine_due")
    repeat = boolean(delivery["repeat_until_acknowledged"], "wellbeing.delivery.repeat_until_acknowledged")
    if enabled or not combine or repeat:
        raise ValueError("wellbeing.delivery: MVP requires disabled presentation, combined due state and no repeated delivery")
    integer(delivery["snooze_minutes"], 1, 1440, "wellbeing.delivery.snooze_minutes")
    reminders = wellbeing["reminders"]
    expected_names = set(CATALOG["reminders"].values())
    if not isinstance(reminders, list) or len(reminders) != len(expected_names):
        raise ValueError("wellbeing.reminders: requires all four permanent reminder identities")
    seen = set()
    cue_names = set(CATALOG["icons"].values())
    expected_fields = ("reminder", "enabled", "every_minutes", "duration_seconds", "distance_feet",
        "cue", "priority", "interval_basis", "source")
    for index, reminder in enumerate(reminders):
        label = f"wellbeing.reminders[{index}]"
        fields(reminder, expected_fields, label)
        identity = reminder["reminder"]
        if not isinstance(identity, str) or identity not in expected_names or identity in seen:
            raise ValueError(f"{label}.reminder: unknown or duplicate reminder {identity!r}")
        seen.add(identity)
        boolean(reminder["enabled"], f"{label}.enabled")
        integer(reminder["every_minutes"], 1, 1440, f"{label}.every_minutes")
        integer(reminder["duration_seconds"], 0, 3600, f"{label}.duration_seconds")
        integer(reminder["distance_feet"], 0, 100, f"{label}.distance_feet")
        integer(reminder["priority"], 0, 1000, f"{label}.priority")
        cue = reminder["cue"]
        if not isinstance(cue, str) or cue not in cue_names:
            raise ValueError(f"{label}.cue: requires a canonical icon name, got {cue!r}")
        if reminder["interval_basis"] not in ("guidance", "product_default"):
            raise ValueError(f"{label}.interval_basis: requires guidance or product_default")
        source = reminder["source"]
        if not isinstance(source, str) or not source or any(character.isspace() for character in source):
            raise ValueError(f"{label}.source: requires an absolute HTTPS URL")
        try:
            parsed = urlsplit(source)
            valid_source = parsed.scheme == "https" and bool(parsed.netloc) and bool(parsed.hostname)
            # Accessing the port also rejects malformed or out-of-range explicit URL ports.
            parsed.port
        except ValueError as error:
            raise ValueError(f"{label}.source: invalid absolute HTTPS URL {source!r}") from error
        if not valid_source:
            raise ValueError(f"{label}.source: requires an absolute HTTPS URL, got {source!r}")
    if seen != expected_names:
        raise ValueError("wellbeing.reminders: reminder identity catalog is incomplete")


# path: Canonical personality authoring YAML.
# Raises OSError for unreadable source or ValueError for invalid/ambiguous authoring.
# Raises yaml.YAMLError for malformed YAML syntax or unsupported YAML tags.
def load_personality_rules(path):
    rules = yaml.load(Path(path).read_text(encoding="utf-8"), Loader=RigLoader)
    fields(rules, ("schema", "scope", "face_binding", "catalog", "happiness", "sadness", "caress", "icons", "audio", "wellbeing"), "personality")
    if rules["schema"] != "cody.personality.v2":
        raise ValueError("Unsupported personality schema")
    if rules["scope"] != "session" or rules["face_binding"] != "disabled":
        raise ValueError("MVP personality requires session scope and disabled face binding")
    validate_catalog(rules["catalog"])
    happiness = rules["happiness"]
    fields(happiness, ("metric", "minimum", "maximum", "initial", "on_caress", "decay"), "happiness")
    if happiness["metric"] != "happiness":
        raise ValueError("happiness.metric: requires the named happiness metric")
    integer(happiness["minimum"], 0, 0, "happiness.minimum")
    integer(happiness["maximum"], 100, 100, "happiness.maximum")
    integer(happiness["initial"], 0, 100, "happiness.initial")
    fields(happiness["on_caress"], ("event", "add"), "happiness.on_caress")
    if happiness["on_caress"]["event"] != "caress":
        raise ValueError("happiness.on_caress.event: requires the named caress event")
    integer(happiness["on_caress"]["add"], 1, 100, "happiness.on_caress.add")
    decay = happiness["decay"]
    fields(decay, ("subtract", "every_seconds", "clock"), "happiness.decay")
    integer(decay["subtract"], 1, 100, "happiness.decay.subtract")
    integer(decay["every_seconds"], 1, 86400, "happiness.decay.every_seconds")
    if decay["clock"] != "awake":
        raise ValueError("MVP happiness decay requires the awake clock")
    fields(rules["sadness"], ("metric", "rule"), "sadness")
    if rules["sadness"]["metric"] != "sadness":
        raise ValueError("sadness.metric: requires the named sadness metric")
    if rules["sadness"]["rule"] != "complement_of_happiness":
        raise ValueError("Sadness must be derived as maximum minus happiness")
    validate_caress(rules["caress"])
    validate_icons(rules["icons"])
    audio = rules["audio"]
    fields(audio, ("enabled", "cooldown_seconds", "request_expiry_ms"), "audio")
    boolean(audio["enabled"], "audio.enabled")
    integer(audio["cooldown_seconds"], 0, 3600, "audio.cooldown_seconds")
    integer(audio["request_expiry_ms"], 1, 1000, "audio.request_expiry_ms")
    validate_wellbeing(rules["wellbeing"])
    return rules
