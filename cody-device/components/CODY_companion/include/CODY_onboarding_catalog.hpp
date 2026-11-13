#pragma once

#include <cstddef>
#include <cstdint>
#include "CODY_settings_model.hpp"

namespace CODY {

enum class OptionsPage : uint8_t {
    Menu = 0, MenuMore = 1, MenuMaintenance = 2, // Retired MenuInfo keeps identity 3 reserved.
    Display = 4, DisplayRings = 5, Time = 6, Behavior = 7, BehaviorMore = 8, BehaviorMoreSettings = 9,
    Alarms = 10, IdleOptions = 11, CodyOptions = 12, ThemesFirst = 13, ThemesSecond = 14,
    QuietHours = 15, QuietEdit = 16, RestartConfirm = 17, RestartPending = 18,
    ResetConfirm = 19, ResetPending = 20, ResetFailed = 21,
    GuideButtons = 22, GuideTouch = 23, GuideOffline = 24, SynthVoice = 25,
    GuideSofia = 26, GuideProject = 27, GuideThanks = 28, None = 255
};

struct OptionsPageDefinition {
    OptionsPage identity;
    const char *title;
    OptionsPage parent;
    OptionsPage next;
    OnboardingTourStep tour_step = OnboardingTourStep::Completed;
    uint8_t tour_position = 0;
};

inline constexpr OptionsPageDefinition kOptionsPages[]{
    {OptionsPage::Menu, "Settings", OptionsPage::None, OptionsPage::MenuMore},
    {OptionsPage::MenuMore, "Settings", OptionsPage::Menu, OptionsPage::MenuMaintenance},
    {OptionsPage::MenuMaintenance, "Settings", OptionsPage::MenuMore, OptionsPage::None},
    {OptionsPage::Display, "Display", OptionsPage::Menu, OptionsPage::DisplayRings},
    {OptionsPage::DisplayRings, "Display & time", OptionsPage::Display, OptionsPage::Time},
    {OptionsPage::Time, "Time", OptionsPage::DisplayRings, OptionsPage::None},
    {OptionsPage::Behavior, "Behavior", OptionsPage::Menu, OptionsPage::BehaviorMore},
    {OptionsPage::BehaviorMore, "Behavior", OptionsPage::Behavior, OptionsPage::BehaviorMoreSettings},
    {OptionsPage::BehaviorMoreSettings, "Behavior", OptionsPage::BehaviorMore, OptionsPage::None},
    {OptionsPage::Alarms, "Alarms", OptionsPage::BehaviorMoreSettings, OptionsPage::None},
    {OptionsPage::IdleOptions, "Idle display", OptionsPage::BehaviorMore, OptionsPage::None},
    {OptionsPage::CodyOptions, "Personalization", OptionsPage::Menu, OptionsPage::None},
    {OptionsPage::ThemesFirst, "Cody themes", OptionsPage::CodyOptions, OptionsPage::ThemesSecond},
    {OptionsPage::ThemesSecond, "Cody themes", OptionsPage::ThemesFirst, OptionsPage::None},
    {OptionsPage::QuietHours, "Quiet hours", OptionsPage::BehaviorMoreSettings, OptionsPage::None},
    {OptionsPage::QuietEdit, "Quiet hours", OptionsPage::QuietHours, OptionsPage::None},
    {OptionsPage::RestartConfirm, "Restart device", OptionsPage::MenuMore, OptionsPage::None},
    {OptionsPage::RestartPending, "Restarting...", OptionsPage::None, OptionsPage::None},
    {OptionsPage::ResetConfirm, "Factory reset", OptionsPage::MenuMore, OptionsPage::None},
    {OptionsPage::ResetPending, "Resetting...", OptionsPage::None, OptionsPage::None},
    {OptionsPage::ResetFailed, "Reset failed", OptionsPage::MenuMore, OptionsPage::None},
    {OptionsPage::SynthVoice, "Synth Voice", OptionsPage::BehaviorMoreSettings, OptionsPage::None},
    {OptionsPage::GuideButtons, "Buttons", OptionsPage::MenuMore, OptionsPage::GuideTouch, OnboardingTourStep::Buttons, 1},
    {OptionsPage::GuideTouch, "Touch", OptionsPage::GuideButtons, OptionsPage::GuideSofia, OnboardingTourStep::Touch, 2},
    {OptionsPage::GuideSofia, "Meet Sofia", OptionsPage::GuideTouch, OptionsPage::GuideOffline, OnboardingTourStep::Sofia, 3},
    {OptionsPage::GuideOffline, "Offline", OptionsPage::GuideSofia, OptionsPage::GuideProject, OnboardingTourStep::Offline, 4},
    {OptionsPage::GuideProject, "GitHub: swipe next", OptionsPage::GuideOffline, OptionsPage::GuideThanks, OnboardingTourStep::Project, 5},
    {OptionsPage::GuideThanks, "Thank you!", OptionsPage::GuideProject, OptionsPage::None, OnboardingTourStep::Thanks, 6},
};
inline constexpr uint8_t kOnboardingTourPages = 6;

// identity: Explicit page ID; removed and unrecognized values have no definition.
constexpr const OptionsPageDefinition *options_page_definition(OptionsPage identity)
{
    for (const auto &page : kOptionsPages) if (page.identity == identity) return &page;
    return nullptr;
}

// identity: Page whose metadata determines whether it belongs to the guide.
constexpr bool is_guide_page(OptionsPage identity)
{
    const auto *page = options_page_definition(identity);
    return page != nullptr && page->tour_position != 0;
}

// step: Decoded storage step; completion deliberately has no visible page.
constexpr const OptionsPageDefinition *onboarding_tour_page(OnboardingTourStep step)
{
    for (const auto &page : kOptionsPages) if (page.tour_position != 0 && page.tour_step == step) return &page;
    return nullptr;
}

// identity: Known page whose parent route is requested.
constexpr OptionsPage options_parent_page(OptionsPage identity)
{
    const auto *page = options_page_definition(identity);
    return page != nullptr ? page->parent : OptionsPage::None;
}

// No parameters: compile-time identity and tour-order admission for the complete catalog.
constexpr bool valid_options_catalog()
{
    size_t guides = 0;
    for (size_t i = 0; i < sizeof(kOptionsPages) / sizeof(kOptionsPages[0]); ++i) {
        const auto &page = kOptionsPages[i];
        if (page.parent != OptionsPage::None && options_page_definition(page.parent) == nullptr) return false;
        if (page.next != OptionsPage::None && options_page_definition(page.next) == nullptr) return false;
        if (page.tour_position != 0) {
            ++guides;
            if (page.tour_position > kOnboardingTourPages || page.tour_step == OnboardingTourStep::Completed) return false;
            const auto *next = options_page_definition(page.next);
            if (page.tour_position < kOnboardingTourPages) {
                if (next == nullptr || next->tour_position != page.tour_position + 1U) return false;
            } else if (page.next != OptionsPage::None) return false;
        } else if (page.tour_step != OnboardingTourStep::Completed) return false;
        for (size_t j = 0; j < i; ++j) {
            const auto &other = kOptionsPages[j];
            if (page.identity == other.identity) return false;
            if (page.tour_position != 0 && other.tour_position != 0 &&
                (page.tour_step == other.tour_step || page.tour_position == other.tour_position)) return false;
        }
    }
    return guides == kOnboardingTourPages;
}
static_assert(valid_options_catalog(), "Options and tour identities must be complete and unique");

} // namespace CODY
