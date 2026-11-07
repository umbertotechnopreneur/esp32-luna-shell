#include "CODY_account_dashboard.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

using SourceState = AccountDashboardPresentation::SourceState;
using Page = AccountDashboardPage;

constexpr char kTag[] = "CODY_account_dashboard";
constexpr DamageRect kContentBounds{73, 73, 393, 393};
constexpr CODY2D::Rect kHeadline{73, 79, 320, 46};
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kIndigo = 0x549E;
constexpr uint16_t kGold = 0xFE27;
constexpr uint16_t kGreen = 0x4EE8;
constexpr uint16_t kDivider = 0x39F1;
constexpr uint16_t kDarkTrack = 0x2127;
constexpr uint16_t kChartBaseline = 318;
constexpr uint16_t kChartHeight = 100;
constexpr uint16_t kChartLeft = 79;
constexpr uint16_t kChartStep = 44;
constexpr size_t kDayCount = 7;
constexpr int64_t kDaySeconds = 86400;

struct QuotaWindow {
    const char *label;
    SourceState state;
    uint16_t remaining;
    uint16_t top;
    uint16_t color;
};

// operation: Failed account layout or raster operation.
// error: Original native failure returned unchanged.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// observation: Aged canonical source and its original success/failure state.
SourceState source_state(const LUNASHELL::ComponentSnapshot &observation)
{
    if (observation.data_stale) { return SourceState::Stale; }
    return observation.data_valid && observation.health == LUNASHELL::DeviceHealth::Ready &&
        observation.last_error == ESP_OK ? SourceState::Current : SourceState::Unavailable;
}

// state: Visible status for an unavailable or expired metric.
const char *status_text(SourceState state)
{
    switch (state) {
    case SourceState::Stale: return "Stale";
    case SourceState::Elapsed: return "Reset pending";
    default: return "Unavailable";
    }
}

// epoch: Positive Unix timestamp supplied by the authenticated source.
// calendar: Receives the actual UTC calendar projection on success.
bool utc_calendar(int64_t epoch, std::tm &calendar)
{
    const auto value = static_cast<std::time_t>(epoch);
    if (epoch <= 0 || static_cast<int64_t>(value) != epoch) { return false; }
#ifdef _WIN32
    return gmtime_s(&calendar, &value) == 0;
#else
    return gmtime_r(&value, &calendar) != nullptr;
#endif
}

// presentation: Fixed display owner of the copied text.
// font: Immutable native text asset.
// text: Complete bounded ASCII text.
// bounds: Logical rectangle used to center the caption.
// color: Native RGB565 foreground.
// scale: Integer glyph enlargement admitted within the supplied logical bounds.
// Exception: Preserves native glyph errors or reports exhausted/invalid geometry.
esp_err_t add_caption(AccountDashboardPresentation &presentation, const CODY2D::MonoFont &font,
    const char *text, CODY2D::Rect bounds, uint16_t color, uint8_t scale = 1)
{
    const size_t length = std::strlen(text);
    if (presentation.caption_count >= presentation.captions.size() || length == 0 ||
        length >= sizeof(presentation.captions[0].text) || scale == 0 || font.height * scale > bounds.height) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t width = 0;
    esp_err_t error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) { return error; }
    if (width * scale > bounds.width) { return ESP_ERR_INVALID_SIZE; }
    auto &caption = presentation.captions[presentation.caption_count];
    std::memcpy(caption.text, text, length + 1U);
    caption.scale = scale;
    error = CODY2D::prepare_text_line(font, {caption.text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(bounds.x + (bounds.width - width * scale) / 2U),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height * scale) / 2U)}, color, caption.line);
    if (error == ESP_OK) { ++presentation.caption_count; }
    return error;
}

// presentation: Prepared page owner retaining this label.
// text: Bounded device caption.
// top: First row of the 25-pixel caption box.
// color: Native RGB565 foreground.
// Exception: Preserves the original caption preparation error.
esp_err_t add_detail(AccountDashboardPresentation &presentation, const char *text, uint16_t top,
    uint16_t color = kMuted)
{
    return add_caption(presentation, *presentation.body_font, text, {73, top, 320, 25}, color);
}

// presentation: Prepared page owner retaining the headline or numeric value.
// text: Complete bounded ASCII display value.
// top: First row of the 42-pixel value box.
// color: Native RGB565 foreground.
// Exception: Preserves unsupported glyph, layout or buffer errors.
esp_err_t add_value(AccountDashboardPresentation &presentation, const char *text, uint16_t top,
    uint16_t color = kWhite)
{
    const auto &hero = *presentation.title_font;
    const auto &detail = *presentation.body_font;
    const auto length = static_cast<uint16_t>(std::strlen(text));
    uint32_t width = 0;
    const esp_err_t error = CODY2D::measure_text_width(hero, {text, length}, width);
    if (error != ESP_OK) { return error; }
    return add_caption(presentation, width <= 320 && hero.height <= 42 ? hero : detail,
        text, {73, top, 320, 42}, color);
}

// presentation: Prepared page owner.
// text: Full glyph string, bounded before any enlargement.
// bounds: Readout rectangle, with clearance from its labels and lateral controls.
// preferred_scale: Authored enlargement for the hero amount.
// color: Native RGB565 foreground.
// Exception: Preserves original native glyph or layout errors.
esp_err_t add_hero(AccountDashboardPresentation &presentation, const char *text,
    CODY2D::Rect bounds, uint8_t preferred_scale, uint16_t color)
{
    const auto &font = *presentation.title_font;
    const auto length = static_cast<uint16_t>(std::strlen(text));
    uint32_t width = 0;
    const esp_err_t error = CODY2D::measure_text_width(font, {text, length}, width);
    if (error != ESP_OK) { return error; }
    uint8_t scale = preferred_scale;
    while (scale > 1 && (width * scale > bounds.width || font.height * scale > bounds.height)) { --scale; }
    if (width <= bounds.width && font.height <= bounds.height) {
        return add_caption(presentation, font, text, bounds, color, scale);
    }
    return add_caption(presentation, *presentation.body_font, text, bounds, color);
}

// presentation: Fixed owner of chart and quota bar geometry.
// bounds: Nonempty admitted native rectangle.
// top: Gradient color on the first row.
// bottom: Gradient color on the last row.
// Exception: Rejects excess bars or an invalid rectangle.
esp_err_t add_bar(AccountDashboardPresentation &presentation, CODY2D::Rect bounds,
    uint16_t top, uint16_t bottom)
{
    if (presentation.bar_count >= presentation.bars.size() || bounds.width == 0 || bounds.height == 0 ||
        bounds.x < kContentBounds.left || bounds.y < kContentBounds.top ||
        bounds.x + bounds.width > kContentBounds.right || bounds.y + bounds.height > kContentBounds.bottom) {
        return ESP_ERR_INVALID_SIZE;
    }
    presentation.bars[presentation.bar_count++] = {bounds, top, bottom};
    return ESP_OK;
}

// presentation: Fixed owner of noninteractive decorative geometry.
// y: Logical row shared by both pages separated by this rule.
// Exception: Preserves an invalid geometry or bounded capacity error.
esp_err_t add_rule(AccountDashboardPresentation &presentation, uint16_t y)
{
    return add_bar(presentation, {94, y, 278, 2}, kDivider, kDivider);
}

// value: Actual count. Compact units use decimal powers and keep the integer observation unchanged.
// text: Fixed output buffer for the displayed quantity.
void format_count(uint64_t value, char (&text)[32])
{
    constexpr char units[]{' ', 'K', 'M', 'G', 'T', 'P', 'E'};
    unsigned unit = 0;
    uint64_t scale = 1;
    while (value / scale >= 1000U && unit + 1 < sizeof(units)) {
        scale *= 1000U;
        ++unit;
    }
    const auto whole = static_cast<unsigned long long>(value / scale);
    const unsigned decimal = unit == 0 ? 0 : static_cast<unsigned>(value % scale / (scale / 10U));
    if (unit == 0) { std::snprintf(text, sizeof(text), "%llu", whole); }
    else if (decimal == 0) { std::snprintf(text, sizeof(text), "%llu%c", whole, units[unit]); }
    else { std::snprintf(text, sizeof(text), "%llu.%u%c", whole, decimal, units[unit]); }
}

// basis_points: Exact reported remaining limit, validated against 10000.
// text: Receives a bounded decimal percent label.
void format_percent(uint16_t basis_points, char (&text)[32])
{
    const unsigned whole = basis_points / 100U;
    const unsigned fraction = basis_points % 100U;
    if (fraction == 0) { std::snprintf(text, sizeof(text), "%u%%", whole); }
    else { std::snprintf(text, sizeof(text), "%u.%02u%%", whole, fraction); }
}

// presentation: Fixed quota page owner.
// window: One independently available allowance window.
// Exception: Preserves any original native formatting or layout failure.
esp_err_t add_quota(AccountDashboardPresentation &presentation, const QuotaWindow &window)
{
    const bool available = window.state == SourceState::Current;
    char text[32]{};
    if (available) { format_percent(window.remaining, text); }
    esp_err_t error = add_detail(presentation, window.label, window.top, kGold);
    if (error != ESP_OK) { return error; }
    const auto value_top = static_cast<uint16_t>(window.top + 25U);
    error = add_hero(presentation, available ? text : status_text(window.state),
        {73, value_top, 320, 80}, available ? 2 : 1, available ? kWhite : kMuted);
    if (error != ESP_OK || !available) { return error; }

    const uint16_t y = static_cast<uint16_t>(window.top + 105U);
    constexpr uint16_t width = 280;
    error = add_bar(presentation, {93, y, width, 7}, kDarkTrack, kDarkTrack);
    if (error != ESP_OK) { return error; }
    const auto filled = static_cast<uint16_t>(static_cast<uint32_t>(width) * window.remaining / 10000U);
    return filled == 0 ? ESP_OK : add_bar(presentation, {93, y, filled, 7}, window.color, window.color);
}

// presentation: Fixed quota page owner.
// account: Canonical short-window fields and transport state.
// weekly: Canonical weekly-window fields and transport state.
// Exception: Preserves native rendering or formatting errors.
esp_err_t prepare_quota(AccountDashboardPresentation &presentation,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly)
{
    char label[32] = "SHORT WINDOW";
    if (presentation.short_state == SourceState::Current || presentation.short_state == SourceState::Elapsed) {
        const unsigned minutes = account.short_window_minutes;
        if (minutes < 60U) { std::snprintf(label, sizeof(label), "%u MINUTE WINDOW", minutes); }
        else if (minutes % 60U == 0) { std::snprintf(label, sizeof(label), "%u HOUR WINDOW", minutes / 60U); }
        else { std::snprintf(label, sizeof(label), "%uH %uM WINDOW", minutes / 60U, minutes % 60U); }
    }
    esp_err_t error = add_rule(presentation, 125);
    if (error != ESP_OK) { return error; }
    error = add_quota(presentation, {label, presentation.short_state,
        account.short_remaining_basis_points, 129, kCyan});
    if (error != ESP_OK) { return error; }
    error = add_rule(presentation, 245);
    if (error != ESP_OK) { return error; }
    error = add_quota(presentation, {"WEEKLY WINDOW", presentation.weekly_state,
        weekly.remaining_basis_points, 249, kIndigo});
    if (error != ESP_OK) { return error; }
    return add_detail(presentation, "REMAINING", 366, kCyan);
}

// presentation: Fixed dashboard owner.
// label: Full English streak caption.
// valid: True only when the provider reported this independent count.
// days: Original provider streak length.
// top: Logical y of the label box.
// color: Distinguishes the current and historical streak.
// Exception: Preserves original native text and layout errors.
esp_err_t add_streak(AccountDashboardPresentation &presentation, const char *label,
    bool valid, uint32_t days, uint16_t top, uint16_t color)
{
    char value[32]{};
    if (valid) { std::snprintf(value, sizeof(value), "%lu DAY %s", static_cast<unsigned long>(days), label); }
    else { std::snprintf(value, sizeof(value), "%s %s", label, status_text(presentation.account_state)); }
    return add_detail(presentation, value, top, valid ? color : kMuted);
}

// presentation: Fixed current-day page owner.
// account: Provider's literal dated buckets and independent streak fields.
// Exception: Preserves original native glyph and layout errors.
esp_err_t prepare_today(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    const bool current = presentation.account_state == SourceState::Current;
    const bool today = current && presentation.utc_day > 0 &&
        static_cast<int64_t>(account.anchor_unix_day) == presentation.utc_day && (account.daily_valid_mask & 0x40U);
    char count[32]{};
    if (today) { format_count(account.daily_tokens[kDayCount - 1U], count); }
    const char *missing = current && presentation.utc_day <= 0
        ? "Device time unavailable" : status_text(presentation.account_state);
    esp_err_t error = add_rule(presentation, 128);
    if (error != ESP_OK) { return error; }
    error = add_hero(presentation, today ? count : missing, {73, 133, 320, 120}, today ? 3 : 1,
        today ? kWhite : kMuted);
    if (error != ESP_OK) { return error; }
    error = add_detail(presentation, "TOKENS", 254, kWhite);
    if (error != ESP_OK) { return error; }
    error = add_rule(presentation, 287);
    if (error != ESP_OK) { return error; }
    error = add_streak(presentation, "STREAK", current && account.current_streak_valid,
        account.current_streak_days, 303, kCyan);
    if (error != ESP_OK) { return error; }
    return add_streak(presentation, "BEST", current && account.longest_streak_valid,
        account.longest_streak_days, 344, kIndigo);
}

// value: Original count for this observed day.
// maximum: Maximum observed count in the same seven visible buckets.
uint16_t chart_height(uint64_t value, uint64_t maximum)
{
    if (value == 0 || maximum == 0) { return 0; }
    const auto height = static_cast<uint16_t>(static_cast<double>(value) / static_cast<double>(maximum) * kChartHeight);
    return std::max<uint16_t>(1, height);
}

// presentation: Fixed chart owner.
// account: Seven consecutive provider date slots, including any unavailable days.
// maximum: Maximum actually observed daily count.
// Exception: Returns original chart geometry or glyph preparation errors.
esp_err_t prepare_chart(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account,
    uint64_t maximum)
{
    constexpr const char *weekday[]{"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    const uint32_t first_day = account.anchor_unix_day - (kDayCount - 1U);
    esp_err_t error = add_bar(presentation, {kChartLeft, kChartBaseline, 308, 2}, kDarkTrack, kDarkTrack);
    if (error != ESP_OK) { return error; }
    for (size_t index = 0; index < kDayCount; ++index) {
        const auto x = static_cast<uint16_t>(kChartLeft + index * kChartStep);
        const bool valid = account.daily_valid_mask & (1U << index);
        if (valid) {
            const uint16_t height = chart_height(account.daily_tokens[index], maximum);
            if (height > 0) {
                const bool newest = index == kDayCount - 1U;
                error = add_bar(presentation,
                    {static_cast<uint16_t>(x + 8U), static_cast<uint16_t>(kChartBaseline - height), 28, height},
                    newest ? kCyan : kIndigo, newest ? 0x4DF9 : 0x4435);
            }
        } else {
            error = add_caption(presentation, *presentation.body_font, "--", {x, 282, kChartStep, 30}, kMuted);
        }
        if (error != ESP_OK) { return error; }
        const char *label = weekday[(first_day + index) % kDayCount];
        error = add_caption(presentation, *presentation.body_font, label, {x, 323, kChartStep, 26}, kMuted);
        if (error != ESP_OK) { return error; }
    }
    std::tm first{};
    std::tm last{};
    if (!utc_calendar(first_day * kDaySeconds, first) || !utc_calendar(account.anchor_unix_day * kDaySeconds, last)) {
        return add_detail(presentation, "Bucket dates unavailable", 352);
    }
    char start[12]{};
    char end[12]{};
    if (std::strftime(start, sizeof(start), "%Y-%m-%d", &first) == 0 ||
        std::strftime(end, sizeof(end), "%Y-%m-%d", &last) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    char range[32]{};
    std::snprintf(range, sizeof(range), "%s to %s", start, end);
    error = add_rule(presentation, 351);
    if (error != ESP_OK) { return error; }
    return add_detail(presentation, range, 357);
}

// presentation: Fixed seven-day page owner.
// account: Dated, independently valid provider buckets.
// Exception: Preserves the original native text or chart error.
esp_err_t prepare_history(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    esp_err_t error = add_rule(presentation, 126);
    if (error != ESP_OK) { return error; }
    if (presentation.account_state != SourceState::Current) {
        return add_value(presentation, status_text(presentation.account_state), 185, kMuted);
    }
    if (account.anchor_unix_day <= kDayCount - 1U || account.daily_valid_mask == 0) {
        return add_value(presentation, "No daily buckets", 185, kMuted);
    }
    uint64_t total = 0;
    uint64_t maximum = 0;
    bool complete = account.daily_valid_mask == 0x7FU;
    for (size_t index = 0; index < kDayCount; ++index) {
        if (!(account.daily_valid_mask & (1U << index))) { continue; }
        const uint64_t value = account.daily_tokens[index];
        maximum = std::max(maximum, value);
        if (std::numeric_limits<uint64_t>::max() - total < value) { complete = false; }
        if (complete) { total += value; }
    }
    char count[32]{};
    if (complete) { format_count(total, count); }
    if (complete) {
        error = add_hero(presentation, count, {82, 133, 198, 80}, 2, kCyan);
        if (error != ESP_OK) { return error; }
        error = add_caption(presentation, *presentation.body_font, "TOKENS", {280, 133, 104, 80}, kWhite);
    } else {
        error = add_value(presentation, "Unavailable", 139, kMuted);
        if (error != ESP_OK) { return error; }
        error = add_detail(presentation, "Seven-day total", 181);
    }
    if (error != ESP_OK) { return error; }
    return prepare_chart(presentation, account, maximum);
}

// value: Exact nonnegative balance in thousandths of one credit.
// text: Receives a bounded decimal representation.
void format_credits(uint64_t value, char (&text)[32])
{
    const auto whole = static_cast<unsigned long long>(value / 1000U);
    const unsigned fraction = static_cast<unsigned>(value % 1000U);
    if (fraction % 10U == 0) { std::snprintf(text, sizeof(text), "%llu.%02u", whole, fraction / 10U); }
    else { std::snprintf(text, sizeof(text), "%llu.%03u", whole, fraction); }
}

// presentation: Fixed credits page owner.
// account: Independent reported credit kind, amount and reset vouchers.
// Exception: Preserves original native label or formatting errors.
esp_err_t prepare_credits(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    const bool current = presentation.account_state == SourceState::Current;
    const bool finite = current && account.credits_kind == LUNASHELL::CodexCreditKind::Finite;
    const bool unlimited = current && account.credits_kind == LUNASHELL::CodexCreditKind::Unlimited;
    char credits[32]{};
    if (finite) { format_credits(account.credit_milliunits, credits); }
    const char *balance = unlimited ? "Unlimited" : finite ? credits : status_text(presentation.account_state);
    esp_err_t error = add_bar(presentation, {99, 102, 44, 2}, kCyan, kCyan);
    if (error != ESP_OK) { return error; }
    error = add_bar(presentation, {323, 102, 44, 2}, kCyan, kCyan);
    if (error != ESP_OK) { return error; }
    error = add_detail(presentation, "BALANCE", 139, kWhite);
    if (error != ESP_OK) { return error; }
    error = add_hero(presentation, balance, {73, 165, 320, 80}, finite ? 2 : 1,
        finite || unlimited ? kCyan : kMuted);
    if (error != ESP_OK) { return error; }
    error = add_rule(presentation, 264);
    if (error != ESP_OK) { return error; }
    const bool vouchers = current && account.reset_vouchers_valid;
    char count[32]{};
    if (vouchers) { format_count(account.reset_vouchers, count); }
    error = add_detail(presentation, "RESET VOUCHERS", 284, kWhite);
    if (error != ESP_OK) { return error; }
    return add_value(presentation, vouchers ? count : status_text(presentation.account_state), 314,
        vouchers ? kGold : kMuted);
}

// presentation: Fixed account page owner.
// account: Authenticated ChatGPT account metadata without an email or account identifier.
// Exception: Returns the original native text or geometry error.
esp_err_t prepare_account(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    const bool current = presentation.account_state == SourceState::Current;
    const char *const end = account.plan + sizeof(account.plan);
    const char *const terminator = std::find(account.plan, end, '\0');
    const bool have_plan = current && terminator != account.plan && terminator != end &&
        std::all_of(account.plan, terminator, [](char letter) { return letter >= 32 && letter <= 126; });
    const char *missing = status_text(presentation.account_state);
    char plan[sizeof(account.plan)]{};
    if (have_plan) {
        const size_t length = static_cast<size_t>(terminator - account.plan);
        for (size_t index = 0; index < length; ++index) {
            const char letter = account.plan[index];
            plan[index] = letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
        }
        auto &badge = presentation.badges[presentation.badge_count++];
        badge = {};
        badge.bounds = {91, 144, 284, 66};
        badge.corner_radius = 22;
        badge.border_color = kIndigo;
        badge.top_color = 0x0023;
        badge.bottom_color = 0x0022;
    }
    esp_err_t error = add_hero(presentation, have_plan ? plan : missing, {97, 147, 272, 60}, 1,
        have_plan ? kWhite : kMuted);
    if (error != ESP_OK) { return error; }
    if (current) {
        auto &dot = presentation.badges[presentation.badge_count++];
        dot = {};
        dot.bounds = {117, 228, 10, 10};
        dot.top_color = kGreen;
        dot.bottom_color = kGreen;
    }
    error = add_detail(presentation, current ? "CHATGPT CONNECTED" : missing, 220, current ? kWhite : kMuted);
    if (error != ESP_OK) { return error; }
    error = add_rule(presentation, 260);
    if (error != ESP_OK) { return error; }
    error = add_detail(presentation, "WORKSPACE MESSAGES", 279, kCyan);
    if (error != ESP_OK) { return error; }
    const bool messages = current && account.workspace_messages_valid;
    char count[32]{};
    if (messages) { format_count(account.workspace_messages, count); }
    const char *label = messages ? (account.workspace_messages == 0 ? "NO ACTIVE MESSAGES" : count) : missing;
    error = add_value(presentation, label, 311, messages ? kWhite : kMuted);
    if (error != ESP_OK) { return error; }
    return add_rule(presentation, 365);
}

// bar: Prepared logical chart or allowance bar.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
void draw_bar(const AccountDashboardPresentation::Bar &bar, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto bounds = bar.bounds;
    const int left = std::max<int>(first_column, bounds.x);
    const int right = std::min<int>(first_column + strip.width, bounds.x + bounds.width);
    const int top = std::max<int>(first_row, bounds.y);
    const int bottom = std::min<int>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right) { return; }
    for (int y = top; y < bottom; ++y) {
        const auto level = static_cast<uint8_t>(bar.top_color == bar.bottom_color || bounds.height == 1
            ? 0 : (y - bounds.y) * 255 / (bounds.height - 1));
        const auto native = CODY2D::blend_rgb565(bar.bottom_color, bar.top_color, level);
        const auto stored = CODY2D::rgb565_native_to_stored(native, strip.byte_order);
        auto *row = strip.pixels + (y - first_row) * strip.stride + left - first_column;
        std::fill_n(row, right - left, stored);
    }
}

// caption: Prepared native glyphs with a logical integer enlargement.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Preserves the native source glyph raster error.
esp_err_t draw_scaled_caption(const AccountDashboardPresentation::Caption &caption,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto &line = caption.line;
    const int scale = caption.scale;
    const int origin_x = line.origin.x;
    const int origin_y = line.origin.y;
    const int left = std::max<int>(first_column, origin_x);
    const int right = std::min<int>(first_column + strip.width, origin_x + line.bounds.width * scale);
    const int top = std::max<int>(first_row, origin_y);
    const int bottom = std::min<int>(first_row + strip.height, origin_y + line.bounds.height * scale);
    if (left >= right || top >= bottom) { return ESP_OK; }

    // Reuse the native glyph raster as an alpha row. Enlarged lines are at
    // least 2x in the 320-pixel square, so the only scratch is 320 bytes.
    uint16_t alpha[160];
    const auto width = line.bounds.width;
    if (width > sizeof(alpha) / sizeof(alpha[0])) { return ESP_ERR_INVALID_SIZE; }
    const CODY2D::Surface source{alpha, width, 1, width, 160};
    auto mask = line;
    mask.color = 0xFFFF;
    int previous_row = -1;
    for (int y = top; y < bottom; ++y) {
        const int row = (y - origin_y) / scale;
        if (row != previous_row) {
            std::fill_n(alpha, width, 0);
            const esp_err_t error = CODY2D::draw_text_rows(mask, mask.text.length, source,
                static_cast<uint16_t>(origin_y + row), static_cast<uint16_t>(origin_x));
            if (error != ESP_OK) { return error; }
            previous_row = row;
        }
        auto *pixel = strip.pixels + (y - first_row) * strip.stride + left - first_column;
        for (int x = left; x < right; ++x, ++pixel) {
            const unsigned level = (alpha[(x - origin_x) / scale] >> 5U) & 63U;
            if (level == 0) { continue; }
            const auto background = CODY2D::rgb565_stored_to_native(*pixel, strip.byte_order);
            const auto color = CODY2D::blend_rgb565(line.color, background,
                static_cast<uint8_t>((level * 255U + 31U) / 63U));
            *pixel = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
    return ESP_OK;
}

// state: Immutable presentation borrowed until the display frame retires.
// strip: Borrowed native tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the original native surface or text raster failure.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) { return result("draw state", ESP_ERR_INVALID_ARG); }
    const auto &presentation = *static_cast<const AccountDashboardPresentation *>(state);
    if (!presentation.valid) { return result("draw state", ESP_ERR_INVALID_STATE); }
    esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) { return result("draw tile", error); }
    for (uint8_t index = 0; index < presentation.bar_count; ++index) {
        draw_bar(presentation.bars[index], strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < presentation.badge_count; ++index) {
        error = CODY2D::draw_capsule_rows(presentation.badges[index], strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < presentation.caption_count; ++index) {
        const auto &caption = presentation.captions[index];
        error = caption.scale > 1 ? draw_scaled_caption(caption, strip, first_row, first_column) :
            CODY2D::draw_text_rows(caption.line, caption.line.text.length, strip, first_row, first_column);
    }
    return result("draw rows", error);
}

// presentation: Prepared central page retained through raster retirement.
// content: Receives the native draw callback and bounded revision.
void attach_content(const AccountDashboardPresentation &presentation, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_rows;
}

// source: Canonical transport/source state for this allowance.
// reset: Optional positive App Server reset timestamp in Unix seconds.
// now: Current canonical clock seconds or zero when unavailable.
SourceState quota_state(SourceState source, int64_t reset, int64_t now)
{
    return source == SourceState::Current && reset > 0 && now >= reset ? SourceState::Elapsed : source;
}

} // namespace

// page: Explicit account page independent of device navigation.
// account: Aged account-level App Server observation with independent optional fields.
// weekly: Aged weekly allowance, separate from the other observations.
// clock: Aged device clock used for the Today bucket and elapsed limits.
// title_font: Immutable headline and numeric glyphs.
// body_font: Immutable date and detail glyphs.
// presentation: Persistent owner, mutated only after the prior frame retires.
// content: Receives the central draw callback without lateral navigation.
// Exception: Returns the original native preparation failure.
esp_err_t prepare_account_dashboard(AccountDashboardPage page,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly,
    const LUNASHELL::ClockSnapshot &clock, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, AccountDashboardPresentation &presentation, SceneContent &content)
{
    if (page > Page::Account) { return result("page identity", ESP_ERR_INVALID_ARG); }
    const SourceState account_state = source_state(account);
    const int64_t now = source_state(clock) == SourceState::Current && clock.utc_epoch_seconds > 0
        ? clock.utc_epoch_seconds : 0;
    const int64_t utc_day = page == Page::Today && now > 0 ? now / kDaySeconds : -1;
    SourceState weekly_state = SourceState::Unavailable;
    SourceState short_state = SourceState::Unavailable;
    if (page == Page::QuotaWindows) {
        weekly_state = quota_state(source_state(weekly), weekly.reset_unix_seconds, now);
        short_state = quota_state(account_state, account.short_reset_unix_seconds, now);
        if (weekly_state == SourceState::Current && weekly.remaining_basis_points > 10000U) {
            weekly_state = SourceState::Unavailable;
        }
        if (short_state == SourceState::Current &&
            (account.short_window_minutes == 0 || account.short_remaining_basis_points > 10000U)) {
            short_state = SourceState::Unavailable;
        }
    }
    const int64_t account_at_us = account_state == SourceState::Current ? account.last_success_at_us : 0;
    const int64_t weekly_at_us = page == Page::QuotaWindows ? weekly.last_success_at_us : 0;
    if (presentation.valid && presentation.page == page && presentation.account_state == account_state &&
        presentation.weekly_state == weekly_state && presentation.short_state == short_state &&
        presentation.account_at_us == account_at_us && presentation.weekly_at_us == weekly_at_us &&
        presentation.utc_day == utc_day && presentation.title_font == &title_font && presentation.body_font == &body_font) {
        attach_content(presentation, content);
        return ESP_OK;
    }
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) {
        return result("page revision", ESP_ERR_INVALID_STATE);
    }
    presentation.valid = false;
    presentation.caption_count = 0;
    presentation.bar_count = 0;
    presentation.badge_count = 0;
    presentation.page = page;
    presentation.account_state = account_state;
    presentation.weekly_state = weekly_state;
    presentation.short_state = short_state;
    presentation.account_at_us = account_at_us;
    presentation.weekly_at_us = weekly_at_us;
    presentation.utc_day = utc_day;
    presentation.title_font = &title_font;
    presentation.body_font = &body_font;

    const char *headline = nullptr;
    uint16_t headline_color = kWhite;
    switch (page) {
    case Page::QuotaWindows: headline = "CODEX LIMITS"; break;
    case Page::Today: headline = "TODAY"; headline_color = kCyan; break;
    case Page::LastSevenDays: headline = "LAST 7 DAYS"; break;
    case Page::Credits: headline = "CREDITS"; headline_color = kCyan; break;
    case Page::Account: headline = "CODEX ACCOUNT"; headline_color = kCyan; break;
    }
    esp_err_t error = add_caption(presentation, title_font, headline, kHeadline, headline_color);
    if (error != ESP_OK) { return result("page headline", error); }
    switch (page) {
    case Page::QuotaWindows: error = prepare_quota(presentation, account, weekly); break;
    case Page::Today: error = prepare_today(presentation, account); break;
    case Page::LastSevenDays: error = prepare_history(presentation, account); break;
    case Page::Credits: error = prepare_credits(presentation, account); break;
    case Page::Account: error = prepare_account(presentation, account); break;
    }
    if (error != ESP_OK) { return result("prepare page", error); }
    ++presentation.revision;
    presentation.valid = true;
    attach_content(presentation, content);
    return ESP_OK;
}

} // namespace CODY
