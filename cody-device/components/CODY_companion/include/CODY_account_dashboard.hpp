#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_chat_title_view.hpp"
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

// observation: Canonical chat/turn-coherent technical projection.
// now_us: Monotonic time for the original source lease.
bool context_dashboard_has_data(const LUNASHELL::CodexTechnicalObservation &observation, int64_t now_us);

// Three visible dates from the same admitted live account. A zero date means no expiry.
struct VoucherList {
    static constexpr uint8_t kVisibleRows = 3;
    std::array<uint32_t, kVisibleRows> expiries{};
    uint64_t series = 0;
    uint8_t first = 0;
    uint8_t count = 0;
    bool current = false;
    bool stale = false;

    // rows: Number of reported dates, including separate vouchers with the same expiry.
    static constexpr uint8_t last_offset(uint8_t rows)
    {
        // A short list still lets the user bring its next entry to the first row.
        return rows > kVisibleRows ? rows - kVisibleRows : (rows > 1 ? 1 : 0);
    }

    // other: Last retired expiry window for this account.
    bool operator==(const VoucherList &other) const
    {
        return expiries == other.expiries && series == other.series && first == other.first &&
            count == other.count && current == other.current && stale == other.stale;
    }
};

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
    // Credits uses two title marks, one divider, eight ticket strokes and two scroll marks.
    std::array<Bar, 13> bars{};
    std::array<uint8_t, 32> plan_mask{};
    CODY2D::CapsuleControl account_status_dot{};
    CODY2D::Rect voucher_bounds{};
    ChatTitleView chat_title{};
    VoucherList voucher_list{};
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
    int8_t voucher_timezone = -1;
    bool voucher_offset_valid = false;
    int16_t voucher_offset_minutes = 0;
    bool context_page = false;
    bool plan_mask_valid = false;
    bool valid = false;
};
static_assert(sizeof(AccountDashboardPresentation) <= 2560,
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
// vouchers: Optional live expiry window for Credits; never sourced from saved data.
// Exception: Returns the original native text or drawing preparation error.
esp_err_t prepare_account_dashboard(AccountDashboardPage page,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly,
    const LUNASHELL::ClockSnapshot &clock, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, AccountDashboardPresentation &presentation, SceneContent &content,
    const LUNASHELL::CodexSavedSnapshot *saved = nullptr, const VoucherList &vouchers = {});

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
