"""Dormant work-session timers; no session adapter, icon delivery or device I/O."""

from dataclasses import dataclass, replace
from enum import IntEnum
from pathlib import Path

from scripts.common.cody_personality_contract import load_personality_rules
from simulator.windows.cody_happiness import PersonalityEvent


PERSONALITY_RULES = load_personality_rules(Path(__file__).resolve().parents[2] / "assets/cody/personality.yaml")
PersonalityReminder = IntEnum("PersonalityReminder", {"NONE": 0} | {
    item["name"].upper(): item["id"] for item in PERSONALITY_RULES["catalog"]["reminders"]}, module=__name__)
MICROSECONDS_PER_MINUTE = 60_000_000
MAXIMUM_CLOCK_US = (1 << 63) - 1


class WorkSessionPhase(IntEnum):
    INACTIVE = 0
    ACTIVE = 1
    PAUSED = 2


class WellbeingReminderPhase(IntEnum):
    DISABLED = 0
    COUNTING = 1
    DUE = 2
    SNOOZED = 3


@dataclass(frozen=True)
class WellbeingReminderState:
    reminder: PersonalityReminder
    phase: WellbeingReminderPhase = WellbeingReminderPhase.DISABLED
    deadline_us: int = 0


@dataclass(frozen=True)
class WellbeingState:
    session: WorkSessionPhase
    active_elapsed_us: int
    fatigue_elapsed_us: int
    reminders: tuple[WellbeingReminderState, ...]


@dataclass(frozen=True)
class WellbeingSnapshot:
    session: WorkSessionPhase
    active_elapsed_us: int
    fatigue_minutes: int
    fatigue_remainder_us: int
    reminders: tuple[WellbeingReminderState, ...]


# rules: Validated YAML with independent enabled reminders and an active-time snooze duration.
# Raises ValueError for malformed engine configuration or ambiguous reminder identities.
def wellbeing_configuration(rules):
    config = rules["wellbeing"]
    if type(config["enabled"]) is not bool or config["clock"] != "active_work_session":
        raise ValueError("Wellbeing requires explicit enablement and the active work-session clock")
    snooze_minutes = config["delivery"]["snooze_minutes"]
    maximum_minutes = 1440
    if type(snooze_minutes) is not int or not 1 <= snooze_minutes <= maximum_minutes:
        raise ValueError("Wellbeing snooze interval rejected")
    reminders = {}
    for item in config["reminders"]:
        try:
            identity = PersonalityReminder[item["reminder"].upper()]
        except KeyError as error:
            raise ValueError("Unknown wellbeing reminder identity") from error
        if identity == PersonalityReminder.NONE or identity in reminders:
            raise ValueError("Duplicate or empty wellbeing reminder identity")
        interval = item["every_minutes"]
        if type(item["enabled"]) is not bool or type(interval) is not int or not 1 <= interval <= maximum_minutes:
            raise ValueError(f"Wellbeing interval or enablement rejected: {identity.name}")
        reminders[identity] = item
    if set(reminders) != set(PersonalityReminder) - {PersonalityReminder.NONE}:
        raise ValueError("Wellbeing reminder catalog is incomplete")
    return config, reminders


# rules: Validated shared rules defining the four reminder identities.
# state: Immutable work-session state; invalid inputs are never repaired.
# Raises ValueError for malformed clocks, phases, identities or deadlines.
def validate_wellbeing_state(rules, state):
    config, definitions = wellbeing_configuration(rules)
    if not isinstance(state, WellbeingState) or not isinstance(state.session, WorkSessionPhase):
        raise ValueError("Wellbeing session state rejected")
    if not config["enabled"] and state.session != WorkSessionPhase.INACTIVE:
        raise ValueError("Disabled wellbeing logic cannot own a running or paused session")
    if (type(state.active_elapsed_us) is not int or not 0 <= state.active_elapsed_us <= MAXIMUM_CLOCK_US or
            type(state.fatigue_elapsed_us) is not int or not 0 <= state.fatigue_elapsed_us <= state.active_elapsed_us):
        raise ValueError("Wellbeing clock or fatigue state rejected")
    seen = set()
    for reminder in state.reminders:
        if (not isinstance(reminder, WellbeingReminderState) or
                not isinstance(reminder.reminder, PersonalityReminder) or
                reminder.reminder not in definitions or reminder.reminder in seen or
                not isinstance(reminder.phase, WellbeingReminderPhase) or
                type(reminder.deadline_us) is not int or not 0 <= reminder.deadline_us <= MAXIMUM_CLOCK_US):
            raise ValueError("Wellbeing reminder state rejected")
        seen.add(reminder.reminder)
        if reminder.phase == WellbeingReminderPhase.DISABLED:
            if (reminder.deadline_us != 0 or
                    definitions[reminder.reminder]["enabled"] and state.session != WorkSessionPhase.INACTIVE):
                raise ValueError("Disabled wellbeing reminder contradicts its deadline or running session")
        elif not definitions[reminder.reminder]["enabled"]:
            raise ValueError("Disabled wellbeing rule has an active reminder state")
        elif reminder.phase == WellbeingReminderPhase.DUE:
            if reminder.deadline_us == 0 or reminder.deadline_us > state.active_elapsed_us:
                raise ValueError("Wellbeing reminder is due before its deadline")
        elif reminder.deadline_us <= state.active_elapsed_us:
            raise ValueError("Wellbeing counting deadline has already elapsed")
    if seen != set(definitions):
        raise ValueError("Wellbeing state must contain every reminder exactly once")
    return config, definitions


# rules: Validated canonical authoring; disabled engines still have an inspectable initial state.
# Raises ValueError for rejected engine configuration.
def initialize_wellbeing(rules):
    _, definitions = wellbeing_configuration(rules)
    reminders = tuple(WellbeingReminderState(identity) for identity in sorted(definitions, key=int))
    return WellbeingState(WorkSessionPhase.INACTIVE, 0, 0, reminders)


# rules: Validated configuration; a disabled engine remains frozen.
# state: Immutable previous session state, independent of any actual app or display.
# elapsed_us: Nonnegative elapsed time supplied by an explicit work-session owner.
# Raises ValueError for invalid input or overflow, leaving the caller's state untouched.
def advance_wellbeing(rules, state, elapsed_us):
    config, _ = validate_wellbeing_state(rules, state)
    if type(elapsed_us) is not int or elapsed_us < 0:
        raise ValueError("Wellbeing elapsed time rejected")
    if not config["enabled"] or state.session != WorkSessionPhase.ACTIVE:
        return state
    if elapsed_us > MAXIMUM_CLOCK_US - state.active_elapsed_us:
        raise ValueError("Wellbeing active clock overflow")
    next_clock = state.active_elapsed_us + elapsed_us
    reminders = tuple(replace(reminder, phase=WellbeingReminderPhase.DUE)
        if reminder.phase in (WellbeingReminderPhase.COUNTING, WellbeingReminderPhase.SNOOZED)
        and next_clock >= reminder.deadline_us else reminder for reminder in state.reminders)
    return replace(state, active_elapsed_us=next_clock,
                   fatigue_elapsed_us=state.fatigue_elapsed_us + elapsed_us, reminders=reminders)


# rules: Validated engine rules; delivery metadata never triggers an icon or sound here.
# state: Immutable previous session and independent reminder deadlines.
# event: Typed explicit work-session or reminder action from the shared event catalog.
# reminder: Typed target for acknowledgement/snooze, or None for a session action.
# Raises NotImplementedError when a known operation targets the disabled engine.
# Raises ValueError for invalid identity, transition, unavailable reminder or deadline overflow.
def apply_wellbeing_event(rules, state, event, reminder=PersonalityReminder.NONE):
    config, definitions = validate_wellbeing_state(rules, state)
    session_events = {PersonalityEvent.WORK_SESSION_START, PersonalityEvent.WORK_SESSION_PAUSE,
                      PersonalityEvent.WORK_SESSION_RESUME, PersonalityEvent.WORK_SESSION_END}
    reminder_events = {PersonalityEvent.REMINDER_ACKNOWLEDGE, PersonalityEvent.REMINDER_SNOOZE}
    if not isinstance(event, PersonalityEvent) or event not in session_events | reminder_events:
        raise ValueError("Wellbeing event identity rejected")
    if not isinstance(reminder, PersonalityReminder):
        raise ValueError("Wellbeing reminder identity must be typed")
    if (event in session_events and reminder != PersonalityReminder.NONE or
            event in reminder_events and reminder not in definitions):
        raise ValueError("Wellbeing event/reminder pairing rejected")
    if not config["enabled"]:
        raise NotImplementedError("Wellbeing engine is disabled by authoring")
    if event == PersonalityEvent.WORK_SESSION_START:
        if state.session != WorkSessionPhase.INACTIVE:
            raise ValueError("Work session can start only when inactive")
        reminders = tuple(WellbeingReminderState(identity,
            WellbeingReminderPhase.COUNTING if definition["enabled"] else WellbeingReminderPhase.DISABLED,
            definition["every_minutes"] * MICROSECONDS_PER_MINUTE if definition["enabled"] else 0)
            for identity, definition in sorted(definitions.items(), key=lambda pair: int(pair[0])))
        return WellbeingState(WorkSessionPhase.ACTIVE, 0, 0, reminders)
    if event == PersonalityEvent.WORK_SESSION_PAUSE:
        if state.session != WorkSessionPhase.ACTIVE:
            raise ValueError("Only an active work session can pause")
        return replace(state, session=WorkSessionPhase.PAUSED)
    if event == PersonalityEvent.WORK_SESSION_RESUME:
        if state.session != WorkSessionPhase.PAUSED:
            raise ValueError("Only a paused work session can resume")
        return replace(state, session=WorkSessionPhase.ACTIVE)
    if event == PersonalityEvent.WORK_SESSION_END:
        if state.session == WorkSessionPhase.INACTIVE:
            raise ValueError("An inactive work session cannot end")
        return replace(state, session=WorkSessionPhase.INACTIVE)
    current = next(item for item in state.reminders if item.reminder == reminder)
    if not definitions[reminder]["enabled"] or current.phase != WellbeingReminderPhase.DUE:
        raise ValueError("Only a due enabled reminder can be acknowledged or snoozed")
    acknowledged = event == PersonalityEvent.REMINDER_ACKNOWLEDGE
    interval_minutes = definitions[reminder]["every_minutes"] if acknowledged else config["delivery"]["snooze_minutes"]
    interval_us = interval_minutes * MICROSECONDS_PER_MINUTE
    if interval_us > MAXIMUM_CLOCK_US - state.active_elapsed_us:
        raise ValueError("Wellbeing reminder deadline overflow")
    next_reminder = replace(current, deadline_us=state.active_elapsed_us + interval_us,
        phase=WellbeingReminderPhase.COUNTING if acknowledged else WellbeingReminderPhase.SNOOZED)
    reminders = tuple(next_reminder if item.reminder == reminder else item for item in state.reminders)
    fatigue = 0 if acknowledged and reminder == PersonalityReminder.SCREEN_BREAK else state.fatigue_elapsed_us
    return replace(state, reminders=reminders, fatigue_elapsed_us=fatigue)


# state: Session snapshot containing exact active microseconds since acknowledged screen rest.
def fatigue_minutes(state):
    return state.fatigue_elapsed_us // MICROSECONDS_PER_MINUTE


# state: Session snapshot whose fractional-minute time remains available to the next update.
def fatigue_remainder_us(state):
    return state.fatigue_elapsed_us % MICROSECONDS_PER_MINUTE


# rules: Validated settings used to check the existing identified session state.
# state: Immutable pure-engine state; reading never advances its active-work clock.
# Raises ValueError for invalid configuration or state.
def read_wellbeing_snapshot(rules, state):
    validate_wellbeing_state(rules, state)
    return WellbeingSnapshot(state.session, state.active_elapsed_us, fatigue_minutes(state),
                             fatigue_remainder_us(state), state.reminders)
