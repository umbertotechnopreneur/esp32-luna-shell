"""Source fixtures for dormant work-session timer logic; execution needs authorization."""

from copy import deepcopy
from dataclasses import replace

from scripts.common.cody_personality_contract import load_personality_rules
from simulator.windows.cody_wellbeing import (
    MAXIMUM_CLOCK_US, MICROSECONDS_PER_MINUTE, PersonalityEvent, PersonalityReminder,
    WellbeingReminderPhase, WorkSessionPhase, advance_wellbeing, apply_wellbeing_event,
    fatigue_minutes, fatigue_remainder_us, initialize_wellbeing, read_wellbeing_snapshot,
)


# root: Checkout containing canonical dormant wellbeing authoring.
# Raises AssertionError for lifecycle, due-state, clock, acknowledgement or identity regressions.
def check_host_wellbeing(root):
    rules = load_personality_rules(root / "assets/cody/personality.yaml")
    assertions = 0

    # condition: Expected pure-engine contract without any session adapter or icon side effect.
    # message: Named regression to expose when the contract fails.
    # Raises AssertionError for a violated expectation.
    def require(condition, message):
        nonlocal assertions
        assertions += 1
        if not condition:
            raise AssertionError(message)

    # operation: Explicit invalid action evaluated only when this suite is authorized.
    # message: Identity of the rejected input or state transition.
    # exception: Required rejection type, distinguishing disabled logic from malformed requests.
    # Raises AssertionError when the operation succeeds or rejects with an unexpected cause.
    def reject(operation, message, exception=ValueError):
        try:
            operation()
        except exception:
            require(True, message)
        else:
            require(False, message)

    # state: One immutable snapshot with uniquely named reminder identities.
    # identity: Stable reminder enum, independent of catalog and rule list ordering.
    def reminder_state(state, identity):
        return next(item for item in state.reminders if item.reminder == identity)

    require(not rules["wellbeing"]["enabled"] and not rules["wellbeing"]["delivery"]["enabled"],
            "Authored wellbeing and reminder delivery remain disabled")
    inactive = initialize_wellbeing(rules)
    require(inactive.session == WorkSessionPhase.INACTIVE and fatigue_minutes(inactive) == 0,
            "Initialization does not infer an active work session")
    require(advance_wellbeing(rules, inactive, 3_600_000_000) == inactive,
            "Disabled logic cannot accumulate work or reminders")
    reject(lambda: apply_wellbeing_event(rules, inactive, PersonalityEvent.WORK_SESSION_START),
           "A disabled engine rejects explicit activation", NotImplementedError)
    reject(lambda: apply_wellbeing_event(rules, inactive, PersonalityEvent.CARESS),
           "Caress is not silently reinterpreted as a work-session event")

    fixture = deepcopy(rules)
    fixture["wellbeing"]["enabled"] = True
    fixture["wellbeing"]["delivery"]["snooze_minutes"] = 10
    intervals = {"screen_break": 60, "movement_break": 30, "eye_rest": 20, "drink_water": 60}
    for item in fixture["wellbeing"]["reminders"]:
        item["enabled"] = True
        item["every_minutes"] = intervals[item["reminder"]]
    initial = initialize_wellbeing(fixture)
    require(advance_wellbeing(fixture, initial, 3_600_000_000) == initial,
            "Enabled logic still requires an explicit session start")
    active = apply_wellbeing_event(fixture, initial, PersonalityEvent.WORK_SESSION_START)
    require(active.session == WorkSessionPhase.ACTIVE and all(
            item.phase == WellbeingReminderPhase.COUNTING for item in active.reminders),
            "Explicit start initializes four independent active-time cycles")
    reject(lambda: apply_wellbeing_event(fixture, active, PersonalityEvent.WORK_SESSION_START),
           "Repeated start cannot erase an active session")
    almost_minute = advance_wellbeing(fixture, active, 59_999_999)
    require(fatigue_minutes(almost_minute) == 0 and fatigue_remainder_us(almost_minute) == 59_999_999,
            "Fatigue minutes preserve the complete fractional-minute remainder")
    snapshot = read_wellbeing_snapshot(fixture, almost_minute)
    require(snapshot.active_elapsed_us == 59_999_999 and snapshot.fatigue_minutes == 0 and
            snapshot.fatigue_remainder_us == 59_999_999 and snapshot.reminders == almost_minute.reminders,
            "Reading a wellbeing snapshot preserves exact time and independent reminder identities")
    minute = advance_wellbeing(fixture, almost_minute, 1)
    require(fatigue_minutes(minute) == 1 and fatigue_remainder_us(minute) == 0,
            "A complete active minute advances the artistic work counter exactly once")
    paused = apply_wellbeing_event(fixture, minute, PersonalityEvent.WORK_SESSION_PAUSE)
    require(advance_wellbeing(fixture, paused, 7_200_000_000) == paused,
            "Paused work cannot accumulate fatigue or pay reminder deadlines")
    reject(lambda: apply_wellbeing_event(fixture, paused, PersonalityEvent.WORK_SESSION_PAUSE),
           "Pause requires an active session")
    resumed = apply_wellbeing_event(fixture, paused, PersonalityEvent.WORK_SESSION_RESUME)
    require(resumed == minute, "Resume retains the exact active-time counters and deadlines")
    reject(lambda: apply_wellbeing_event(fixture, resumed, PersonalityEvent.WORK_SESSION_RESUME),
           "Resume requires a paused session")

    before_eyes = advance_wellbeing(fixture, active, 20 * MICROSECONDS_PER_MINUTE - 1)
    require(all(item.phase == WellbeingReminderPhase.COUNTING for item in before_eyes.reminders),
            "No reminder becomes due before its own inclusive deadline")
    eyes = advance_wellbeing(fixture, before_eyes, 1)
    require(reminder_state(eyes, PersonalityReminder.EYE_REST).phase == WellbeingReminderPhase.DUE and
            all(item.phase == WellbeingReminderPhase.COUNTING for item in eyes.reminders
                if item.reminder != PersonalityReminder.EYE_REST),
            "Eye rest becomes due independently at its 20-minute boundary")
    hour = advance_wellbeing(fixture, active, 60 * MICROSECONDS_PER_MINUTE)
    require(all(item.phase == WellbeingReminderPhase.DUE for item in hour.reminders),
            "All four reminders can be due together without choosing or queuing icons")
    continued = advance_wellbeing(fixture, hour, 60 * MICROSECONDS_PER_MINUTE)
    require(continued.reminders == hour.reminders and fatigue_minutes(continued) == 120,
            "Overdue reminders remain one due state without automatic repetition or cycle debt")
    stepped = active
    for elapsed in (1, 59_999_999, 713_000_000, 1_227_000_000, 1_600_000_000):
        stepped = advance_wellbeing(fixture, stepped, elapsed)
    require(stepped == hour, "Timer results are independent of update cadence")

    acknowledged_eyes = apply_wellbeing_event(fixture, hour, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                            PersonalityReminder.EYE_REST)
    require(fatigue_minutes(acknowledged_eyes) == 60 and
            reminder_state(acknowledged_eyes, PersonalityReminder.EYE_REST).deadline_us == 80 * MICROSECONDS_PER_MINUTE,
            "Eye acknowledgement restarts only its cycle and does not reset fatigue")
    for identity in (PersonalityReminder.MOVEMENT_BREAK, PersonalityReminder.DRINK_WATER):
        acknowledged = apply_wellbeing_event(fixture, hour, PersonalityEvent.REMINDER_ACKNOWLEDGE, identity)
        require(acknowledged.fatigue_elapsed_us == hour.fatigue_elapsed_us,
                "Movement and water acknowledgement do not claim a completed screen break")
    screen = apply_wellbeing_event(fixture, hour, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                  PersonalityReminder.SCREEN_BREAK)
    require(screen.active_elapsed_us == hour.active_elapsed_us and screen.fatigue_elapsed_us == 0 and
            reminder_state(screen, PersonalityReminder.SCREEN_BREAK).deadline_us == 120 * MICROSECONDS_PER_MINUTE,
            "Explicit screen-break acknowledgement alone resets fatigue and its reminder cycle")
    require(all(item == reminder_state(hour, item.reminder) for item in screen.reminders
                if item.reminder != PersonalityReminder.SCREEN_BREAK),
            "Screen rest does not acknowledge the other independent reminders")
    reject(lambda: apply_wellbeing_event(fixture, screen, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                         PersonalityReminder.SCREEN_BREAK),
           "A counting reminder cannot be acknowledged twice")
    snoozed = apply_wellbeing_event(fixture, hour, PersonalityEvent.REMINDER_SNOOZE,
                                   PersonalityReminder.DRINK_WATER)
    require(reminder_state(snoozed, PersonalityReminder.DRINK_WATER).phase == WellbeingReminderPhase.SNOOZED and
            reminder_state(snoozed, PersonalityReminder.DRINK_WATER).deadline_us == 70 * MICROSECONDS_PER_MINUTE and
            snoozed.fatigue_elapsed_us == hour.fatigue_elapsed_us,
            "Snooze creates one active-time deadline without implying a completed break")
    reject(lambda: apply_wellbeing_event(fixture, snoozed, PersonalityEvent.REMINDER_SNOOZE,
                                         PersonalityReminder.DRINK_WATER),
           "Snoozing requires a due reminder rather than adding another queued delay")
    before_snooze = advance_wellbeing(fixture, snoozed, 10 * MICROSECONDS_PER_MINUTE - 1)
    require(reminder_state(before_snooze, PersonalityReminder.DRINK_WATER).phase == WellbeingReminderPhase.SNOOZED,
            "Snoozed water remains quiet before its new deadline")
    after_snooze = advance_wellbeing(fixture, before_snooze, 1)
    require(reminder_state(after_snooze, PersonalityReminder.DRINK_WATER).phase == WellbeingReminderPhase.DUE,
            "Snoozed water becomes due exactly at the replacement deadline")

    paused_due = apply_wellbeing_event(fixture, hour, PersonalityEvent.WORK_SESSION_PAUSE)
    paused_ack = apply_wellbeing_event(fixture, paused_due, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                      PersonalityReminder.SCREEN_BREAK)
    require(paused_ack.session == WorkSessionPhase.PAUSED and paused_ack.fatigue_elapsed_us == 0,
            "An explicit completed break can be acknowledged while the session is paused")
    ended = apply_wellbeing_event(fixture, hour, PersonalityEvent.WORK_SESSION_END)
    require(ended.session == WorkSessionPhase.INACTIVE and ended.reminders == hour.reminders and
            advance_wellbeing(fixture, ended, 3_600_000_000) == ended,
            "Ending freezes the retained snapshot and never acknowledges a break implicitly")
    ended_ack = apply_wellbeing_event(fixture, ended, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                     PersonalityReminder.SCREEN_BREAK)
    require(ended_ack.session == WorkSessionPhase.INACTIVE and ended_ack.fatigue_elapsed_us == 0,
            "Retained due reminders accept explicit acknowledgement after session end")
    restarted = apply_wellbeing_event(fixture, ended, PersonalityEvent.WORK_SESSION_START)
    require(restarted == active, "A later explicit session start initializes a fresh work session")
    reject(lambda: apply_wellbeing_event(fixture, initial, PersonalityEvent.WORK_SESSION_END),
           "An inactive session cannot end again")

    reordered = deepcopy(fixture)
    reordered["catalog"]["reminders"].reverse()
    reordered["wellbeing"]["reminders"].reverse()
    reordered_start = apply_wellbeing_event(reordered, initialize_wellbeing(reordered),
                                           PersonalityEvent.WORK_SESSION_START)
    require(reordered_start == active, "Reminder identity and initial state survive catalog reordering")
    one_disabled = deepcopy(fixture)
    for item in one_disabled["wellbeing"]["reminders"]:
        if item["reminder"] == "movement_break":
            item["enabled"] = False
    disabled_start = apply_wellbeing_event(one_disabled, initialize_wellbeing(one_disabled),
                                          PersonalityEvent.WORK_SESSION_START)
    disabled_hour = advance_wellbeing(one_disabled, disabled_start, 60 * MICROSECONDS_PER_MINUTE)
    require(reminder_state(disabled_hour, PersonalityReminder.MOVEMENT_BREAK).phase == WellbeingReminderPhase.DISABLED,
            "Disabling one reminder leaves the other cycles independent")
    reject(lambda: apply_wellbeing_event(one_disabled, disabled_hour, PersonalityEvent.REMINDER_ACKNOWLEDGE,
                                         PersonalityReminder.MOVEMENT_BREAK),
           "A disabled reminder cannot be acknowledged")
    reject(lambda: advance_wellbeing(fixture, active, -1), "Backwards elapsed time is rejected")
    reject(lambda: apply_wellbeing_event(fixture, active, 3), "Raw integers cannot impersonate typed events")
    reject(lambda: apply_wellbeing_event(fixture, hour, PersonalityEvent.REMINDER_SNOOZE, 4),
           "Raw integers cannot impersonate typed reminder identities")
    reject(lambda: apply_wellbeing_event(fixture, active, PersonalityEvent.WORK_SESSION_PAUSE,
                                         PersonalityReminder.SCREEN_BREAK),
           "Session events cannot carry an unrelated reminder identity")
    reject(lambda: advance_wellbeing(fixture, replace(active, fatigue_elapsed_us=1), 0),
           "Fatigue cannot exceed the explicit session clock")
    reject(lambda: advance_wellbeing(rules, active, 0),
           "Disabled authoring cannot accept a running session state")
    reject(lambda: advance_wellbeing(fixture, replace(initial, session=WorkSessionPhase.ACTIVE), 0),
           "Active enabled reminders cannot silently remain disabled")
    for invalid_interval in (0, 1.5, 1441):
        invalid_rules = deepcopy(fixture)
        invalid_rules["wellbeing"]["reminders"][0]["every_minutes"] = invalid_interval
        reject(lambda: initialize_wellbeing(invalid_rules),
               "Reminder intervals require whole minutes between one and 1440")
        invalid_snooze = deepcopy(fixture)
        invalid_snooze["wellbeing"]["delivery"]["snooze_minutes"] = invalid_interval
        reject(lambda: initialize_wellbeing(invalid_snooze),
               "Snooze intervals require whole minutes between one and 1440")
    at_limit = advance_wellbeing(fixture, active, MAXIMUM_CLOCK_US)
    reject(lambda: advance_wellbeing(fixture, at_limit, 1), "Active-clock overflow is rejected")
    reject(lambda: apply_wellbeing_event(fixture, at_limit, PersonalityEvent.REMINDER_SNOOZE,
                                         PersonalityReminder.DRINK_WATER),
           "Reminder deadline overflow is rejected")
    require(at_limit.active_elapsed_us == MAXIMUM_CLOCK_US,
            "Rejected operations leave the caller's immutable state unchanged")
    return assertions
