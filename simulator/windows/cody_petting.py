# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Synthetic pointer preview of the native gentle-stroke recognizer."""

from enum import IntEnum
import math
from pathlib import Path

from scripts.common.cody_response import hit_test_body
from scripts.common.cody_personality_contract import load_personality_rules


PERSONALITY_RULES = load_personality_rules(Path(__file__).resolve().parents[2] / "assets/cody/personality.yaml")
PettingZone = IntEnum("PettingZone", {"NONE": 0} | {
    item["name"].upper(): item["id"] for item in PERSONALITY_RULES["catalog"]["zones"]}, module=__name__)


# root: Checkout containing the validated personality YAML caress rule.
# Raises OSError for unreadable source or ValueError for incomplete/invalid values.
def load_petting_config(root):
    caress = load_personality_rules(root / "assets/cody/personality.yaml")["caress"]
    config = {name.removesuffix("_ms") + "_us": caress[name] * 1000
              for name in ("minimum_contact_ms", "maximum_contact_ms", "maximum_sample_gap_ms")}
    config.update({"cooldown_us": caress["cooldown_seconds"] * 1_000_000,
                   "affection_us": caress["affection_seconds"] * 1_000_000})
    for name in ("upper_margin_per_scale", "lower_margin_per_scale", "minimum_path_per_scale",
                 "minimum_displacement_per_scale", "maximum_speed_per_scale", "jitter_pixels"):
        config[name] = caress[name]
    return config


class PettingRecognizer:
    # self: Synthetic gesture owner; it never publishes firmware observations.
    # config: Thresholds projected from the validated shared YAML caress rule.
    def __init__(self, config):
        self.config = config
        self.last_triggered_us = 0
        self.cancel()

    # self: Contact state whose cooldown survives cancellation.
    def cancel(self):
        self.zone = PettingZone.NONE
        self.origin = self.accepted = (0., 0.)
        self.scale = self.path = 0.
        self.started_us = self.sampled_us = self.accepted_us = 0
        self.active = self.rejected = self.triggered = False

    # self: Recognizer with the shared authored zone margins.
    # scene: Geometry of the last displayed character.
    # point: Logical pointer coordinate already mapped through display rotation.
    # Raises ValueError for invalid body, face or scale geometry.
    def classify(self, scene, point):
        body_hit = hit_test_body(scene, point)
        scale = scene["scale"]
        upper_y = min(scene["world"]["eye.left"][1], scene["world"]["eye.right"][1])
        lower_y = scene["world"]["mouth"][1]
        if not all(math.isfinite(value) for value in (scale, upper_y, lower_y)) or scale <= 0:
            raise ValueError("Petting face anchors or scale rejected")
        if body_hit and point[1] <= upper_y - scale * self.config["upper_margin_per_scale"]:
            return PettingZone.ABOVE_EYES
        if body_hit and point[1] >= lower_y + scale * self.config["lower_margin_per_scale"]:
            return PettingZone.BELOW_MOUTH
        return PettingZone.NONE

    # self: One contact plus the last accepted cooldown, without an event queue.
    # scene: Last displayed body and face geometry.
    # point: Logical pointer location; release coordinates are not classified.
    # pressed: Actual desktop pointer button state.
    # pressed_edge: First report of the contact.
    # sampled_us: Original monotonic pointer event timestamp.
    # Raises ValueError for invalid acquisition time or evaluated geometry.
    def observe(self, scene, point, pressed, pressed_edge, sampled_us):
        if sampled_us <= 0:
            raise ValueError(f"Petting timestamp rejected: {sampled_us}")
        if not pressed:
            self.cancel()
            return PettingZone.NONE
        zone = self.classify(scene, point)
        if pressed_edge:
            self.cancel()
            self.active = True
            self.zone = zone
            self.origin = self.accepted = point
            self.scale = scene["scale"]
            self.started_us = self.sampled_us = self.accepted_us = sampled_us
            self.rejected = zone == PettingZone.NONE or (
                self.last_triggered_us != 0 and sampled_us - self.last_triggered_us < self.config["cooldown_us"])
            return PettingZone.NONE
        if not self.active or self.rejected or self.triggered:
            return PettingZone.NONE
        elapsed_us = sampled_us - self.started_us
        interval_us = sampled_us - self.sampled_us
        if (interval_us <= 0 or interval_us > self.config["maximum_sample_gap_us"] or
                elapsed_us > self.config["maximum_contact_us"] or zone != self.zone):
            self.rejected = True
            return PettingZone.NONE
        accepted_segment = math.dist(point, self.accepted)
        accepted_interval_us = sampled_us - self.accepted_us
        if (accepted_segment > self.config["jitter_pixels"] and
                accepted_segment * 1_000_000 / accepted_interval_us >
                self.scale * self.config["maximum_speed_per_scale"]):
            self.rejected = True
            return PettingZone.NONE
        self.sampled_us = sampled_us
        if accepted_segment > self.config["jitter_pixels"]:
            self.path += accepted_segment
            self.accepted = point
            self.accepted_us = sampled_us
        if (elapsed_us >= self.config["minimum_contact_us"] and
                self.path >= self.scale * self.config["minimum_path_per_scale"] and
                math.dist(point, self.origin) >= self.scale * self.config["minimum_displacement_per_scale"]):
            self.triggered = True
            self.last_triggered_us = sampled_us
            return self.zone
        return PettingZone.NONE
