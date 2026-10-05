// Generated from assets/cody/personality.yaml; edit that source, then regenerate.
#pragma once
#include <cstdint>
namespace CODY {
enum class PersonalityEvent : uint8_t {
    None = 0,
    Caress = 1,
    WorkSessionStart = 2,
    WorkSessionPause = 3,
    WorkSessionResume = 4,
    WorkSessionEnd = 5,
    ReminderAcknowledge = 6,
    ReminderSnooze = 7,
};
struct PersonalityEventIdentity {
    PersonalityEvent id;
    const char *name;
};
inline constexpr PersonalityEventIdentity events_catalog[] = {
    {PersonalityEvent::Caress, "caress"},
    {PersonalityEvent::WorkSessionStart, "work_session_start"},
    {PersonalityEvent::WorkSessionPause, "work_session_pause"},
    {PersonalityEvent::WorkSessionResume, "work_session_resume"},
    {PersonalityEvent::WorkSessionEnd, "work_session_end"},
    {PersonalityEvent::ReminderAcknowledge, "reminder_acknowledge"},
    {PersonalityEvent::ReminderSnooze, "reminder_snooze"},
};
enum class PettingZone : uint8_t {
    None = 0,
    AboveEyes = 1,
    BelowMouth = 2,
};
struct PettingZoneIdentity {
    PettingZone id;
    const char *name;
};
inline constexpr PettingZoneIdentity zones_catalog[] = {
    {PettingZone::AboveEyes, "above_eyes"},
    {PettingZone::BelowMouth, "below_mouth"},
};
enum class PersonalityMetric : uint8_t {
    None = 0,
    Happiness = 1,
    Sadness = 2,
    FatigueMinutes = 3,
};
struct PersonalityMetricIdentity {
    PersonalityMetric id;
    const char *name;
};
inline constexpr PersonalityMetricIdentity metrics_catalog[] = {
    {PersonalityMetric::Happiness, "happiness"},
    {PersonalityMetric::Sadness, "sadness"},
    {PersonalityMetric::FatigueMinutes, "fatigue_minutes"},
};
enum class PersonalityCue : uint8_t {
    None = 0,
    Sleep = 1,
    Fear = 2,
    Joy = 3,
    Hunger = 4,
    Thirst = 5,
    Heat = 6,
    Cold = 7,
    LowBattery = 8,
    Charging = 9,
    NoWifi = 10,
    Curiosity = 11,
    Confusion = 12,
    Affection = 13,
    Frustration = 14,
};
struct PersonalityCueIdentity {
    PersonalityCue id;
    const char *name;
};
inline constexpr PersonalityCueIdentity icons_catalog[] = {
    {PersonalityCue::Sleep, "sleep"},
    {PersonalityCue::Fear, "fear"},
    {PersonalityCue::Joy, "joy"},
    {PersonalityCue::Hunger, "hunger"},
    {PersonalityCue::Thirst, "thirst"},
    {PersonalityCue::Heat, "heat"},
    {PersonalityCue::Cold, "cold"},
    {PersonalityCue::LowBattery, "low_battery"},
    {PersonalityCue::Charging, "charging"},
    {PersonalityCue::NoWifi, "no_wifi"},
    {PersonalityCue::Curiosity, "curiosity"},
    {PersonalityCue::Confusion, "confusion"},
    {PersonalityCue::Affection, "affection"},
    {PersonalityCue::Frustration, "frustration"},
};
enum class PersonalityReminder : uint8_t {
    None = 0,
    ScreenBreak = 1,
    MovementBreak = 2,
    EyeRest = 3,
    DrinkWater = 4,
};
struct PersonalityReminderIdentity {
    PersonalityReminder id;
    const char *name;
};
inline constexpr PersonalityReminderIdentity reminders_catalog[] = {
    {PersonalityReminder::ScreenBreak, "screen_break"},
    {PersonalityReminder::MovementBreak, "movement_break"},
    {PersonalityReminder::EyeRest, "eye_rest"},
    {PersonalityReminder::DrinkWater, "drink_water"},
};
} // namespace CODY
