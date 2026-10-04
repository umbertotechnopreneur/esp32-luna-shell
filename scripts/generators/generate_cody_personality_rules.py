"""Compile the personality catalog and policies into immutable firmware data."""

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.common.asset_provenance import text_source_hash
from scripts.common.cody_personality_contract import load_personality_rules


# name: Validated permanent snake-case catalog name.
def enum_name(name):
    return "".join(part.capitalize() for part in name.split("_"))


# value: Validated finite numeric geometry parameter.
def cpp_float(value):
    return f"{float(value)!r}f"


# rules: Strictly validated canonical authoring rules.
def generated_catalog(rules):
    lines = ["// Generated from assets/cody/personality.yaml; edit that source, then regenerate.",
             "#pragma once", "#include <cstdint>", "namespace CODY {"]
    types = {"events": "PersonalityEvent", "zones": "PettingZone",
             "metrics": "PersonalityMetric", "icons": "PersonalityCue",
             "reminders": "PersonalityReminder"}
    for kind, type_name in types.items():
        entries = sorted(rules["catalog"][kind], key=lambda entry: entry["id"])
        lines.extend((f"enum class {type_name} : uint8_t {{", "    None = 0,"))
        lines.extend(f"    {enum_name(entry['name'])} = {entry['id']}," for entry in entries)
        lines.append("};")
        lines.extend((f"struct {type_name}Identity {{", f"    {type_name} id;",
                      "    const char *name;", "};",
                      f"inline constexpr {type_name}Identity {kind}_catalog[] = {{"))
        lines.extend(f'    {{{type_name}::{enum_name(entry["name"])}, "{entry["name"]}"}},'
                     for entry in entries)
        lines.append("};")
    lines.extend(("} // namespace CODY", ""))
    return "\n".join(lines)


# rules: Strictly validated channel, catalog, follow and audio policies.
def generated_personality_config(rules):
    icons = rules["icons"]
    lines = ["// Generated from assets/cody/personality.yaml; edit that source, then regenerate.",
             "#pragma once", '#include "personality_catalog.hpp"', "#include <cstdint>",
             "namespace CODY::PersonalityConfig {",
             "struct AnchorProfile { float offset_x; float offset_y; float extent_per_scale; };"]
    for name, anchor in icons["anchors"].items():
        values = ", ".join(cpp_float(anchor[key]) for key in ("offset_x", "offset_y", "extent_per_scale"))
        lines.append(f"inline constexpr AnchorProfile {name}{{{values}}};")
    for section, names in (("channel", ("stability", "fade_in", "minimum_visible", "fade_out")),
                           ("follow", ("position_window", "scale_window", "history_bucket"))):
        for name in names:
            lines.append(f"inline constexpr int64_t {name}_us = {icons[section][name + '_ms'] * 1000};")
    follow = icons["follow"]
    count = follow["scale_window_ms"] // follow["history_bucket_ms"] + 1
    lines.append(f"inline constexpr int64_t history_bucket_count = {count};")
    for name in ("maximum_position_lag_pixels", "minimum_extent", "maximum_base_extent",
                 "maximum_deformation_pixels", "body_clearance_pixels"):
        lines.append(f"inline constexpr float {name} = {cpp_float(follow[name])};")
    lines.extend(("struct CueRule {", "    PersonalityCue cue;", "    uint16_t priority;",
                  "    bool interrupts;", "    int64_t repeat_cooldown_us;", "    bool sfx_enabled;",
                  "};", "inline constexpr CueRule cue_rules[] = {"))
    for entry in sorted(rules["catalog"]["icons"], key=lambda item: item["id"]):
        lines.append(f"    {{PersonalityCue::{enum_name(entry['name'])}, {entry['priority']}, "
                     f"{str(entry['interrupts']).lower()}, {entry['repeat_cooldown_seconds'] * 1_000_000}, "
                     f"{str(entry['sfx_enabled']).lower()}}},")
    lines.extend(("};", "inline constexpr uint8_t cue_rule_count = 14;"))
    for name, value in icons["low_battery"].items():
        lines.append(f"inline constexpr uint8_t low_battery_{name} = {value};")
    lines.append("} // namespace CODY::PersonalityConfig")
    audio = rules["audio"]
    lines.extend(("namespace CODY::PersonalityAudioConfig {",
                  f"inline constexpr bool enabled = {str(audio['enabled']).lower()};",
                  f"inline constexpr int64_t cooldown_us = {audio['cooldown_seconds'] * 1_000_000};",
                  f"inline constexpr int64_t request_expiry_us = {audio['request_expiry_ms'] * 1000};",
                  "} // namespace CODY::PersonalityAudioConfig", ""))
    return "\n".join(lines)


# rules: Strictly validated gesture and named event bindings.
def generated_petting_config(rules):
    caress = rules["caress"]
    lines = ["// Generated from assets/cody/personality.yaml; edit that source, then regenerate.",
             "#pragma once", '#include "personality_catalog.hpp"', "#include <cstdint>",
             "namespace CODY::PettingConfig {",
             f"inline constexpr PersonalityEvent event = PersonalityEvent::{enum_name(caress['event'])};"]
    for name in ("minimum_contact", "maximum_contact", "maximum_sample_gap"):
        lines.append(f"inline constexpr int64_t {name}_us = {caress[name + '_ms'] * 1000};")
    for name in ("cooldown", "affection"):
        lines.append(f"inline constexpr int64_t {name}_us = {caress[name + '_seconds'] * 1_000_000};")
    for name in ("upper_margin_per_scale", "lower_margin_per_scale", "minimum_path_per_scale",
                 "minimum_displacement_per_scale", "maximum_speed_per_scale", "jitter_pixels"):
        lines.append(f"inline constexpr float {name} = {cpp_float(caress[name])};")
    lines.extend(("} // namespace CODY::PettingConfig", ""))
    return "\n".join(lines)


# rules: Strictly validated canonical authoring rules.
def generated_header(rules):
    happiness = rules["happiness"]
    values = (happiness["minimum"], happiness["maximum"], happiness["initial"],
              happiness["on_caress"]["add"], happiness["decay"]["subtract"],
              happiness["decay"]["every_seconds"] * 1_000_000)
    encoded = ", ".join(str(value) for value in values)
    return "\n".join((
        "// Generated from assets/cody/personality.yaml; edit that source, then regenerate.",
        "#pragma once", '#include "CODY_happiness.hpp"', '#include "CODY_wellbeing.hpp"',
        "namespace CODY::PersonalityData {",
        f"inline constexpr HappinessConfig happiness{{{encoded}}};",
        generated_wellbeing_config(rules),
        "} // namespace CODY::PersonalityData", "",
    ))


# rules: Validated dormant active-session reminder policies.
def generated_wellbeing_config(rules):
    wellbeing = rules["wellbeing"]
    identities = {entry["name"]: entry["id"] for entry in rules["catalog"]["reminders"]}
    lines = [f"inline constexpr WellbeingConfig wellbeing{{{str(wellbeing['enabled']).lower()}, "
             f"{wellbeing['delivery']['snooze_minutes'] * 60_000_000}, {{"]
    for rule in sorted(wellbeing["reminders"], key=lambda item: identities[item["reminder"]]):
        lines.append(f"    {{PersonalityReminder::{enum_name(rule['reminder'])}, "
                     f"{str(rule['enabled']).lower()}, {rule['every_minutes'] * 60_000_000}}},")
    lines.append("}};")
    return "\n".join(lines)


# argv: Explicit CLI arguments forwarded by scripts/device.ps1 generate.
# Raises the original authoring or filesystem error; no default/repair is attempted.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    source = ROOT / "assets/cody/personality.yaml"
    rules = load_personality_rules(source)
    output = ROOT / "assets/cody/personality/generated"
    output.mkdir(parents=True, exist_ok=True)
    headers = {"personality_rules.hpp": generated_header(rules),
               "personality_catalog.hpp": generated_catalog(rules),
               "CODY_personality_config.hpp": generated_personality_config(rules),
               "CODY_petting_config.hpp": generated_petting_config(rules)}
    for name, content in headers.items():
        (output / name).write_text(content, encoding="utf-8")
    manifest = {"schema": "cody.personality.generated.v2",
                "source": source.relative_to(ROOT).as_posix(),
                "source_sha256": text_source_hash(source),
                "headers": [(output / name).relative_to(ROOT).as_posix() for name in headers],
                "rules": rules}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated personality catalog and {len(headers)} immutable rule headers: {output}")


if __name__ == "__main__":
    main()
