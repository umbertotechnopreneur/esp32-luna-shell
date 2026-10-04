// Generated from assets/cody/personality.yaml; edit that source, then regenerate.
#pragma once
#include "CODY_happiness.hpp"
#include "CODY_wellbeing.hpp"
namespace CODY::PersonalityData {
inline constexpr HappinessConfig happiness{0, 100, 50, 10, 1, 60000000};
inline constexpr WellbeingConfig wellbeing{false, 600000000, {
    {PersonalityReminder::ScreenBreak, true, 3600000000},
    {PersonalityReminder::MovementBreak, true, 1800000000},
    {PersonalityReminder::EyeRest, true, 1200000000},
    {PersonalityReminder::DrinkWater, true, 3600000000},
}};
} // namespace CODY::PersonalityData
