#include "CODY_token_history.hpp"

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
constexpr CODY2D::Rect kChart{83, 161, 300, 126};
constexpr uint16_t kBaseline = 279;
constexpr uint16_t kPlotHeight = 110;
constexpr int64_t kQuarterSeconds = 900;
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kBright = 0x5FF1;
constexpr uint16_t kOldBright = 0x7C34;
constexpr uint16_t kBars = 0x12CF;
constexpr uint16_t kOldBars = 0x226A;
constexpr uint16_t kGrid = 0x0986;
constexpr uint16_t kChartBackground = 0x0062;
constexpr uint16_t kGap = 0x318A;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kAmber = 0xFE27;

struct Extent {
    uint8_t bins;
    const char *caption;
};

// interval: Supported interval selection, validated by the caller.
Extent extent(TokenHistoryInterval interval)
{
    switch (interval) {
    case TokenHistoryInterval::OneHour: return {4, "1 HOUR"};
    case TokenHistoryInterval::FiveHours: return {20, "5 HOURS"};
    case TokenHistoryInterval::TwentyFourHours: return {96, "24 HOURS"};
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
    const esp_err_t error = prepare_caption(caption, font, text, bounds, color);
    if (error == ESP_OK) { ++presentation.caption_count; }
    return error;
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
    for (uint16_t y = 169; y < kBaseline; y = static_cast<uint16_t>(y + 11U)) {
        fill_clipped(strip, first_row, first_column, {kChart.x, y, kChart.width, 1}, kGrid);
    }
    for (uint16_t x = 98; x < kChart.x + kChart.width; x = static_cast<uint16_t>(x + 15U)) {
        fill_clipped(strip, first_row, first_column, {x, kChart.y, 1, kChart.height}, kGrid);
    }
    fill_clipped(strip, first_row, first_column, {kChart.x, kBaseline, kChart.width, 1}, kGrid);
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
    for (uint8_t index = 0; error == ESP_OK && index < presentation.caption_count; ++index) {
        const auto &line = presentation.captions[index].line;
        error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = CODY2D::draw_capsule_rows(presentation.interval_control, strip, first_row, first_column);
    }
    return result("draw rows", error);
}

// presentation: Immutable page owner and interactive interval control.
// content: Receives the central chart and separate action damage slots.
void attach_content(const TokenHistoryPresentation &presentation, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.bottom_action_bounds = kControlDamage;
    content.bottom_action_visible = true;
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_rows;
}

// value: Actual maximum measured 15-minute token count.
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
    if (unit == 0) { std::snprintf(text, sizeof(text), "%llu", whole); }
    else { std::snprintf(text, sizeof(text), "%llu%c", whole, units[unit]); }
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
        const char *status = stale ? "ACCOUNT SAMPLES ARE STALE" :
            collecting ? "COLLECTING ACCOUNT USAGE" : "NO ACCOUNT SAMPLES IN RANGE";
        esp_err_t error = add_caption(presentation, font, status, {73, 195, 320, 28}, stale ? kAmber : kMuted);
        if (error != ESP_OK) { return error; }
        return add_caption(presentation, font, "ONE POINT EVERY 15 MIN", {73, 235, 320, 28}, kMuted);
    }
    presentation.bar_width = static_cast<uint8_t>(std::clamp<unsigned>(kChart.width / count / 2U, 1, 18));
    for (uint8_t index = 0; index < count; ++index) {
        const uint32_t quarter = oldest + index;
        uint64_t value = 0;
        const bool valid = snapshot.read_token_delta(quarter, value);
        const bool gap = !valid && (!known_start || quarter > first_observed);
        uint16_t height = 0;
        if (valid && value > 0 && peak > 0) {
            height = std::max<uint16_t>(1,
                static_cast<uint16_t>(static_cast<double>(value) / static_cast<double>(peak) * kPlotHeight));
        }
        const auto x = static_cast<uint16_t>(kChart.x + (2U * index + 1U) * kChart.width / (2U * count));
        presentation.bins[index] = {x, static_cast<uint16_t>(kBaseline - height), valid, gap};
    }
    presentation.bin_count = count;

    char message[32]{};
    char number[24]{};
    format_count(peak, number);
    std::snprintf(message, sizeof(message), stale ? "STALE ACCOUNT PEAK %.8s" : "ACCOUNT PEAK %.8s TOKENS", number);
    esp_err_t error = add_caption(presentation, font, message, {73, 130, 320, 25}, stale ? kAmber : kMuted);
    if (error != ESP_OK) { return error; }
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
        attach_content(presentation, content);
        return ESP_OK;
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
    esp_err_t error = add_caption(presentation, title_font, "TOKEN ACTIVITY", {73, 78, 320, 46}, kCyan);
    if (error != ESP_OK) { return result("prepare title", error); }
    if (!has_range) {
        error = add_caption(presentation, body_font, "NO ACCOUNT SAMPLES YET", {73, 212, 320, 28}, kMuted);
    } else {
        const auto oldest = static_cast<uint32_t>(first_epoch / kQuarterSeconds);
        error = prepare_plot(presentation, snapshot, oldest, selected.bins, local_time ? local : nullptr);
    }
    if (error != ESP_OK) { return result("prepare chart", error); }
    const char *legend = presentation.bin_count == 0 ? "15 MIN ACCOUNT OBSERVATIONS" :
        local_time ? "15 MIN LOCAL | GAP: MISSING" : "15 MIN UTC | GAP: MISSING";
    error = add_caption(presentation, body_font, legend, {73, 321, 320, 25}, kMuted);
    if (error != ESP_OK) { return result("prepare legend", error); }

    error = prepare_caption(presentation.interval_label, body_font, selected.caption, kControlBounds, kWhite);
    if (error != ESP_OK) { return result("prepare interval control", error); }
    auto &control = presentation.interval_control;
    control = {};
    control.bounds = kControlBounds;
    control.caption = presentation.interval_label.line;
    control.top_color = 0x226B;
    control.bottom_color = 0x10C6;
    control.border_color = 0x4B9B;
    ++presentation.revision;
    presentation.valid = true;
    attach_content(presentation, content);
    return ESP_OK;
}

// presentation: Prepared current token history screen.
// point: Logical touch position shared with the display transform.
bool hit_token_history_interval(const TokenHistoryPresentation &presentation, CODY2D::Point point)
{
    return presentation.valid && CODY2D::capsule_contains(presentation.interval_control, point);
}

} // namespace CODY
