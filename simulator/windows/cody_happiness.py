"""Session-only artistic mood preview; it does not publish device measurements."""

from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

from scripts.common.cody_personality_contract import load_personality_rules


PERSONALITY_RULES = load_personality_rules(Path(__file__).resolve().parents[2] / "assets/cody/personality.yaml")
PersonalityEvent = IntEnum("PersonalityEvent", {
    item["name"].upper(): item["id"] for item in PERSONALITY_RULES["catalog"]["events"]}, module=__name__)
PersonalityMetric = IntEnum("PersonalityMetric", {
    item["name"].upper(): item["id"] for item in PERSONALITY_RULES["catalog"]["metrics"]}, module=__name__)


@dataclass(frozen=True)
class HappinessState:
    happiness: int
    decay_elapsed_us: int = 0


# rules: Strict canonical YAML already loaded by the shared authoring contract.
# state: One bounded artistic value and fractional-minute remainder.
# Raises ValueError for invalid state; values are not silently repaired.
def validate_state(rules, state):
    happiness = rules["happiness"]
    period_us = happiness["decay"]["every_seconds"] * 1_000_000
    if (type(state.happiness) is not int or
            not happiness["minimum"] <= state.happiness <= happiness["maximum"] or
            type(state.decay_elapsed_us) is not int or not 0 <= state.decay_elapsed_us < period_us):
        raise ValueError("Happiness state rejected")


# rules: Validated canonical authoring with session scope and disabled face binding.
def initial_state(rules):
    return HappinessState(rules["happiness"]["initial"])


# rules: Validated shared YAML defining one bounded value and its decay.
# state: Immutable previous state; invalid state is rejected.
# elapsed_us: Nonnegative elapsed awake time, independent of animation pause/FPS.
# Raises ValueError for invalid time or state.
def advance_happiness(rules, state, elapsed_us):
    validate_state(rules, state)
    if type(elapsed_us) is not int or elapsed_us < 0:
        raise ValueError(f"Happiness elapsed time rejected: {elapsed_us!r}")
    config = rules["happiness"]
    period_us = config["decay"]["every_seconds"] * 1_000_000
    ticks, remainder_us = divmod(state.decay_elapsed_us + elapsed_us, period_us)
    value = max(config["minimum"], state.happiness - ticks * config["decay"]["subtract"])
    return HappinessState(value, remainder_us)


# rules: Validated authoring providing the gain and its explicit event binding.
# state: Immutable mood state; the existing minute phase remains intact.
# event: Typed catalog identity of the recognized interaction.
# Raises ValueError for invalid state, an untyped identity or an unbound event.
def apply_event(rules, state, event):
    validate_state(rules, state)
    config = rules["happiness"]
    if not isinstance(event, PersonalityEvent) or event.name.lower() != config["on_caress"]["event"]:
        raise ValueError(f"Happiness event rejected: {event!r}")
    value = min(config["maximum"], state.happiness + config["on_caress"]["add"])
    return HappinessState(value, state.decay_elapsed_us)


# rules: Validated bounds defining the derived complement.
# state: One canonical artistic happiness value.
# Raises ValueError for invalid state.
def sadness(rules, state):
    validate_state(rules, state)
    return rules["happiness"]["maximum"] - state.happiness
