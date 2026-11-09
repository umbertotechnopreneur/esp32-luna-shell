#include "CODY_account_dashboard.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string_view>

#include "LUNASHELL_device_snapshot.hpp"
#include "CODY_technical_panels.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace CODY {
namespace {

using SourceState = AccountDashboardPresentation::SourceState;
using Page = AccountDashboardPage;

constexpr char kTag[] = "CODY_account_dashboard";
constexpr DamageRect kContentBounds{73, 73, 393, 393};
constexpr CODY2D::Rect kHeadline{73, 79, 320, 46};
constexpr CODY2D::Rect kPlanIconBounds{205, 134, 56, 56};
constexpr int kPlanMaskEdge = 16;
constexpr int kPlanMaskScale = 3;
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
constexpr int64_t kHistoryDayCount = 7;
constexpr int64_t kDaySeconds = 86400;

struct QuotaWindow {
    const char *label;
    uint16_t remaining;
    uint16_t top;
    uint16_t color;
    int64_t saved_epoch;
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

// state: Independently admitted live or historical numeric data.
bool displayable(SourceState state)
{
    return state == SourceState::Current || state == SourceState::Saved;
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
// left_aligned: Places a row label at the left edge of its reserved slot.
// Exception: Preserves native glyph errors or reports exhausted/invalid geometry.
esp_err_t add_caption(AccountDashboardPresentation &presentation, const CODY2D::MonoFont &font,
    const char *text, CODY2D::Rect bounds, uint16_t color, uint8_t scale = 1, bool left_aligned = false)
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
        {static_cast<int16_t>(bounds.x + (left_aligned ? 0 : (bounds.width - width * scale) / 2U)),
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
// window: One independently observed live or saved allowance window.
// Exception: Preserves any original native formatting or layout failure.
esp_err_t add_quota(AccountDashboardPresentation &presentation, const QuotaWindow &window)
{
    char text[32]{};
    format_percent(window.remaining, text);
    esp_err_t error = add_detail(presentation, window.label, window.top, kGold);
    if (error != ESP_OK) { return error; }
    const auto value_top = static_cast<uint16_t>(window.top + 25U);
    const uint16_t value_height = window.saved_epoch > 0 ? 58 : 80;
    error = add_hero(presentation, text, {73, value_top, 320, value_height}, 2, kWhite);
    if (error == ESP_OK && window.saved_epoch > 0) {
        std::tm calendar{};
        if (!utc_calendar(window.saved_epoch, calendar) ||
            std::strftime(text, sizeof(text), "Saved %Y-%m-%d %H:%MZ", &calendar) == 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        return add_detail(presentation, text, static_cast<uint16_t>(value_top + value_height + 1U));
    }
    if (error != ESP_OK) { return error; }

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
// account_epoch: Last update of the saved short window, or zero for live or absent data.
// weekly_epoch: Last update of the saved weekly window, or zero for live or absent data.
// Exception: Preserves native rendering or formatting errors.
esp_err_t prepare_quota(AccountDashboardPresentation &presentation,
    const LUNASHELL::CodexAccountSnapshot &account, const LUNASHELL::CodexWeeklyLimitSnapshot &weekly,
    int64_t account_epoch, int64_t weekly_epoch)
{
    const bool short_available = displayable(presentation.short_state);
    const bool weekly_available = displayable(presentation.weekly_state);
    if (!short_available && !weekly_available) {
        const bool elapsed = presentation.short_state == SourceState::Elapsed ||
            presentation.weekly_state == SourceState::Elapsed;
        const bool stale = presentation.short_state == SourceState::Stale ||
            presentation.weekly_state == SourceState::Stale;
        const char *message = elapsed ? "Limit reset pending" : stale ? "Limits are stale" :
            displayable(presentation.account_state) ? "No limits reported" : "Limits unavailable";
        return add_value(presentation, message, 212, kMuted);
    }

    char label[32]{};
    if (short_available) {
        const unsigned minutes = account.short_window_minutes;
        if (minutes < 60U) { std::snprintf(label, sizeof(label), "%u MINUTE WINDOW", minutes); }
        else if (minutes % 60U == 0) { std::snprintf(label, sizeof(label), "%u HOUR WINDOW", minutes / 60U); }
        else { std::snprintf(label, sizeof(label), "%uH %uM WINDOW", minutes / 60U, minutes % 60U); }
    }
    const bool both = short_available && weekly_available;
    constexpr uint16_t single_top = 176;
    esp_err_t error = add_rule(presentation, 125);
    if (error == ESP_OK && short_available) {
        const uint16_t top = both ? 129 : single_top;
        error = add_quota(presentation, {label, account.short_remaining_basis_points, top, kCyan, account_epoch});
    }
    if (error == ESP_OK && both) { error = add_rule(presentation, 245); }
    if (error == ESP_OK && weekly_available) {
        const uint16_t top = both ? 249 : single_top;
        error = add_quota(presentation,
            {"WEEKLY WINDOW", weekly.remaining_basis_points, top, kIndigo, weekly_epoch});
    }
    if (error != ESP_OK) { return error; }
    const bool saved = (short_available && account_epoch > 0) || (weekly_available && weekly_epoch > 0);
    return add_detail(presentation, saved ? "REMAINING AT LAST UPDATE" : "REMAINING", both ? 366 : 313, kCyan);
}

// presentation: Fixed current-day page owner.
// account: Provider's literal dated token totals.
// Exception: Preserves original native glyph and layout errors.
esp_err_t prepare_today(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    const bool available = displayable(presentation.account_state);
    const bool saved = presentation.account_state == SourceState::Saved;
    esp_err_t error = add_rule(presentation, 128);
    if (error != ESP_OK) { return error; }

    if (!available || presentation.utc_day <= 0) {
        const char *status = available ? "Device time unavailable" :
            presentation.account_state == SourceState::Stale ? "Daily data is stale" : "Daily data unavailable";
        return add_hero(presentation, status, {73, 175, 320, 144}, 1, kMuted);
    }

    char label[32] = "TOKENS TODAY (UTC)";
    int64_t day = presentation.utc_day;
    const auto *tokens = account.tokens_for_unix_day(day);
    if (tokens == nullptr) {
        const char *status = saved ? "DAILY TOTAL NOT REPORTED" : "TODAY TOTAL NOT REPORTED";
        tokens = account.latest_tokens_before(presentation.utc_day, day);
        if (tokens == nullptr) {
            return add_value(presentation, saved ? "No saved daily total" : "No daily total reported", 210, kMuted);
        }
        error = add_detail(presentation, status, 151);
        if (error != ESP_OK) { return error; }
    }
    if (saved || day != presentation.utc_day) {
        std::tm calendar{};
        char date[12]{};
        if (!utc_calendar(day * kDaySeconds, calendar) ||
            std::strftime(date, sizeof(date), "%Y-%m-%d", &calendar) == 0) {
            return ESP_ERR_INVALID_SIZE;
        }
        std::snprintf(label, sizeof(label), "TOKENS ON %s", date);
    }

    char count[32]{};
    format_count(*tokens, count);
    error = add_hero(presentation, count, {73, 178, 320, 138}, 4, kWhite);
    if (error != ESP_OK) { return error; }
    return add_detail(presentation, label, 319, kWhite);
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
// account: Canonical dated counts, which may have gaps in the chart interval.
// first_day: Oldest of the seven complete calendar days.
// maximum: Maximum actually observed daily count.
// Exception: Returns original chart geometry or glyph preparation errors.
esp_err_t prepare_chart(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account,
    int64_t first_day, uint64_t maximum)
{
    constexpr const char *weekday[]{"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    esp_err_t error = add_bar(presentation, {kChartLeft, kChartBaseline, 308, 2}, kDarkTrack, kDarkTrack);
    if (error != ESP_OK) { return error; }
    for (int64_t index = 0; index < kHistoryDayCount; ++index) {
        const auto x = static_cast<uint16_t>(kChartLeft + index * kChartStep);
        const int64_t day = first_day + index;
        if (const auto *tokens = account.tokens_for_unix_day(day)) {
            const uint16_t height = *tokens == 0 ? 2 : chart_height(*tokens, maximum);
            const bool newest = index == kHistoryDayCount - 1;
            error = add_bar(presentation,
                {static_cast<uint16_t>(x + 8U), static_cast<uint16_t>(kChartBaseline - height), 28, height},
                newest ? kCyan : kIndigo, newest ? 0x4DF9 : 0x4435);
        } else {
            error = add_caption(presentation, *presentation.body_font, "--", {x, 282, kChartStep, 30}, kMuted);
        }
        if (error != ESP_OK) { return error; }
        const char *label = weekday[day % kHistoryDayCount];
        error = add_caption(presentation, *presentation.body_font, label, {x, 323, kChartStep, 26}, kMuted);
        if (error != ESP_OK) { return error; }
    }
    std::tm first{};
    std::tm last{};
    const int64_t last_day = first_day + kHistoryDayCount - 1;
    if (!utc_calendar(first_day * kDaySeconds, first) || !utc_calendar(last_day * kDaySeconds, last)) {
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
    if (!displayable(presentation.account_state)) {
        const char *status = presentation.account_state == SourceState::Stale
            ? "Daily data is stale" : "Daily data unavailable";
        return add_value(presentation, status, 209, kMuted);
    }
    if (presentation.utc_day <= kHistoryDayCount) {
        return add_value(presentation, "Device time unavailable", 185, kMuted);
    }
    // UTC is the earlier or equal calendar day in every supported device time zone.
    const int64_t first_day = presentation.utc_day - kHistoryDayCount;
    uint64_t total = 0;
    uint64_t maximum = 0;
    uint8_t observed = 0;
    bool overflow = false;
    for (int64_t day = first_day; day < presentation.utc_day; ++day) {
        const auto *tokens = account.tokens_for_unix_day(day);
        if (tokens == nullptr) { continue; }
        ++observed;
        maximum = std::max(maximum, *tokens);
        if (std::numeric_limits<uint64_t>::max() - total < *tokens) { overflow = true; }
        if (!overflow) { total += *tokens; }
    }
    if (observed == 0) {
        return add_value(presentation, "No daily totals reported", 209, kMuted);
    }
    if (observed == kHistoryDayCount && !overflow) {
        char count[32]{};
        format_count(total, count);
        error = add_hero(presentation, count, {82, 133, 198, 80}, 2, kCyan);
        if (error != ESP_OK) { return error; }
        error = add_caption(presentation, *presentation.body_font, "TOKENS", {280, 133, 104, 80}, kWhite);
    } else if (observed < kHistoryDayCount) {
        char coverage[32]{};
        std::snprintf(coverage, sizeof(coverage), "%u/7 days reported", static_cast<unsigned>(observed));
        error = add_value(presentation, coverage, 153, kWhite);
    } else {
        error = add_value(presentation, "Seven-day total unavailable", 153, kMuted);
    }
    if (error != ESP_OK) { return error; }
    return prepare_chart(presentation, account, first_day, maximum);
}

// value: Exact nonnegative whole count, without abbreviated units.
// text: Receives English thousands separators within its fixed 32-byte capacity.
void format_grouped_count(uint64_t value, char (&text)[32])
{
    char digits[21]{};
    std::snprintf(digits, sizeof(digits), "%llu", static_cast<unsigned long long>(value));
    const size_t length = std::strlen(digits);
    size_t destination = length + (length - 1U) / 3U;
    text[destination] = '\0';
    for (size_t remaining = length; remaining > 0; --remaining) {
        text[--destination] = digits[remaining - 1U];
        if (remaining > 1U && (length - remaining + 1U) % 3U == 0) {
            text[--destination] = ',';
        }
    }
}

// value: Exact nonnegative balance in thousandths of one credit.
// text: Receives a grouped whole-credit value, rounded half up without overflow.
void format_credits(uint64_t value, char (&text)[32])
{
    const uint64_t whole = value / 1000U + (value % 1000U >= 500U ? 1U : 0U);
    format_grouped_count(whole, text);
}

// presentation: Fixed owner of the section label and right-aligned ticket artwork.
// top: Upper edge of the voucher heading slot.
// Exception: Preserves original native glyph, geometry or bounded-capacity errors.
esp_err_t add_voucher_label(AccountDashboardPresentation &presentation, uint16_t top)
{
    auto error = add_caption(presentation, *presentation.body_font, "Reset Vouchers",
        {93, top, 168, 32}, kWhite, 1, true);
    if (error != ESP_OK) { return error; }
    const auto upper = static_cast<uint16_t>(top + 5U);
    const CODY2D::Rect outline[]{
        {339, upper, 34, 2},
        {339, static_cast<uint16_t>(upper + 20U), 34, 2},
        {337, static_cast<uint16_t>(upper + 2U), 2, 6},
        {337, static_cast<uint16_t>(upper + 14U), 2, 6},
        {373, static_cast<uint16_t>(upper + 2U), 2, 6},
        {373, static_cast<uint16_t>(upper + 14U), 2, 6},
        {362, static_cast<uint16_t>(upper + 5U), 2, 4},
        {362, static_cast<uint16_t>(upper + 13U), 2, 4},
    };
    for (const auto &bounds : outline) {
        error = add_bar(presentation, bounds, kGold, kGold);
        if (error != ESP_OK) { return error; }
    }
    return ESP_OK;
}

// expiry: Actual UTC instant, or zero when Codex explicitly reports no expiry.
// clock: Canonical timezone preference for this device.
// text: Receives the configured local date or an explicit unavailable caption.
void format_voucher_date(uint32_t expiry, const LUNASHELL::ClockSnapshot &clock, char (&text)[32])
{
    if (expiry == 0) {
        std::strcpy(text, "No expiry");
        return;
    }
    constexpr const char *months[]{"January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    LUNASHELL::ClockLocalCalendar calendar{};
    const auto error = clock.preferences_valid ? LUNASHELL::project_clock_local_calendar(expiry, calendar) :
        ESP_ERR_INVALID_STATE;
    if (error != ESP_OK || calendar.timezone != clock.timezone || calendar.month < 1 || calendar.month > 12) {
        ESP_LOGW(kTag, "Voucher local date unavailable (%s)",
            esp_err_to_name(error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE));
        std::strcpy(text, "Local date unavailable");
        return;
    }
    std::snprintf(text, sizeof(text), "%u %s %u", unsigned(calendar.day),
        months[calendar.month - 1U], unsigned(calendar.year));
}

// presentation: Owns the three complete rows and their scroll region.
// clock: Canonical timezone preference, independent of credit freshness.
// top: Logical first row below the heading.
// Exception: Preserves the original formatting or native geometry error.
esp_err_t add_voucher_rows(AccountDashboardPresentation &presentation,
    const LUNASHELL::ClockSnapshot &clock, uint16_t top)
{
    const auto &list = presentation.voucher_list;
    const auto visible = std::min<size_t>(list.expiries.size(), list.count - list.first);
    constexpr uint16_t kRowStep = 29;
    for (size_t index = 0; index < visible; ++index) {
        const auto y = static_cast<uint16_t>(top + index * kRowStep);
        char number[8]{};
        std::snprintf(number, sizeof(number), "%u", unsigned(list.first + index + 1U));
        auto error = add_caption(presentation, *presentation.body_font, number, {93, y, 24, 25}, kGold);
        if (error != ESP_OK) return error;
        char date[32]{};
        format_voucher_date(list.expiries[index], clock, date);
        error = add_caption(presentation, *presentation.body_font, date, {128, y, 244, 25}, kWhite, 1, true);
        if (error != ESP_OK) return error;
    }
    const auto last = VoucherList::last_offset(list.count);
    if (last == 0) return ESP_OK;

    constexpr uint16_t kTrackHeight = VoucherList::kVisibleRows * kRowStep - 4U;
    constexpr uint16_t kThumbHeight = 19;
    const auto thumb_top = static_cast<uint16_t>(top + (kTrackHeight - kThumbHeight) * list.first / last);
    // The heading and the complete rows share one swipe region, clear of both arrows.
    presentation.voucher_bounds = {73, static_cast<uint16_t>(top - 40U), 320, kTrackHeight + 40U};
    auto error = add_bar(presentation, {380, top, 2, kTrackHeight}, kDarkTrack, kDarkTrack);
    if (error != ESP_OK) return error;
    return add_bar(presentation, {380, thumb_top, 2, kThumbHeight}, kGold, kGold);
}

// presentation: Fixed credits page owner.
// account: Independent reported credit kind, amount and reset vouchers.
// clock: Canonical timezone preference used for date projection.
// Exception: Preserves original native label or formatting errors.
esp_err_t prepare_credits(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account,
    const LUNASHELL::ClockSnapshot &clock)
{
    const bool observed = displayable(presentation.account_state);
    const bool finite = observed && account.credits_kind == LUNASHELL::CodexCreditKind::Finite;
    const bool unlimited = observed && account.credits_kind == LUNASHELL::CodexCreditKind::Unlimited;
    const bool balance = finite || unlimited;
    const auto &list = presentation.voucher_list;
    const bool count_valid = observed && account.reset_vouchers_valid;
    const bool vouchers = count_valid || list.current;
    esp_err_t error = add_bar(presentation, {99, 102, 44, 2}, kCyan, kCyan);
    if (error != ESP_OK) { return error; }
    error = add_bar(presentation, {323, 102, 44, 2}, kCyan, kCyan);
    if (error != ESP_OK) { return error; }
    if (!balance && !vouchers) {
        const char *message = presentation.account_state == SourceState::Stale ? "Credit data is stale" :
            observed ? "No credit data reported" : "Credit data unavailable";
        return add_value(presentation, message, 212, kMuted);
    }

    if (balance) {
        char credits[32]{};
        if (finite) { format_credits(account.credit_milliunits, credits); }
        const uint16_t top = vouchers ? 127 : 175;
        error = add_detail(presentation, "BALANCE", top, kWhite);
        if (error != ESP_OK) { return error; }
        error = add_hero(presentation, unlimited ? "Unlimited" : credits,
            {73, static_cast<uint16_t>(top + 26), 320, 80}, finite ? 2 : 1, kCyan);
        if (error != ESP_OK || !vouchers) { return error; }
        error = add_rule(presentation, 239);
        if (error != ESP_OK) { return error; }
    }

    const uint16_t top = balance ? 247 : 172;
    error = add_voucher_label(presentation, top);
    if (error != ESP_OK) { return error; }
    if (count_valid) {
        char count[32]{};
        format_count(account.reset_vouchers, count);
        error = add_caption(presentation, *presentation.body_font, count, {261, top, 68, 32}, kMuted);
        if (error != ESP_OK) return error;
    }
    const auto row_top = static_cast<uint16_t>(top + 40U);
    if (list.current && list.count != 0) return add_voucher_rows(presentation, clock, row_top);
    const char *status = count_valid && account.reset_vouchers == 0 ? "No reset vouchers" :
        list.current ? "No expiry dates reported" : list.stale ? "Expiry dates are stale" : "Expiry dates unavailable";
    return add_detail(presentation, status, row_top);
}

struct PlanAppearance {
    const char *label = nullptr;
    CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
    uint16_t color = kMuted;
    const char *price = nullptr;
};

// account: Current authenticated account snapshot with a bounded optional plan token.
// fallback: Receives a readable label for a newly reported plan.
PlanAppearance account_plan_appearance(const LUNASHELL::CodexAccountSnapshot &account,
    char (&fallback)[sizeof(LUNASHELL::CodexAccountSnapshot::plan)])
{
    using Icon = CODY2D::ActionIcon;
    struct PlanEntry {
        std::string_view token;
        PlanAppearance appearance;
    };
    // Public US list prices are the baseline for bridges predating plan projections.
    // App Server reports a plan type, which does not establish the user's invoice.
    static constexpr char pro_price[] = "100/200/500 USD/mo";
    static constexpr PlanEntry plans[] = {
        {"free", {"Free", Icon::PlanFree, kCyan, "0 USD/mo"}},
        {"go", {"Go", Icon::Arrow, kCyan, "8 USD/mo"}},
        {"plus", {"Plus", Icon::PlanPlus, kCyan, "20 USD/mo"}},
        {"pro", {"ChatGPT Pro", Icon::PlanPro, kIndigo, pro_price}},
        {"prolite", {"ChatGPT Pro", Icon::PlanPro, kIndigo, pro_price}},
        {"promax", {"ChatGPT Pro", Icon::PlanPro, kIndigo, pro_price}},
        {"team", {"Business", Icon::PlanBusiness, kCyan, "25 USD/user/mo"}},
        // An older bridge could normalize usage-based Business to this same token.
        {"business", {"Business", Icon::PlanBusiness, kCyan}},
        {"ent26", {"Enterprise", Icon::PlanEnterprise, kCyan}},
        {"enterprise", {"Enterprise", Icon::PlanEnterprise, kCyan}},
        {"edu", {"Edu", Icon::PlanEducation, kCyan}},
        {"edu_plus", {"Edu Plus", Icon::PlanEducation, kCyan}},
        {"edu_pro", {"Edu Pro", Icon::PlanEducation, kCyan}},
    };
    const char *const end = account.plan + sizeof(account.plan);
    const char *const terminator = std::find(account.plan, end, '\0');
    if (terminator == account.plan || terminator == end ||
        !std::all_of(account.plan, terminator, [](char letter) { return letter >= 32 && letter <= 126; })) {
        return {};
    }
    const std::string_view token{account.plan, static_cast<size_t>(terminator - account.plan)};
    if (token == "unknown") { return {}; }
    for (const auto &plan : plans) {
        if (plan.token == token) { return plan.appearance; }
    }
    for (size_t index = 0; index < token.size(); ++index) {
        const char letter = token[index];
        fallback[index] = letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
        if (letter == '_') { fallback[index] = ' '; }
    }
    fallback[token.size()] = '\0';
    return {fallback, Icon::PlanAccount, kMuted};
}

// presentation: Fixed account page owner.
// account: Authenticated ChatGPT account metadata without an email or account identifier.
// Exception: Returns the original native text or geometry error.
esp_err_t prepare_account(AccountDashboardPresentation &presentation, const LUNASHELL::CodexAccountSnapshot &account)
{
    if (presentation.account_state != SourceState::Current) {
        const bool stale = presentation.account_state == SourceState::Stale;
        const bool saved = presentation.account_state == SourceState::Saved;
        presentation.plan_icon = CODY2D::ActionIcon::PlanAccount;
        presentation.plan_icon_color = stale || saved ? kGold : kIndigo;
        const char *status = stale ? "Account data stale" :
            saved ? "Saved account data" : "Account unavailable";
        auto error = add_value(presentation, status, 210, stale || saved ? kGold : kWhite);
        if (error != ESP_OK) { return error; }
        error = add_detail(presentation,
            stale || saved ? "Live details unavailable" : "No account details received", 270);
        if (error != ESP_OK) { return error; }
        return add_detail(presentation, "Check Codex on your PC", 301);
    }
    char fallback[sizeof(account.plan)]{};
    const PlanAppearance plan = account_plan_appearance(account, fallback);
    const char *label = account.plan_label[0] != '\0' ? account.plan_label : plan.label;
    const char *price = account.plan_projection_supported ? account.plan_price : plan.price;
    uint16_t status_top = account.workspace_messages_valid ? 171 : 212;
    if (label != nullptr) {
        presentation.plan_icon = plan.icon != CODY2D::ActionIcon::None
            ? plan.icon : CODY2D::ActionIcon::PlanAccount;
        presentation.plan_icon_color = plan.color;
        presentation.plan_mask_valid = account.plan_icon_valid;
        if (presentation.plan_mask_valid) {
            static_assert(sizeof(account.plan_icon) == kPlanMaskEdge * kPlanMaskEdge / 8);
            std::copy_n(account.plan_icon, presentation.plan_mask.size(), presentation.plan_mask.begin());
        }
        esp_err_t error = add_value(presentation, label, 192);
        if (error != ESP_OK) { return error; }
        status_top = 247;
        if (price != nullptr && price[0] != '\0') {
            error = add_detail(presentation, price, 237, kCyan);
            if (error != ESP_OK) { return error; }
            error = add_detail(presentation, "US LIST PRICE", 263);
            if (error != ESP_OK) { return error; }
            status_top = 291;
        }
    }
    presentation.account_status_dot.bounds = {117, static_cast<uint16_t>(status_top + 8), 10, 10};
    presentation.account_status_dot.top_color = kGreen;
    presentation.account_status_dot.bottom_color = kGreen;
    esp_err_t error = add_detail(presentation, "CHATGPT CONNECTED", status_top, kWhite);
    if (error != ESP_OK) { return error; }
    if (!account.workspace_messages_valid) { return ESP_OK; }
    error = add_rule(presentation, status_top + 29);
    if (error != ESP_OK) { return error; }
    error = add_detail(presentation, "WORKSPACE MESSAGES", status_top + 33, kCyan);
    if (error != ESP_OK) { return error; }
    char count[32]{};
    format_count(account.workspace_messages, count);
    const char *message_count = account.workspace_messages == 0 ? "NO ACTIVE MESSAGES" : count;
    return add_value(presentation, message_count, status_top + 59);
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

// presentation: Prepared plugin icon mask and display accent.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
void draw_plan_mask(const AccountDashboardPresentation &presentation, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    constexpr int size = kPlanMaskEdge * kPlanMaskScale;
    constexpr int icon_x = kPlanIconBounds.x + (kPlanIconBounds.width - size) / 2;
    constexpr int icon_y = kPlanIconBounds.y + (kPlanIconBounds.height - size) / 2;
    const int left = std::max<int>(first_column, icon_x);
    const int right = std::min<int>(first_column + strip.width, icon_x + size);
    const int top = std::max<int>(first_row, icon_y);
    const int bottom = std::min<int>(first_row + strip.height, icon_y + size);
    if (left >= right || top >= bottom) { return; }

    const auto color = CODY2D::rgb565_native_to_stored(presentation.plan_icon_color, strip.byte_order);
    for (int y = top; y < bottom; ++y) {
        const int row = (y - icon_y) / kPlanMaskScale;
        const auto *mask = presentation.plan_mask.data() + row * (kPlanMaskEdge / 8);
        auto *pixels = strip.pixels + (y - first_row) * strip.stride;
        for (int column = (left - icon_x) / kPlanMaskScale; column < kPlanMaskEdge; ++column) {
            const int cell_x = icon_x + column * kPlanMaskScale;
            if (cell_x >= right) { break; }
            if ((mask[column / 8] & (0x80U >> (column % 8))) == 0) { continue; }
            const int run_start = std::max(cell_x, left);
            const int run_end = std::min(cell_x + kPlanMaskScale, right);
            std::fill_n(pixels + run_start - first_column, run_end - run_start, color);
        }
    }
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
    if (presentation.plan_mask_valid) {
        draw_plan_mask(presentation, strip, first_row, first_column);
    } else if (presentation.plan_icon != CODY2D::ActionIcon::None) {
        error = CODY2D::draw_action_icon_rows(presentation.plan_icon, kPlanIconBounds,
            presentation.plan_icon_color, strip, first_row, first_column);
    }
    if (error == ESP_OK && presentation.account_status_dot.bounds.width != 0) {
        error = CODY2D::draw_capsule_rows(presentation.account_status_dot, strip, first_row, first_column);
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
    return displayable(source) && reset > 0 && now >= reset ? SourceState::Elapsed : source;
}

// metric: Independently projected native counter and original freshness state.
SourceState metric_state(const PanelMetric &metric)
{
    switch (metric.state) {
    case PanelValueState::Current: return SourceState::Current;
    case PanelValueState::Stale: return SourceState::Stale;
    case PanelValueState::Invalid: return SourceState::Invalid;
    default: return SourceState::Unavailable;
    }
}

// observation: Coherent canonical chat identity and explicit compaction validity.
// inputs: Existing technical projection, including stale/orphaned lifecycle handling.
// Returns null when no compaction observation belongs to the followed chat.
const char *compaction_text(const LUNASHELL::CodexTechnicalObservation &observation, const PanelInputs &inputs)
{
    using State = LUNASHELL::CodexCompactionState;
    switch (inputs.compaction) {
    case State::Active: return "Compacting";
    case State::Refreshed: return inputs.counters_pending ? "Refreshed; counters pending" : "Context refreshed";
    case State::Orphaned: return "Partially observed";
    case State::Interrupted: return "Interrupted";
    case State::Stale: return "Stale";
    case State::None: break;
    default: return "Invalid data";
    }
    const auto &technical = observation.technical;
    const auto &activity = observation.link.activity;
    const bool same_owner = technical.data_valid && activity.data_valid && technical.chat_id[0] != '\0' &&
        inputs.chat_id[0] != '\0' && std::strcmp(technical.chat_id, inputs.chat_id) == 0 &&
        technical.revision == activity.revision;
    if (!technical.compaction_valid || !same_owner) { return nullptr; }
    if (source_state(technical) != SourceState::Current || inputs.link != PanelValueState::Current ||
        inputs.activity != PanelValueState::Current || !inputs.connected) { return "Stale"; }
    return "Not compacting";
}

} // namespace

// page: Explicit account page independent of device navigation.
// live_account: Aged account-level App Server observation with independent optional fields.
// live_weekly: Aged weekly allowance, separate from the other observations.
// clock: Aged device clock used for calendar day boundaries and elapsed limits.
// title_font: Immutable headline and numeric glyphs.
// body_font: Immutable date and detail glyphs.
// presentation: Persistent owner, mutated only after the prior frame retires.
// content: Receives the central draw callback without lateral navigation.
// saved: Dated historical data for the retained pairing, selected only while disconnected.
// vouchers: Current expiry viewport, independent of the observed balance.
// Exception: Returns the original native preparation failure.
esp_err_t prepare_account_dashboard(AccountDashboardPage page,
    const LUNASHELL::CodexAccountSnapshot &live_account, const LUNASHELL::CodexWeeklyLimitSnapshot &live_weekly,
    const LUNASHELL::ClockSnapshot &clock, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, AccountDashboardPresentation &presentation, SceneContent &content,
    const LUNASHELL::CodexSavedSnapshot *saved, const VoucherList &vouchers)
{
    if (page > Page::Account) { return result("page identity", ESP_ERR_INVALID_ARG); }
    const auto &account = saved != nullptr ? saved->account : live_account;
    const auto &weekly = saved != nullptr ? saved->weekly : live_weekly;
    const int64_t saved_account_at = saved != nullptr ? saved->account_observed_utc : 0;
    const int64_t saved_weekly_at = saved != nullptr ? saved->weekly_observed_utc : 0;
    const SourceState account_state = saved == nullptr ? source_state(account) :
        (saved_account_at > 0 ? SourceState::Saved : SourceState::Unavailable);
    const int64_t now = source_state(clock) == SourceState::Current && clock.utc_epoch_seconds > 0
        ? clock.utc_epoch_seconds : 0;
    const bool dated_page = page == Page::Today || page == Page::LastSevenDays;
    const int64_t day_reference = saved != nullptr ? saved_account_at : now;
    const int64_t utc_day = dated_page && day_reference > 0 ? day_reference / kDaySeconds : -1;
    SourceState weekly_state = SourceState::Unavailable;
    SourceState short_state = SourceState::Unavailable;
    if (page == Page::QuotaWindows) {
        const auto weekly_source = saved == nullptr ? source_state(weekly) :
            (saved_weekly_at > 0 ? SourceState::Saved : SourceState::Unavailable);
        weekly_state = quota_state(weekly_source, weekly.reset_unix_seconds, now);
        short_state = quota_state(account_state, account.short_reset_unix_seconds, now);
        if (displayable(weekly_state) && weekly.remaining_basis_points > 10000U) {
            weekly_state = SourceState::Unavailable;
        }
        if (displayable(short_state) &&
            (account.short_window_minutes == 0 || account.short_remaining_basis_points > 10000U)) {
            short_state = SourceState::Unavailable;
        }
    }
    const int64_t account_at_us = saved != nullptr ? saved_account_at : account.last_success_at_us;
    const int64_t weekly_at_us = page == Page::QuotaWindows
        ? (saved != nullptr ? saved_weekly_at : weekly.last_success_at_us) : 0;
    const int8_t voucher_timezone = page == Page::Credits && clock.preferences_valid
        ? static_cast<int8_t>(clock.timezone) : -1;
    if (presentation.valid && !presentation.context_page && presentation.page == page && presentation.account_state == account_state &&
        presentation.weekly_state == weekly_state && presentation.short_state == short_state &&
        presentation.account_at_us == account_at_us && presentation.weekly_at_us == weekly_at_us &&
        presentation.utc_day == utc_day && presentation.title_font == &title_font && presentation.body_font == &body_font &&
        presentation.voucher_list == vouchers && presentation.voucher_timezone == voucher_timezone) {
        attach_content(presentation, content);
        return ESP_OK;
    }
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) {
        return result("page revision", ESP_ERR_INVALID_STATE);
    }
    presentation.valid = false;
    presentation.context_page = false;
    presentation.caption_count = 0;
    presentation.bar_count = 0;
    presentation.account_status_dot = {};
    presentation.voucher_bounds = {};
    presentation.voucher_list = vouchers;
    presentation.voucher_timezone = voucher_timezone;
    presentation.plan_icon = CODY2D::ActionIcon::None;
    presentation.plan_mask_valid = false;
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
    case Page::Today: headline = saved != nullptr ? "SAVED DAY" : "TODAY"; headline_color = kCyan; break;
    case Page::LastSevenDays: headline = saved != nullptr ? "SAVED 7 DAYS" : "LAST 7 DAYS"; break;
    case Page::Credits: headline = "CREDITS"; headline_color = kCyan; break;
    case Page::Account: headline = "Codex account"; headline_color = kWhite; break;
    }
    esp_err_t error = add_caption(presentation, title_font, headline, kHeadline, headline_color);
    if (error != ESP_OK) { return result("page headline", error); }
    switch (page) {
    case Page::QuotaWindows:
        error = prepare_quota(presentation, account, weekly,
            account.short_window_minutes != 0 ? saved_account_at : 0, saved_weekly_at);
        break;
    case Page::Today: error = prepare_today(presentation, account); break;
    case Page::LastSevenDays: error = prepare_history(presentation, account); break;
    case Page::Credits: error = prepare_credits(presentation, account, clock); break;
    case Page::Account: error = prepare_account(presentation, account); break;
    }
    if (error != ESP_OK) { return result("prepare page", error); }
    ++presentation.revision;
    presentation.valid = true;
    attach_content(presentation, content);
    return ESP_OK;
}

// observation: Canonical chat/turn-coherent measurements and explicit compaction state.
// title_font: Immutable native headline and numeric glyphs.
// body_font: Immutable native detail glyphs.
// presentation: Persistent display-owned storage, updated after the prior frame retires.
// content: Receives the central account-style context page without navigation controls.
// Exception: Preserves original projection, clock and native preparation failures.
esp_err_t prepare_context_dashboard(const LUNASHELL::CodexTechnicalObservation &observation,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AccountDashboardPresentation &presentation, SceneContent &content)
{
    PanelInputs inputs{};
    auto error = project_companion_panel_inputs(observation.link, observation.technical,
        esp_timer_get_time(), observation.firmware_version, inputs);
    if (error != ESP_OK) { return result("context projection", error); }
    SourceState capacity_state = metric_state(inputs.capacity);
    if (capacity_state == SourceState::Current && inputs.capacity.value == 0) {
        capacity_state = SourceState::Invalid;
    }
    const SourceState used_state = metric_state(inputs.used);
    SourceState remaining_state = SourceState::Unavailable;
    uint64_t remaining = 0;
    if (capacity_state == SourceState::Invalid || used_state == SourceState::Invalid) {
        remaining_state = SourceState::Invalid;
    } else if (capacity_state == SourceState::Stale || used_state == SourceState::Stale) {
        remaining_state = SourceState::Stale;
    } else if (inputs.context_coherent && capacity_state == SourceState::Current && used_state == SourceState::Current) {
        remaining_state = inputs.used.value > inputs.capacity.value ? SourceState::OverLimit : SourceState::Current;
        if (remaining_state == SourceState::Current) { remaining = inputs.capacity.value - inputs.used.value; }
    }
    const uint64_t capacity = capacity_state == SourceState::Current ? inputs.capacity.value : 0;
    const char *compaction = compaction_text(observation, inputs);
    if (presentation.valid && presentation.context_page && presentation.account_state == remaining_state &&
        presentation.weekly_state == capacity_state && presentation.context_remaining == remaining &&
        presentation.context_capacity == capacity && presentation.compaction_label == compaction &&
        presentation.title_font == &title_font && presentation.body_font == &body_font) {
        attach_content(presentation, content);
        return ESP_OK;
    }
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) {
        return result("context revision", ESP_ERR_INVALID_STATE);
    }
    presentation.valid = false;
    presentation.context_page = true;
    presentation.caption_count = 0;
    presentation.bar_count = 0;
    presentation.account_status_dot = {};
    presentation.voucher_bounds = {};
    presentation.plan_icon = CODY2D::ActionIcon::None;
    presentation.plan_mask_valid = false;
    presentation.title_font = &title_font;
    presentation.body_font = &body_font;
    presentation.account_state = remaining_state;
    presentation.weekly_state = capacity_state;
    presentation.context_remaining = remaining;
    presentation.context_capacity = capacity;
    presentation.compaction_label = compaction;

    char count[32]{};
    if (remaining_state == SourceState::Current) { format_grouped_count(remaining, count); }
    error = add_caption(presentation, title_font, "CONTEXT", kHeadline, kCyan);
    if (error == ESP_OK) { error = add_rule(presentation, 125); }
    if (remaining_state == SourceState::Current) {
        if (error == ESP_OK) { error = add_detail(presentation, "TOKENS REMAINING", 133, kWhite); }
        if (error == ESP_OK) { error = add_hero(presentation, count, {73, 159, 320, 80}, 2, kCyan); }
        if (error == ESP_OK) { error = add_bar(presentation, {93, 244, 280, 7}, kDarkTrack, kDarkTrack); }
        const auto width = static_cast<uint16_t>(static_cast<double>(remaining) / static_cast<double>(capacity) * 280.0);
        if (error == ESP_OK && width != 0) { error = add_bar(presentation, {93, 244, width, 7}, kCyan, kCyan); }
    } else {
        // Missing metrics do not create empty counters. Compaction below has
        // its own hook provenance and remains visible when independently known.
        const char *message = remaining_state == SourceState::Stale ? "Context data is stale" :
            remaining_state == SourceState::Invalid ? "Invalid context data" :
            remaining_state == SourceState::OverLimit ? "Context over limit" : "Context data unavailable";
        const uint16_t top = capacity_state == SourceState::Current || compaction != nullptr ? 178 : 207;
        if (error == ESP_OK) { error = add_detail(presentation, message, top, kWhite); }
        if (error == ESP_OK) {
            error = add_detail(presentation, "For the followed chat", static_cast<uint16_t>(top + 31U), kMuted);
        }
    }
    if (capacity_state == SourceState::Current) {
        if (error == ESP_OK) { error = add_rule(presentation, 259); }
        if (error == ESP_OK) { error = add_detail(presentation, "CONTEXT WINDOW", 266, kWhite); }
        format_grouped_count(capacity, count);
        if (error == ESP_OK) { error = add_value(presentation, count, 291, kWhite); }
    }
    if (error == ESP_OK && compaction != nullptr) {
        const uint16_t top = capacity_state == SourceState::Current ? 335 : 259;
        error = add_rule(presentation, top);
        if (error == ESP_OK) {
            error = add_detail(presentation, "COMPACTION", static_cast<uint16_t>(top + 3U), kMuted);
        }
        if (error == ESP_OK) {
            error = add_detail(presentation, compaction, static_cast<uint16_t>(top + 30U),
                inputs.compaction == LUNASHELL::CodexCompactionState::Active ? kGold : kCyan);
        }
    }
    if (error != ESP_OK) { return result("prepare context", error); }
    ++presentation.revision;
    presentation.valid = true;
    attach_content(presentation, content);
    return ESP_OK;
}

} // namespace CODY
