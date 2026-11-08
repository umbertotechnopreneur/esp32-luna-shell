#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"

namespace LUNASHELL {
struct ClockSnapshot;
struct CodexAccountSnapshot;
struct CodexTechnicalObservation;
struct CodexWeeklyLimitSnapshot;
struct FirmwareBuildMetadata;
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
    enum class SourceState : uint8_t { Current, Unavailable, Stale, Elapsed, Invalid, OverLimit };

    std::array<Caption, 20> captions{};
    std::array<Bar, 12> bars{};
    std::array<CODY2D::CapsuleControl, 2> badges{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    int64_t account_at_us = 0;
    int64_t weekly_at_us = 0;
    int64_t utc_day = -1;
    uint64_t context_remaining = 0;
    uint64_t context_capacity = 0;
    const char *compaction_label = nullptr;
    const char *image_version = nullptr;
    const LUNASHELL::FirmwareBuildMetadata *build_metadata = nullptr;
    uint64_t uptime_seconds = 0;
    uint64_t previous_uptime_seconds = 0;
    uint32_t revision = 0;
    AccountDashboardPage page = AccountDashboardPage::QuotaWindows;
    SourceState account_state = SourceState::Unavailable;
    SourceState weekly_state = SourceState::Unavailable;
    SourceState short_state = SourceState::Unavailable;
    uint8_t caption_count = 0;
    uint8_t bar_count = 0;
    uint8_t badge_count = 0;
    bool context_page = false;
    bool about_page = false;
    bool previous_uptime_valid = false;
    bool valid = false;
};
static_assert(sizeof(AccountDashboardPresentation) <= 2048,
    "Account charts and labels use bounded persistent display storage");

// page: Explicit account page, independent of Cody Home and display navigation.
// account: Aged account-level App Server observation, with independent optional fields.
// weekly: Aged weekly limit observation, independently valid from account history.
// clock: Aged device-clock observation used only for the Today date and elapsed limits.
// title_font: Immutable native headline and hero-value glyphs.
// body_font: Immutable native text for chart dates and detail labels.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central native content without lateral navigation controls.
// Exception: Returns the original native text or drawing preparation error.
esp_err_t prepare_account_dashboard(AccountDashboardPage page,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly,
    const LUNASHELL::ClockSnapshot &clock, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, AccountDashboardPresentation &presentation, SceneContent &content);

// observation: Canonical chat/turn-coherent technical data with independent measurement validity.
// title_font: Immutable native headline and numeric glyphs.
// body_font: Immutable native detail glyphs.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central account-style context page without navigation controls.
// Exception: Preserves original projection, clock and native preparation failures.
esp_err_t prepare_context_dashboard(const LUNASHELL::CodexTechnicalObservation &observation,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AccountDashboardPresentation &presentation, SceneContent &content);

// uptime_seconds: Current monotonic device uptime, provided by its owner.
// previous_uptime_seconds: Last observed uptime from the preceding boot, never an inferred final duration.
// previous_uptime_valid: True only when the retained preceding-boot observation was validated.
// image_version: Immutable running-image version, bounded to the ESP app descriptor's 32-byte field.
// build: Immutable running-image build timestamp and hostname, with their original field states.
// title_font: Immutable native headline glyphs.
// body_font: Immutable native detail glyphs.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central native About page without navigation controls.
// Exception: Preserves native glyph, layout and bounded-text failures.
esp_err_t prepare_about_dashboard(uint64_t uptime_seconds, uint64_t previous_uptime_seconds,
    bool previous_uptime_valid, const char *image_version, const LUNASHELL::FirmwareBuildMetadata &build,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AccountDashboardPresentation &presentation, SceneContent &content);

} // namespace CODY
