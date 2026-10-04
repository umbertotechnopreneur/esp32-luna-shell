"""Mood time/event boundaries and canonical YAML generation contracts."""

import yaml

from scripts.common.asset_provenance import text_source_hash
from scripts.common.cody_personality_contract import load_personality_rules
from scripts.generators.generate_cody_personality_rules import (
    generated_header, generated_catalog, generated_personality_config, generated_petting_config,
)
from simulator.windows.cody_happiness import (
    HappinessState, PersonalityEvent, advance_happiness, apply_event, initial_state, sadness,
)


# root: Checkout containing canonical YAML and generated firmware rules.
# output: Existing suite artifact directory for explicit invalid-authoring fixtures.
# Raises AssertionError for semantic/provenance regressions or original authoring/file errors.
def check_host_happiness(root, output):
    import json

    source = root / "assets/cody/personality.yaml"
    rules = load_personality_rules(source)
    generated = root / "assets/cody/personality/generated"
    manifest = json.loads((generated / "manifest.json").read_text(encoding="utf-8"))
    assertions = 0

    # condition: Expected time/event or source-provenance contract.
    # message: Named regression to expose without substituting a default.
    # Raises AssertionError when the contract is violated.
    def require(condition, message):
        nonlocal assertions
        assertions += 1
        if not condition:
            raise AssertionError(message)

    require(manifest["source_sha256"] == text_source_hash(source) and manifest["rules"] == rules,
            "Generated rules must match canonical YAML provenance and values")
    require((generated / "personality_rules.hpp").read_text(encoding="utf-8") == generated_header(rules),
            "Compiled mood constants must match the canonical rule generator")
    for name, emitter in (("personality_catalog.hpp", generated_catalog),
                          ("CODY_personality_config.hpp", generated_personality_config),
                          ("CODY_petting_config.hpp", generated_petting_config)):
        require((generated / name).read_text(encoding="utf-8") == emitter(rules),
                f"Compiled catalog and behavior must match YAML: {name}")
    # Fixed synthetic parameters isolate event ordering and minute boundaries from art tuning.
    fixture_rules = json.loads(json.dumps(rules))
    fixture_rules["happiness"]["initial"] = 50
    fixture_rules["happiness"]["on_caress"]["add"] = 10
    fixture_rules["happiness"]["decay"]["subtract"] = 1
    fixture_rules["happiness"]["decay"]["every_seconds"] = 60
    state = initial_state(fixture_rules)
    require(state.happiness == 50 and sadness(fixture_rules, state) == 50, "One value defines both moods")
    state = advance_happiness(fixture_rules, state, 59_999_999)
    require(state.happiness == 50, "No decay before the complete minute")
    state = apply_event(fixture_rules, state, PersonalityEvent.CARESS)
    require(state.happiness == 60 and state.decay_elapsed_us == 59_999_999,
            "Carezza adds happiness without restarting the existing minute")
    state = advance_happiness(fixture_rules, state, 1)
    require(state.happiness == 59 and sadness(fixture_rules, state) == 41 and state.decay_elapsed_us == 0,
            "One full minute removes one point and derives increased sadness")
    batched = advance_happiness(fixture_rules, initial_state(fixture_rules), 181_000_000)
    stepped = initial_state(fixture_rules)
    for elapsed in (30_000_000, 29_999_999, 1, 75_000_000, 46_000_000):
        stepped = advance_happiness(fixture_rules, stepped, elapsed)
    require(batched == stepped and batched.happiness == 47 and batched.decay_elapsed_us == 1_000_000,
            "Elapsed time determines decay regardless of update cadence")
    state = advance_happiness(fixture_rules, HappinessState(1), 600_000_000)
    require(state.happiness == 0 and sadness(fixture_rules, state) == 100,
            "Decay stops at the authored floor without integer underflow")
    state = apply_event(fixture_rules, state, PersonalityEvent.CARESS)
    require(state.happiness == 10, "Expired decay at the floor leaves no debt for a later carezza")
    state = apply_event(fixture_rules, HappinessState(98, 42), PersonalityEvent.CARESS)
    require(state.happiness == 100 and state.decay_elapsed_us == 42 and sadness(fixture_rules, state) == 0,
            "Gain saturates intentionally and preserves the minute phase")
    invalid_states = (HappinessState(-1), HappinessState(101), HappinessState(50, 60_000_000))
    for invalid in invalid_states:
        try:
            advance_happiness(fixture_rules, invalid, 0)
        except ValueError:
            require(True, "Invalid state rejected")
        else:
            require(False, "Invalid mood state must not be silently clamped")
    try:
        advance_happiness(fixture_rules, state, -1)
    except ValueError:
        require(True, "Backwards time rejected")
    else:
        require(False, "Backwards mood time must be rejected")
    directory = output / "mood-fixtures"
    directory.mkdir(parents=True, exist_ok=True)
    text = yaml.safe_dump(fixture_rules, sort_keys=False)
    invalid_documents = {
        "duplicate.yaml": text + "\nscope: session\n",
        "unknown.yaml": text + "\nunknown_rule: 1\n",
        "bad-initial.yaml": text.replace("initial: 50", "initial: 101"),
        "bad-clock.yaml": text.replace("clock: awake", "clock: guessed_wall_time"),
    }
    # Stable identities survive storage reordering but never remapping or ambiguity.
    reordered = json.loads(json.dumps(fixture_rules))
    for entries in reordered["catalog"].values():
        entries.reverse()
    reordered_path = directory / "reordered.yaml"
    reordered_path.write_text(yaml.safe_dump(reordered, sort_keys=False), encoding="utf-8")
    reordered_rules = load_personality_rules(reordered_path)
    require(generated_catalog(reordered_rules) == generated_catalog(fixture_rules),
            "Catalog order must not change event, metric, zone or cue identity")
    malformed = {}
    remapped = json.loads(json.dumps(fixture_rules))
    remapped["catalog"]["events"][0]["name"] = "petting"
    malformed["remapped-event.yaml"] = remapped
    duplicate = json.loads(json.dumps(fixture_rules))
    duplicate["catalog"]["icons"][1] = duplicate["catalog"]["icons"][0].copy()
    malformed["duplicate-icon.yaml"] = duplicate
    bad_reference = json.loads(json.dumps(fixture_rules))
    bad_reference["happiness"]["on_caress"]["event"] = "affection"
    malformed["bad-reference.yaml"] = bad_reference
    missing_zone = json.loads(json.dumps(fixture_rules))
    missing_zone["caress"]["zones"] = ["above_eyes"]
    malformed["missing-zone.yaml"] = missing_zone
    bad_thresholds = json.loads(json.dumps(fixture_rules))
    bad_thresholds["icons"]["low_battery"] = {"enter_percent": 20, "exit_percent": 15}
    malformed["reversed-hysteresis.yaml"] = bad_thresholds
    excessive_history = json.loads(json.dumps(fixture_rules))
    excessive_history["icons"]["follow"]["history_bucket_ms"] = 1
    malformed["excessive-history.yaml"] = excessive_history
    bad_reminder = json.loads(json.dumps(fixture_rules))
    bad_reminder["wellbeing"]["reminders"][0]["reminder"] = "unknown_break"
    malformed["unknown-reminder.yaml"] = bad_reminder
    visible_delivery = json.loads(json.dumps(fixture_rules))
    visible_delivery["wellbeing"]["delivery"]["enabled"] = True
    malformed["premature-delivery.yaml"] = visible_delivery
    raw_unit = json.loads(json.dumps(fixture_rules))
    raw_unit["wellbeing"]["fatigue"]["unit"] = "points"
    malformed["ambiguous-fatigue-unit.yaml"] = raw_unit
    missing_health_basis = json.loads(json.dumps(fixture_rules))
    del missing_health_basis["wellbeing"]["reminders"][0]["source"]
    malformed["missing-health-source.yaml"] = missing_health_basis
    for name, document in malformed.items():
        invalid_documents[name] = yaml.safe_dump(document, sort_keys=False)
    for name, document in invalid_documents.items():
        path = directory / name
        path.write_text(document, encoding="utf-8")
        try:
            load_personality_rules(path)
        except ValueError:
            require(True, "Invalid authoring rejected")
        else:
            require(False, f"Invalid personality YAML accepted: {name}")
    return assertions
