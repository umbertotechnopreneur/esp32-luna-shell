#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "LUNASHELL_clock.hpp"

namespace LUNASHELL { struct CodexTokenIntervalsSnapshot; }

namespace CODY {

enum class TokenHistoryInterval : uint8_t { OneHour, FiveHours, TwentyFourHours };

struct TokenHistoryLocalRange {
    LUNASHELL::ClockLocalCalendar first{};
    LUNASHELL::ClockLocalCalendar last{};
};

// Returns the actual UTC starts of the first and last quarter-hour buckets in the view.
bool token_history_utc_range(TokenHistoryInterval interval,
    const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot, int64_t &first_epoch, int64_t &last_epoch);

// Fixed presentation owned by the display worker through tile retirement.
struct TokenHistoryPresentation {
    struct Bin {
        uint16_t x = 0;
        uint16_t y = 0;
        bool valid = false;
        bool gap = false;
    };
    struct Caption {
        char text[32]{};
        CODY2D::TextLine line{};
    };
    enum class SourceState : uint8_t { Current, Stale, Unavailable };

    std::array<Bin, 96> bins{};
    std::array<Caption, 8> captions{};
    Caption interval_label{};
    CODY2D::CapsuleControl interval_control{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    int64_t observed_at_us = 0;
    uint32_t anchor_quarter = 0;
    uint32_t first_observed_quarter = 0;
    uint32_t revision = 0;
    int16_t first_offset_minutes = 0;
    int16_t last_offset_minutes = 0;
    TokenHistoryInterval interval = TokenHistoryInterval::FiveHours;
    SourceState source_state = SourceState::Unavailable;
    uint8_t bin_count = 0;
    uint8_t caption_count = 0;
    uint8_t bar_width = 0;
    bool local_time = false;
    bool valid = false;
};
static_assert(sizeof(TokenHistoryPresentation) <= 1536,
    "The token history renderer retains only bounded plot geometry and captions");

// interval: Selected time extent, composed of consecutive 15-minute bins.
// snapshot: Aged authenticated per-interval deltas, with explicit missing samples.
// title_font: Immutable native headline font.
// body_font: Immutable native dates, legend and action font.
// presentation: Display-owned output retained until frame retirement.
// content: Receives the chart and separate lower interval action drawing slots.
// local: Both endpoints projected in the same configured zone, or null for UTC.
// Exception: Preserves original native layout errors and rejects an invalid interval.
esp_err_t prepare_token_history(TokenHistoryInterval interval,
    const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, TokenHistoryPresentation &presentation, SceneContent &content,
    const TokenHistoryLocalRange *local = nullptr);

// presentation: Prepared current token history screen.
// point: Logical touch point in the selected display orientation.
bool hit_token_history_interval(const TokenHistoryPresentation &presentation, CODY2D::Point point);

} // namespace CODY
