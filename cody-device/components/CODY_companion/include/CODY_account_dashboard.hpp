#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"

namespace LUNASHELL {
struct ClockSnapshot;
struct CodexAccountSnapshot;
struct CodexSavedSnapshot;
struct CodexTechnicalObservation;
struct CodexWeeklyLimitSnapshot;
}

namespace CODY {

enum class AccountDashboardPage : uint8_t { QuotaWindows, Today, LastSevenDays, Credits, Account };

// One display-owned view retained until its native tile callbacks retire.
struct AccountDashboardPresentation {
    struct Caption {
        char text[32]{};
        CODY2D::TextLine line{};
        uint8_t scale = 1;
    };
    struct Bar {
        CODY2D::Rect bounds{};
        uint16_t top_color = 0;
        uint16_t bottom_color = 0;
    };
    enum class SourceState : uint8_t { Current, Saved, Unavailable, Stale, Elapsed, Invalid, OverLimit };

    std::array<Caption, 20> captions{};
    std::array<Bar, 12> bars{};
    std::array<uint8_t, 32> plan_mask{};
    CODY2D::CapsuleControl account_status_dot{};
    CODY2D::Rect voucher_bounds{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    int64_t account_at_us = 0;
    int64_t weekly_at_us = 0;
    int64_t utc_day = -1;
    uint64_t context_remaining = 0;
    uint64_t context_capacity = 0;
    const char *compaction_label = nullptr;
    uint32_t revision = 0;
    CODY2D::ActionIcon plan_icon = CODY2D::ActionIcon::None;
    uint16_t plan_icon_color = 0;
    AccountDashboardPage page = AccountDashboardPage::QuotaWindows;
    SourceState account_state = SourceState::Unavailable;
    SourceState weekly_state = SourceState::Unavailable;
    SourceState short_state = SourceState::Unavailable;
    uint8_t caption_count = 0;
    uint8_t bar_count = 0;
    bool context_page = false;
    bool plan_mask_valid = false;
    bool valid = false;
};
static_assert(sizeof(AccountDashboardPresentation) <= 2048,
    "Account charts and labels use bounded persistent display storage");

// page: Explicit account page, independent of Cody Home and display navigation.
// account: Aged account-level App Server observation, with independent optional fields.
// weekly: Aged weekly limit observation, independently valid from account history.
// clock: Aged device-clock observation used for calendar day boundaries and elapsed limits.
// title_font: Immutable native headline and hero-value glyphs.
// body_font: Immutable native text for chart dates and detail labels.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central native content without lateral navigation controls.
// saved: Explicit historical source for a disconnected device, or null for live data.
// Exception: Returns the original native text or drawing preparation error.
esp_err_t prepare_account_dashboard(AccountDashboardPage page,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly,
    const LUNASHELL::ClockSnapshot &clock, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, AccountDashboardPresentation &presentation, SceneContent &content,
    const LUNASHELL::CodexSavedSnapshot *saved = nullptr);

// observation: Canonical chat/turn-coherent technical data with independent measurement validity.
// title_font: Immutable native headline and numeric glyphs.
// body_font: Immutable native detail glyphs.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central account-style context page without navigation controls.
// Exception: Preserves original projection, clock and native preparation failures.
esp_err_t prepare_context_dashboard(const LUNASHELL::CodexTechnicalObservation &observation,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AccountDashboardPresentation &presentation, SceneContent &content);

} // namespace CODY
