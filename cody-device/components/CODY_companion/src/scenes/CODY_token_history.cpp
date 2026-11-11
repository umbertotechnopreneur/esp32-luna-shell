#include "CODY_token_history.hpp"
#include "CODY_title_style.hpp"
#include "CODY_typography.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_ui_components.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "CODY2D_paths.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

using SourceState = TokenHistoryPresentation::SourceState;

constexpr char kTag[] = "CODY_token_history";
constexpr DamageRect kContentBounds{73, 73, 393, 348};
constexpr CODY2D::Rect kControlBounds{123, 359, 220, 50};
constexpr DamageRect kControlDamage{123, 359, 343, 409};
constexpr CODY2D::Rect kChart{139, 161, 244, 126};
constexpr CODY2D::Rect kWaitingIcon{201, 157, 64, 64};
constexpr uint16_t kBaseline = 279;
constexpr uint16_t kPlotHeight = 110;
constexpr std::array<uint16_t, 3> kAxisRows{kBaseline - kPlotHeight,
    kBaseline - kPlotHeight / 2U, kBaseline};
constexpr int64_t kQuarterSeconds = 900;
constexpr uint16_t kWhite = UI::Palette::text;
constexpr uint16_t kMuted = UI::Palette::muted;
constexpr uint16_t kBright = Theme::secondary_bright;
constexpr uint16_t kOldBright = Theme::firmware_token_history_oldbright;
constexpr uint16_t kBars = Theme::firmware_token_history_bars;
constexpr uint16_t kOldBars = Theme::firmware_token_history_oldbars;
constexpr uint16_t kGrid = Theme::firmware_token_history_grid;
constexpr uint16_t kChartBackground = Theme::firmware_token_history_chartbackground;
constexpr uint16_t kGap = Theme::firmware_token_history_gap;
constexpr uint16_t kCyan = UI::Palette::cyan;
constexpr uint16_t kAmber = UI::Palette::warning;

struct Extent {
    uint8_t bins;
    const char *caption;
};

// interval: Supported interval selection, validated by the caller.
Extent extent(TokenHistoryInterval interval)
{
    switch (interval) {
    case TokenHistoryInterval::OneHour: return {4, "Show 1 hour"};
    case TokenHistoryInterval::FiveHours: return {20, "Show 5 hours"};
    case TokenHistoryInterval::TwentyFourHours: return {96, "Show 24 hours"};
    }
    return {0, nullptr};
}

// operation: Failed renderer or layout operation.
// error: Original error returned without substitution.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// snapshot: Canonical interval series and current authenticated lease.
SourceState source_state(const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot)
{
    if (snapshot.initialized && snapshot.data_stale) { return SourceState::Stale; }
    if (snapshot.initialized && snapshot.data_valid && snapshot.health == LUNASHELL::DeviceHealth::Ready &&
        snapshot.last_error == ESP_OK) {
        return SourceState::Current;
    }
    return SourceState::Unavailable;
}

// caption: Persistent text and prepared native glyph line.
// font: Immutable monospace glyph asset.
// text: Complete bounded ASCII text.
// bounds: Admitted logical caption placement.
// color: Native RGB565 text color.
// Exception: Returns original glyph/layout failures without clipping data.
esp_err_t prepare_caption(TokenHistoryPresentation::Caption &caption, const CODY2D::MonoFont &font,
    const char *text, CODY2D::Rect bounds, uint16_t color)
{
    const size_t length = std::strlen(text);
    if (length == 0 || length >= sizeof(caption.text) || font.height > bounds.height) { return ESP_ERR_INVALID_SIZE; }
    uint32_t width = 0;
    const esp_err_t error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) { return error; }
    if (width > bounds.width) { return ESP_ERR_INVALID_SIZE; }
    std::memcpy(caption.text, text, length + 1U);
    return CODY2D::prepare_text_line(font, {caption.text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, caption.line);
}

// presentation: Fixed caption owner.
// font: Immutable glyph asset.
// text: Full text for one prepared row.
// bounds: Shared logical centering and geometry check.
// color: Native RGB565 foreground.
// Exception: Returns original caption preparation error or exhausted capacity.
esp_err_t add_caption(TokenHistoryPresentation &presentation, const CODY2D::MonoFont &font,
    const char *text, CODY2D::Rect bounds, uint16_t color)
{
    if (presentation.caption_count >= presentation.captions.size()) { return ESP_ERR_INVALID_SIZE; }
    auto &caption = presentation.captions[presentation.caption_count];
    if (presentation.caption_count == 0) {
        const size_t length = std::strlen(text);
        if (length == 0 || length >= sizeof(caption.text)) return ESP_ERR_INVALID_SIZE;
        std::memcpy(caption.text, text, length + 1U);
        caption.line.text = {caption.text, static_cast<uint16_t>(length)};
        ++presentation.caption_count;
        return ESP_OK;
    }
    const esp_err_t error = prepare_caption(caption, font, text, bounds, color);
    if (error == ESP_OK) { ++presentation.caption_count; }
    return error;
}

// presentation: Fixed page owner, with no plotted or invented values.
// status: Explicit missing, collecting or stale observation state.
// guidance: Short next step appropriate to the canonical source state.
// Exception: Returns the original caption or capacity error.
esp_err_t prepare_empty_history(TokenHistoryPresentation &presentation,
    const char *status, const char *guidance)
{
    const auto &font = *presentation.body_font;
    const bool stale = presentation.source_state == SourceState::Stale;
    auto error = add_caption(presentation, font, status, {73, 238, 320, 28}, stale ? kAmber : kWhite);
    if (error != ESP_OK) { return error; }
    const CODY2D::Rect guidance_bounds{73, 279, 320, 25};
    const CODY2D::MonoFont *guidance_font = nullptr;
    error = Typography::label_font(guidance, guidance_bounds, font, guidance_font);
    if (error == ESP_OK) error = add_caption(presentation, *guidance_font, guidance, guidance_bounds, kMuted);
    if (error != ESP_OK) { return error; }
    const CODY2D::Rect note_bounds{73, 310, 320, 25};
    const CODY2D::MonoFont *note_font = nullptr;
    error = Typography::label_font("15-minute observations", note_bounds, font, note_font);
    return error == ESP_OK ? add_caption(presentation, *note_font, "15-minute observations", note_bounds, kMuted) : error;
}

// strip: Valid borrowed native RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// bounds: Logical rectangle clipped to the tile.
// color: Native RGB565 fill color.
void fill_clipped(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
    CODY2D::Rect bounds, uint16_t color)
{
    const int left = std::max<int>(first_column, bounds.x);
    const int right = std::min<int>(first_column + strip.width, bounds.x + bounds.width);
    const int top = std::max<int>(first_row, bounds.y);
    const int bottom = std::min<int>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right) { return; }
    const auto stored = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
    for (int y = top; y < bottom; ++y) {
        auto *row = strip.pixels + (y - first_row) * strip.stride + left - first_column;
        std::fill_n(row, right - left, stored);
    }
}

// strip: Valid borrowed native RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
void draw_grid(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    fill_clipped(strip, first_row, first_column, kChart, kChartBackground);
    for (const uint16_t y : kAxisRows) {
        fill_clipped(strip, first_row, first_column, {kChart.x, y, kChart.width, 1}, kGrid);
    }
}

// presentation: Immutable per-bin validity and prepared plot geometry.
// strip: Borrowed tile to receive the histogram and adjacent-sample trace.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves the original native stroke error.
esp_err_t draw_plot(const TokenHistoryPresentation &presentation, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (presentation.bin_count == 0 || first_row >= kChart.y + kChart.height ||
        first_row + strip.height <= kChart.y || first_column >= kChart.x + kChart.width ||
        first_column + strip.width <= kChart.x) { return ESP_OK; }
    draw_grid(strip, first_row, first_column);
    const bool stale = presentation.source_state == SourceState::Stale;
    const uint16_t trace = stale ? kOldBright : kBright;
    const uint16_t histogram = stale ? kOldBars : kBars;
    for (uint8_t index = 0; index < presentation.bin_count; ++index) {
        const auto &bin = presentation.bins[index];
        const auto width = presentation.bar_width;
        const auto x = static_cast<uint16_t>(bin.x - width / 2U);
        if (!bin.valid) {
            if (bin.gap) {
                fill_clipped(strip, first_row, first_column,
                    {x, static_cast<uint16_t>(kBaseline + 4U), width, 2}, kGap);
            }
        } else if (bin.y == kBaseline) {
            // A real zero is a green mark on the baseline. Missing samples are below it.
            fill_clipped(strip, first_row, first_column, {x, kBaseline, width, 2}, trace);
        } else {
            fill_clipped(strip, first_row, first_column,
                {x, bin.y, width, static_cast<uint16_t>(kBaseline - bin.y)}, histogram);
        }
    }
    for (uint8_t index = 0; index < presentation.bin_count; ++index) {
        const auto &bin = presentation.bins[index];
        if (!bin.valid) { continue; }
        const auto *previous = index == 0 ? nullptr : &presentation.bins[index - 1U];
        const auto *next = index + 1U == presentation.bin_count ? nullptr : &presentation.bins[index + 1U];
        const bool joined_before = previous != nullptr && previous->valid;
        const bool joined_after = next != nullptr && next->valid;
        if (!joined_before && joined_after) { continue; } // Its next segment includes this endpoint.
        const CODY2D::Vec2 from{static_cast<float>(bin.x), static_cast<float>(bin.y)};
        CODY2D::Vec2 to = from;
        if (joined_before) { to = {static_cast<float>(previous->x), static_cast<float>(previous->y)}; }
        const int top = static_cast<int>(std::min(from.y, to.y)) - 2;
        const int bottom = static_cast<int>(std::max(from.y, to.y)) + 3;
        if (first_row >= bottom || first_row + strip.height <= top) { continue; }
        const esp_err_t error = CODY2D::stroke_rows(strip, first_row, from, to, 2.f, trace, first_column);
        if (error != ESP_OK) { return error; }
    }
    return ESP_OK;
}

// state: Immutable presentation retained through the tile callback.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves the original tile, line, text or control error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) { return result("draw state", ESP_ERR_INVALID_ARG); }
    const auto &presentation = *static_cast<const TokenHistoryPresentation *>(state);
    if (!presentation.valid) { return result("draw state", ESP_ERR_INVALID_STATE); }
    esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) { return result("draw tile", error); }
    error = draw_plot(presentation, strip, first_row, first_column);
    if (error == ESP_OK && presentation.bin_count == 0) {
        const uint16_t color = presentation.source_state == SourceState::Stale ? kAmber : kCyan;
        error = CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Clock, kWaitingIcon, color,
            strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < presentation.caption_count; ++index) {
        const auto &line = presentation.captions[index].line;
        error = index == 0 ? ESP_OK :
            CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = CODY2D::draw_capsule_rows(presentation.interval_control, strip, first_row, first_column);
    }
    return result("draw rows", error);
}

// presentation: Immutable page owner and interactive interval control.
// content: Receives the central chart and separate action damage slots.
esp_err_t attach_content(const TokenHistoryPresentation &presentation, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.bottom_action_bounds = kControlDamage;
    content.bottom_action_visible = true;
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_rows;
    return prepare_styled_title(presentation.captions[0].line.text, CODY2D::Rect{73, 78, 320, 46}, content.heading, 3, true);
}

// peak: Largest observed value in the selected window, including a real zero.
uint64_t axis_ceiling(uint64_t peak)
{
    if (peak <= 10U) return 10U;
    uint64_t magnitude = 1;
    while (peak / magnitude >= 100U) magnitude *= 10U;
    // Round up to an even increment so the midpoint also has a readable label.
    const uint64_t step = 2U * magnitude;
    const uint64_t steps = peak / step + (peak % step != 0);
    return steps <= std::numeric_limits<uint64_t>::max() / step
        ? steps * step : std::numeric_limits<uint64_t>::max();
}

// value: Token count represented by an axis tick.
// text: Bounded compact number with a decimal magnitude suffix.
void format_count(uint64_t value, char (&text)[24])
{
    constexpr char units[]{' ', 'K', 'M', 'G', 'T', 'P', 'E'};
    unsigned unit = 0;
    uint64_t scale = 1;
    while (value / scale >= 1000U && unit + 1U < sizeof(units)) {
        scale *= 1000U;
        ++unit;
    }
    const auto whole = static_cast<unsigned long long>(value / scale);
    const char decimal = unit == 0 ? '0' : static_cast<char>('0' + value % scale / (scale / 10U));
    if (unit == 0) { std::snprintf(text, sizeof(text), "%llu", whole); }
    else if (decimal == '0') { std::snprintf(text, sizeof(text), "%llu%c", whole, units[unit]); }
    else { std::snprintf(text, sizeof(text), "%llu.%c%c", whole, decimal, units[unit]); }
}

// quarter: Real Unix 15-minute index identifying the start of a source slot.
// label: Bounded UTC calendar stamp, updated only after successful conversion.
bool format_utc(uint32_t quarter, char (&label)[16])
{
    const int64_t epoch = static_cast<int64_t>(quarter) * kQuarterSeconds;
    const auto value = static_cast<std::time_t>(epoch);
    if (epoch <= 0 || static_cast<int64_t>(value) != epoch) { return false; }
    std::tm calendar{};
#ifdef _WIN32
    if (gmtime_s(&calendar, &value) != 0) { return false; }
#else
    if (gmtime_r(&value, &calendar) == nullptr) { return false; }
#endif
    return std::strftime(label, sizeof(label), "%m-%d %H:%M", &calendar) != 0;
}

// calendar: Exact endpoint projected by the canonical time owner.
bool valid_local_endpoint(const LUNASHELL::ClockLocalCalendar &calendar, int64_t epoch)
{
    return calendar.utc_epoch_seconds == epoch && calendar.month >= 1 && calendar.month <= 12 &&
        calendar.day >= 1 && calendar.day <= 31 && calendar.hour <= 23 && calendar.minute <= 59 &&
        calendar.utc_offset_minutes > -1440 && calendar.utc_offset_minutes < 1440;
}

// calendar: Admitted local endpoint for the selected history window.
bool format_local(const LUNASHELL::ClockLocalCalendar &calendar, char (&label)[16])
{
    const int length = std::snprintf(label, sizeof(label), "%02u-%02u %02u:%02u",
        static_cast<unsigned>(calendar.month), static_cast<unsigned>(calendar.day),
        static_cast<unsigned>(calendar.hour), static_cast<unsigned>(calendar.minute));
    return length > 0 && static_cast<size_t>(length) < sizeof(label);
}

// presentation: Persistent bounded output, with admitted interval and source state.
// snapshot: Aged authenticated history containing genuine values and explicit gaps.
// oldest: First Unix 15-minute index in the selected display window.
// count: Number of consecutive slots in the selected interval.
// local: Admitted local endpoints, or null for UTC.
// Exception: Preserves original label/layout errors.
esp_err_t prepare_plot(TokenHistoryPresentation &presentation, const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot,
    uint32_t oldest, uint8_t count, const TokenHistoryLocalRange *local)
{
    uint64_t peak = 0;
    bool measured = false;
    for (uint8_t index = 0; index < count; ++index) {
        uint64_t value = 0;
        if (snapshot.read_token_delta(oldest + index, value)) {
            measured = true;
            peak = std::max(peak, value);
        }
    }
    const auto first_observed = snapshot.first_observed_quarter;
    const bool known_start = first_observed != 0 && first_observed <= snapshot.anchor_quarter;
    const auto &font = *presentation.body_font;
    const bool stale = presentation.source_state == SourceState::Stale;
    if (!measured) {
        // Without a genuine observation there is no plot, including no synthetic zero line.
        const bool collecting = known_start && snapshot.anchor_quarter == first_observed;
        const char *status = stale ? "History is stale" :
            collecting ? "Collecting usage" : "No samples in this range";
        const char *guidance = stale ? "Check Codex on your PC" :
            collecting ? "Waiting for first interval" : "Try a wider time range";
        return prepare_empty_history(presentation, status, guidance);
    }
    const uint64_t ceiling = axis_ceiling(peak);
    presentation.bar_width = static_cast<uint8_t>(std::clamp<unsigned>(kChart.width / count / 2U, 1, 24));
    for (uint8_t index = 0; index < count; ++index) {
        const uint32_t quarter = oldest + index;
        uint64_t value = 0;
        const bool valid = snapshot.read_token_delta(quarter, value);
        const bool gap = !valid && (!known_start || quarter > first_observed);
        uint16_t height = 0;
        if (valid && value > 0) {
            height = std::max<uint16_t>(1,
                static_cast<uint16_t>(static_cast<double>(value) / static_cast<double>(ceiling) * kPlotHeight));
        }
        const auto x = static_cast<uint16_t>(kChart.x + (2U * index + 1U) * kChart.width / (2U * count));
        presentation.bins[index] = {x, static_cast<uint16_t>(kBaseline - height), valid, gap};
    }
    presentation.bin_count = count;

    const char *units = stale ? "STALE ACCOUNT TOKENS" : "ACCOUNT TOKENS / 15 MIN";
    esp_err_t error = add_caption(presentation, font, units, {73, 130, 320, 25}, stale ? kAmber : kMuted);
    if (error != ESP_OK) { return error; }
    const std::array<uint64_t, 3> ticks{ceiling, ceiling / 2U, 0};
    for (size_t index = 0; index < ticks.size(); ++index) {
        char number[24]{};
        format_count(ticks[index], number);
        const auto top = static_cast<uint16_t>(kAxisRows[index] - font.height / 2U);
        error = add_caption(presentation, font, number, {73, top, 63, 25}, kMuted);
        if (error != ESP_OK) { return error; }
    }
    char first[16]{};
    char last[16]{};
    const bool dates_ready = local != nullptr
        ? format_local(local->first, first) && format_local(local->last, last)
        : format_utc(oldest, first) && format_utc(snapshot.anchor_quarter, last);
    if (!dates_ready) {
        return add_caption(presentation, font, "DATES UNAVAILABLE", {73, 292, 320, 25}, kMuted);
    }
    error = add_caption(presentation, font, first, {73, 292, 155, 25}, kMuted);
    if (error != ESP_OK) { return error; }
    return add_caption(presentation, font, last, {238, 292, 155, 25}, kMuted);
}

} // namespace

bool token_history_utc_range(TokenHistoryInterval interval,
    const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot, int64_t &first_epoch, int64_t &last_epoch)
{
    const auto bins = extent(interval).bins;
    if (!snapshot.initialized || bins == 0 || snapshot.anchor_quarter < bins) {
        return false;
    }
    const uint32_t oldest = snapshot.anchor_quarter - bins + 1U;
    first_epoch = static_cast<int64_t>(oldest) * kQuarterSeconds;
    last_epoch = static_cast<int64_t>(snapshot.anchor_quarter) * kQuarterSeconds;
    return true;
}

// interval: Requested display window of consecutive 15-minute bins.
// snapshot: Aged authenticated source, with optional retained stale history.
// title_font: Immutable native headline font.
// body_font: Immutable native dates, legend and action font.
// presentation: Fixed display owner, mutated only after frame retirement.
// content: Receives bounded chart and independent interval action slots.
// local: Both endpoints in the same configured zone, when the model could project them.
// Exception: Preserves original caption, layout or interval validation errors.
esp_err_t prepare_token_history(TokenHistoryInterval interval,
    const LUNASHELL::CodexTokenIntervalsSnapshot &snapshot, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, TokenHistoryPresentation &presentation, SceneContent &content,
    const TokenHistoryLocalRange *local)
{
    const auto selected = extent(interval);
    if (selected.bins == 0) { return result("interval identity", ESP_ERR_INVALID_ARG); }
    const SourceState source = source_state(snapshot);
    int64_t first_epoch = 0;
    int64_t last_epoch = 0;
    const bool has_range = source != SourceState::Unavailable &&
        token_history_utc_range(interval, snapshot, first_epoch, last_epoch);
    const bool local_time = has_range && local != nullptr && local->first.timezone == local->last.timezone &&
        valid_local_endpoint(local->first, first_epoch) && valid_local_endpoint(local->last, last_epoch);
    const int16_t first_offset = local_time ? local->first.utc_offset_minutes : 0;
    const int16_t last_offset = local_time ? local->last.utc_offset_minutes : 0;
    if (presentation.valid && presentation.interval == interval && presentation.source_state == source &&
        presentation.observed_at_us == snapshot.last_success_at_us &&
        presentation.anchor_quarter == snapshot.anchor_quarter &&
        presentation.first_observed_quarter == snapshot.first_observed_quarter &&
        presentation.local_time == local_time && presentation.first_offset_minutes == first_offset &&
        presentation.last_offset_minutes == last_offset &&
        presentation.title_font == &title_font && presentation.body_font == &body_font) {
        return attach_content(presentation, content);
    }
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) {
        return result("view revision", ESP_ERR_INVALID_STATE);
    }
    presentation.valid = false;
    presentation.interval = interval;
    presentation.source_state = source;
    presentation.observed_at_us = snapshot.last_success_at_us;
    presentation.anchor_quarter = snapshot.anchor_quarter;
    presentation.first_observed_quarter = snapshot.first_observed_quarter;
    presentation.local_time = local_time;
    presentation.first_offset_minutes = first_offset;
    presentation.last_offset_minutes = last_offset;
    presentation.title_font = &title_font;
    presentation.body_font = &body_font;
    presentation.caption_count = 0;
    presentation.bin_count = 0;
    esp_err_t error = add_caption(presentation, title_font, "Token activity", {73, 78, 320, 46}, kWhite);
    if (error != ESP_OK) { return result("prepare title", error); }
    if (!has_range) {
        const bool stale = source == SourceState::Stale;
        const bool unavailable = source == SourceState::Unavailable;
        const char *status = stale ? "History is stale" :
            unavailable ? "History unavailable" : "No samples yet";
        const char *guidance = stale || unavailable ? "Check Codex on your PC" : "Keep Codex connected";
        error = prepare_empty_history(presentation, status, guidance);
    } else {
        const auto oldest = static_cast<uint32_t>(first_epoch / kQuarterSeconds);
        error = prepare_plot(presentation, snapshot, oldest, selected.bins, local_time ? local : nullptr);
    }
    if (error != ESP_OK) { return result("prepare chart", error); }
    const bool has_plot = presentation.bin_count != 0;
    if (has_plot) {
        const char *legend = local_time ? "15 MIN LOCAL | GAP: MISSING" : "15 MIN UTC | GAP: MISSING";
        error = add_caption(presentation, body_font, legend, {73, 321, 320, 25}, kMuted);
        if (error != ESP_OK) { return result("prepare legend", error); }
    }

    error = prepare_caption(presentation.interval_label, body_font, selected.caption, kControlBounds, kWhite);
    if (error != ESP_OK) { return result("prepare interval control", error); }
    auto &control = presentation.interval_control;
    control = {};
    control.bounds = kControlBounds;
    control.caption = presentation.interval_label.line;
    control.top_color = Theme::controls_secondary_top;
    control.bottom_color = Theme::controls_secondary_bottom;
    control.border_color = Theme::controls_secondary_border;
    ++presentation.revision;
    presentation.valid = true;
    return attach_content(presentation, content);
}

// presentation: Prepared current token history screen.
// point: Logical touch position shared with the display transform.
bool hit_token_history_interval(const TokenHistoryPresentation &presentation, CODY2D::Point point)
{
    return presentation.valid && CODY2D::capsule_contains(presentation.interval_control, point);
}

} // namespace CODY
