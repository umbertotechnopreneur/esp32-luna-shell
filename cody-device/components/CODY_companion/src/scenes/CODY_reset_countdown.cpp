#include "CODY_reset_countdown.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_reset_countdown";
constexpr uint16_t kMinYear = 2020;
constexpr uint16_t kMaxYear = 2099;
constexpr uint64_t kDaySeconds = 86400;
constexpr uint64_t kMaxSeconds = (kMaxYear - kMinYear + 1U) * 366U * kDaySeconds;
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kAccent = 0x6F9E;
constexpr uint16_t kError = 0xFD20;
constexpr uint16_t kTileTop = 0x2125;
constexpr uint16_t kTileBottom = 0x0862;
constexpr uint16_t kFoldTop = 0x1904;
constexpr uint16_t kFoldBottom = 0x10A3;
constexpr uint16_t kHinge = 0x4208;
constexpr uint16_t kHalfFlip = kResetCountdownFlipComplete / 2U;
constexpr CODY2D::Rect kMain{73, 73, 320, 320};
constexpr CODY2D::Rect kClockIcon{204, 181, 58, 58};
constexpr uint16_t kFaceTop = 172;
constexpr uint16_t kFaceWidth = 74;
constexpr uint16_t kFaceHeight = 74;
constexpr uint16_t kHalfFace = kFaceHeight / 2U;
constexpr uint16_t kFoldInset = 4;
constexpr const char *kUnits[]{"DAYS", "HRS", "MIN", "SEC"};
constexpr uint16_t kCanvasSize = 466;
constexpr float kCenter = kCanvasSize / 2.f;
constexpr float kInnerEdge = 223.5f;
constexpr float kOuterEdge = 229.5f;
constexpr float kBatteryOuterEdge = kInnerEdge - 1.f;
constexpr float kBatteryInnerEdge = kBatteryOuterEdge - (kOuterEdge - kInnerEdge);
constexpr uint16_t kBatteryTrack = 0x18C3;
constexpr uint16_t kBatteryGreen = 0x366A;
constexpr uint16_t kBatteryYellow = 0xCF25;
static_assert(kInnerEdge - kBatteryOuterEdge == 1.f, "Perimeter rings require a one-pixel radial gap");
constexpr float kTau = 6.28318530718f;

// operation: Exact failed preparation or draw operation.
// error: Original error, preserved for the display owner.
esp_err_t countdown_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// year: Gregorian calendar year.
// month: Valid one-based Gregorian calendar month.
uint8_t days_in_month(uint16_t year, uint8_t month)
{
    constexpr uint8_t days[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4U == 0 && (year % 100U != 0 || year % 400U == 0);
    return static_cast<uint8_t>(days[month - 1U] + (month == 2 && leap));
}

// rect: Authored half-open logical content rectangle.
DamageRect damage_rect(CODY2D::Rect rect)
{
    return {rect.x, rect.y, static_cast<uint16_t>(rect.x + rect.width),
        static_cast<uint16_t>(rect.y + rect.height)};
}

// caption: Fixed persistent storage for copied text and native layout.
// font: Immutable prepared glyph asset.
// text: Complete bounded printable ASCII caption.
// bounds: Admitted logical rectangle used for both centering and measuring.
// color: Semantic native RGB565 foreground.
// Exception: Returns original glyph error or rejects unsupported text size.
esp_err_t prepare_caption(ResetCountdownPresentation::Caption &caption,
    const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds, uint16_t color)
{
    const size_t length = std::strlen(text);
    if (length == 0 || length >= sizeof(caption.text) || font.height > bounds.height) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t width = 0;
    esp_err_t error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) { return error; }
    if (width > bounds.width) { return ESP_ERR_INVALID_SIZE; }
    std::memcpy(caption.text, text, length + 1U);
    return CODY2D::prepare_text_line(font, {caption.text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, caption.line);
}

// presentation: Serialized caption owner whose buffers outlive this frame.
// font: Immutable text asset.
// text: Complete bounded printable ASCII caption.
// bounds: Centering rectangle for this text only.
// color: Authored RGB565 foreground.
// Exception: Returns original text preparation or bounded-capacity error.
esp_err_t add_caption(ResetCountdownPresentation &presentation, const CODY2D::MonoFont &font,
    const char *text, CODY2D::Rect bounds, uint16_t color)
{
    if (presentation.caption_count >= presentation.captions.size()) { return ESP_ERR_INVALID_SIZE; }
    auto &caption = presentation.captions[presentation.caption_count];
    const esp_err_t error = prepare_caption(caption, font, text, bounds, color);
    if (error == ESP_OK) { ++presentation.caption_count; }
    return error;
}

// strip: Borrowed logical tile with valid original storage layout.
// first_row: Global logical y of its first pixel row.
// top: Inclusive global y limit of the requested subrange.
// bottom: Exclusive global y limit of the requested subrange.
// clipped_top: Receives the clipped logical y corresponding to row zero.
CODY2D::Surface clip_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t top, uint16_t bottom,
    uint16_t &clipped_top)
{
    clipped_top = std::max(first_row, top);
    const uint16_t clipped_bottom = std::min<uint16_t>(first_row + strip.height, bottom);
    if (clipped_top >= clipped_bottom) { return {}; }
    const uint32_t offset = (clipped_top - first_row) * strip.stride;
    strip.pixels += offset;
    strip.pixel_capacity -= offset;
    strip.height = static_cast<uint16_t>(clipped_bottom - clipped_top);
    return strip;
}

// strip: Valid borrowed native display tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// bounds: Full logical rectangle, clipped to the tile for drawing only.
// color: Native RGB565 fill value.
// Exception: Preserves the original native fill error.
esp_err_t fill_clipped(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
    CODY2D::Rect bounds, uint16_t color)
{
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right || top >= bottom) { return ESP_OK; }
    return CODY2D::fill_rect(strip, {static_cast<uint16_t>(left - first_column),
        static_cast<uint16_t>(top - first_row), static_cast<uint16_t>(right - left),
        static_cast<uint16_t>(bottom - top)}, color);
}

// line: Immutable prepared digit text.
// strip: Valid borrowed display tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// top: Inclusive logical y of the selected stationary half.
// bottom: Exclusive logical y of the selected stationary half.
// Exception: Returns the original native text draw error.
esp_err_t draw_digit_half(const CODY2D::TextLine &line, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t top, uint16_t bottom)
{
    uint16_t clipped_top = 0;
    const auto half = clip_rows(strip, first_row, top, bottom, clipped_top);
    if (half.height == 0) { return ESP_OK; }
    return CODY2D::draw_text_rows(line, line.text.length, half, clipped_top, first_column);
}

// phase: Half-phase progress from the flat face at zero to the hinge at 500.
uint16_t folded_height(uint16_t phase)
{
    // Samples of cos(angle) for an even rotation from 0 to 90 degrees.
    constexpr uint16_t projection[]{1000, 988, 951, 891, 809, 707, 588, 454, 309, 156, 0};
    if (phase >= kHalfFlip) { return 0; }
    constexpr unsigned sample_step = kHalfFlip / 10U;
    const unsigned sample = phase / sample_step;
    const unsigned fraction = phase % sample_step;
    const unsigned scale = (projection[sample] * (sample_step - fraction) +
        projection[sample + 1U] * fraction) / sample_step;
    return static_cast<uint16_t>(kHalfFace * scale / 1000U);
}

// line: Old upper or new lower prepared digit text.
// face: Logical bounds of the unchanged rounded tile under the flap.
// strip: Valid borrowed display tile, reused row-by-row without a scratch framebuffer.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// height: Apparent height of the folding half-card.
// upper: True for the old top folding toward the hinge.
// Exception: Returns original fill and glyph raster errors.
esp_err_t draw_fold(const CODY2D::TextLine &line, CODY2D::Rect face, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t height, bool upper)
{
    if (height == 0) { return ESP_OK; }
    const uint16_t hinge = static_cast<uint16_t>(face.y + kHalfFace);
    const uint16_t fold_top = upper ? static_cast<uint16_t>(hinge - height) : hinge;
    const uint16_t source_top = upper ? face.y : hinge;
    const auto x = static_cast<uint16_t>(face.x + kFoldInset);
    const auto width = static_cast<uint16_t>(face.width - 2U * kFoldInset);
    esp_err_t error = fill_clipped(strip, first_row, first_column, {x, fold_top, width, height},
        upper ? kFoldTop : kFoldBottom);
    const uint16_t top = std::max(first_row, fold_top);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, fold_top + height);
    for (uint16_t y = top; error == ESP_OK && y < bottom; ++y) {
        uint16_t row_top = 0;
        const auto row = clip_rows(strip, first_row, y, static_cast<uint16_t>(y + 1U), row_top);
        // Use the existing glyph raster with a one-row destination and the source y
        // projected around the hinge. The two are independent of display rotation.
        const auto source_y = static_cast<uint16_t>(source_top + (y - fold_top) * kHalfFace / height);
        error = CODY2D::draw_text_rows(line, line.text.length, row, source_y, first_column);
    }
    return error;
}

// tile: Prepared face and digit pair with optional previous unit.
// progress: Owner's full 200-ms animation projection in thousandths.
// strip: Valid borrowed display tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original native face, flap or digit raster error.
esp_err_t draw_tile(const ResetCountdownPresentation::Tile &tile, uint16_t progress,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto face = tile.face.bounds;
    if (first_row >= face.y + face.height || first_row + strip.height <= face.y ||
        first_column >= face.x + face.width || first_column + strip.width <= face.x) {
        return ESP_OK;
    }
    esp_err_t error = CODY2D::draw_capsule_rows(tile.face, strip, first_row, first_column);
    if (error != ESP_OK) { return error; }
    const uint16_t hinge = static_cast<uint16_t>(face.y + kHalfFace);
    const auto &value = tile.value.line;
    const auto &previous = tile.previous.line;
    if (previous.valid) {
        error = draw_digit_half(value, strip, first_row, first_column, face.y, hinge);
        if (error != ESP_OK) { return error; }
        error = draw_digit_half(previous, strip, first_row, first_column, hinge,
            static_cast<uint16_t>(face.y + face.height));
        if (error != ESP_OK) { return error; }
        const bool folding_top = progress < kHalfFlip;
        const uint16_t height = folded_height(folding_top ? progress : kResetCountdownFlipComplete - progress);
        error = draw_fold(folding_top ? previous : value, face, strip, first_row, first_column, height, folding_top);
    } else {
        error = CODY2D::draw_text_rows(value, value.text.length, strip, first_row, first_column);
    }
    if (error != ESP_OK) { return error; }
    error = fill_clipped(strip, first_row, first_column,
        {static_cast<uint16_t>(face.x + 1U), static_cast<uint16_t>(hinge - 1U),
         static_cast<uint16_t>(face.width - 2U), 2}, 0);
    if (error != ESP_OK) { return error; }
    const uint16_t pin_top = static_cast<uint16_t>(hinge - 5U);
    error = fill_clipped(strip, first_row, first_column,
        {static_cast<uint16_t>(face.x + 1U), pin_top, 3, 10}, kHinge);
    if (error != ESP_OK) { return error; }
    return fill_clipped(strip, first_row, first_column,
        {static_cast<uint16_t>(face.x + face.width - 4U), pin_top, 3, 10}, kHinge);
}

// state: Immutable presentation retained through all native content callbacks.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical y coordinate shared with hit testing and display rotation.
// first_column: Logical x coordinate shared with hit testing and display rotation.
// Exception: Preserves the original native tile, face or glyph error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) { return countdown_result("draw state", ESP_ERR_INVALID_ARG); }
    const auto &presentation = *static_cast<const ResetCountdownPresentation *>(state);
    if (!presentation.valid) { return countdown_result("draw state", ESP_ERR_INVALID_STATE); }
    esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) { return countdown_result("draw tile", error); }
    if (presentation.tile_count == 0) {
        error = CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Clock, kClockIcon, kError,
            strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < presentation.tile_count; ++index) {
        error = draw_tile(presentation.tiles[index], presentation.view.flip_progress, strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < presentation.caption_count; ++index) {
        const auto &line = presentation.captions[index].line;
        error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    return countdown_result("draw rows", error);
}

// presentation: Prepared owner of the central read-only countdown.
// content: Receives an immutable borrowed native callback and bounded damage.
void attach_content(const ResetCountdownPresentation &presentation, SceneContent &content)
{
    content = {};
    content.bounds = damage_rect(kMain);
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_rows;
}

// presentation: Fixed display buffers between frames.
// title_font: Immutable native title asset.
// body_font: Immutable native monospace body asset.
// Exception: Fails if the content revision would cease to be unique.
esp_err_t begin_preparation(ResetCountdownPresentation &presentation,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font)
{
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) { return ESP_ERR_INVALID_STATE; }
    presentation.valid = false;
    presentation.tile_count = 0;
    presentation.caption_count = 0;
    presentation.title_font = &title_font;
    presentation.body_font = &body_font;
    return ESP_OK;
}

// seconds: Exact nonnegative countdown duration bounded by the supported calendar range.
// values: Receives display-order days, hours, minutes, and seconds.
void split_duration(uint64_t seconds, std::array<unsigned, 4> &values)
{
    values = {static_cast<unsigned>(seconds / kDaySeconds), static_cast<unsigned>(seconds / 3600U % 24U),
        static_cast<unsigned>(seconds / 60U % 60U), static_cast<unsigned>(seconds % 60U)};
}

// presentation: Fixed owner of face and glyph storage.
// view: Active timer or elapsed timer with an admitted duration.
// title_font: Native large digit font for up to three digits.
// body_font: Native compact fallback for longer day values and unit labels.
// Exception: Returns original digit or unit-caption preparation error.
esp_err_t prepare_tiles(ResetCountdownPresentation &presentation, const ResetCountdownView &view,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font)
{
    const bool animate = view.status == ResetCountdownStatus::Active && view.flip_progress < kResetCountdownFlipComplete;
    std::array<unsigned, 4> current{};
    std::array<unsigned, 4> previous{};
    split_duration(view.remaining_seconds, current);
    if (animate) { split_duration(view.previous_seconds, previous); }
    for (size_t index = 0; index < presentation.tiles.size(); ++index) {
        auto &tile = presentation.tiles[index];
        const uint16_t left = static_cast<uint16_t>(73U + index * 82U);
        const CODY2D::Rect bounds{left, kFaceTop, kFaceWidth, kFaceHeight};
        tile.face = {};
        tile.face.bounds = bounds;
        tile.face.top_color = kTileTop;
        tile.face.bottom_color = kTileBottom;
        tile.face.corner_radius = 9;
        tile.previous.line = {};
        char digits[11]{};
        std::snprintf(digits, sizeof(digits), "%02u", current[index]);
        const auto &font = current[index] > 999 ? body_font : title_font;
        esp_err_t error = prepare_caption(tile.value, font, digits, bounds, kWhite);
        if (error != ESP_OK) { return error; }
        if (animate && current[index] != previous[index]) {
            std::snprintf(digits, sizeof(digits), "%02u", previous[index]);
            const auto &old_font = previous[index] > 999 ? body_font : title_font;
            error = prepare_caption(tile.previous, old_font, digits, bounds, kWhite);
            if (error != ESP_OK) { return error; }
        }
        error = add_caption(presentation, body_font, kUnits[index], {left, 252, kFaceWidth, 28}, kMuted);
        if (error != ESP_OK) { return error; }
        ++presentation.tile_count;
    }
    return ESP_OK;
}

// status: Whether the account fields are available for this view.
bool has_account_data(ResetCountdownStatus status)
{
    return status == ResetCountdownStatus::Active || status == ResetCountdownStatus::Elapsed ||
        status == ResetCountdownStatus::TimeUnavailable;
}

// presentation: Fixed owner of the reset metadata.
// view: Admitted account fields and independently valid countdown status.
// font: Native monospace metadata font.
// Exception: Returns original caption or formatting error.
esp_err_t prepare_metadata(ResetCountdownPresentation &presentation, const ResetCountdownView &view,
    const CODY2D::MonoFont &font)
{
    const char *status = nullptr;
    switch (view.status) {
    case ResetCountdownStatus::Unavailable: status = "Codex usage unavailable"; break;
    case ResetCountdownStatus::TimeUnavailable: status = "Device time unavailable"; break;
    case ResetCountdownStatus::Stale: status = "Codex usage stale"; break;
    case ResetCountdownStatus::Elapsed:
        status = view.saved ? "Reset passed - sync Codex" : "Reset time passed";
        break;
    case ResetCountdownStatus::Active: break;
    }
    if (status != nullptr) {
        const uint16_t color = has_account_data(view.status) ? kMuted : kError;
        const uint16_t top = view.status == ResetCountdownStatus::Elapsed ? 285 : 263;
        const esp_err_t error = add_caption(presentation, font, status, {73, top, 320, 25}, color);
        if (error != ESP_OK) { return error; }
    }
    if (!has_account_data(view.status)) { return ESP_OK; }

    const auto &fields = view.fields;
    char line[32]{};
    char timezone[10] = "UTC";
    if (view.local_time) {
        const int offset = view.utc_offset_minutes;
        const unsigned minutes = static_cast<unsigned>(offset < 0 ? -offset : offset);
        const int count = std::snprintf(timezone, sizeof(timezone), "UTC%c%02u:%02u",
            offset < 0 ? '-' : '+', minutes / 60U, minutes % 60U);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(timezone)) { return ESP_ERR_INVALID_SIZE; }
    }
    const int date = std::snprintf(line, sizeof(line), "%s %04u-%02u-%02u %02u:%02u",
        timezone, static_cast<unsigned>(fields.year), static_cast<unsigned>(fields.month),
        static_cast<unsigned>(fields.day), static_cast<unsigned>(fields.hour), static_cast<unsigned>(fields.minute));
    if (date < 0 || static_cast<size_t>(date) >= sizeof(line)) { return ESP_ERR_INVALID_SIZE; }
    esp_err_t error = add_caption(presentation, font, line, {73, 314, 320, 26}, kWhite);
    if (error != ESP_OK) { return error; }
    if (view.status == ResetCountdownStatus::Elapsed) { return ESP_OK; }

    const unsigned basis_points = fields.remaining_basis_points;
    const char *label = view.saved ? "at last update" : "remaining";
    const int percent = basis_points % 100U == 0
        ? std::snprintf(line, sizeof(line), "%u%% %s", basis_points / 100U, label)
        : std::snprintf(line, sizeof(line), "%u.%02u%% %s", basis_points / 100U, basis_points % 100U, label);
    if (percent < 0 || static_cast<size_t>(percent) >= sizeof(line)) { return ESP_ERR_INVALID_SIZE; }
    return add_caption(presentation, font, line, {73, 346, 320, 26}, view.saved ? kMuted : kAccent);
}

// left: Current prepared screen source and projection.
// right: Next canonical source and projected flip phase.
bool same_view(const ResetCountdownView &left, const ResetCountdownView &right)
{
    const auto &a = left.fields;
    const auto &b = right.fields;
    return left.status == right.status && left.remaining_seconds == right.remaining_seconds &&
        left.previous_seconds == right.previous_seconds && left.flip_progress == right.flip_progress &&
        left.local_time == right.local_time && left.utc_offset_minutes == right.utc_offset_minutes &&
        left.saved == right.saved &&
        a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour &&
        a.minute == b.minute && a.remaining_basis_points == b.remaining_basis_points;
}

// angle: Clockwise radians from twelve o'clock, excluding one full revolution.
uint16_t perimeter_color(float angle)
{
    constexpr uint16_t stops[]{0x4F9B, 0x651F, 0xB37F, 0xE39B, 0x4F9B};
    const float sample = angle * 4.f / kTau;
    const unsigned index = std::min(static_cast<unsigned>(sample), 3U);
    const auto level = static_cast<uint8_t>((sample - index) * 255.f);
    return CODY2D::blend_rgb565(stops[index + 1U], stops[index], level);
}
} // namespace

// fields: Reset calendar fields and reported remaining allowance in basis points.
bool valid_reset_countdown_fields(const ResetCountdownFields &fields)
{
    return fields.year >= kMinYear && fields.year <= kMaxYear && fields.month >= 1 && fields.month <= 12 &&
        fields.day >= 1 && fields.day <= days_in_month(fields.year, fields.month) && fields.hour <= 23 &&
        fields.minute <= 59 && fields.remaining_basis_points <= 10000;
}

// view: Current account observation and owner-projected flip phase.
// title_font: Immutable native headline and large-digit glyphs.
// body_font: Immutable native monospace detail glyphs.
// presentation: Fixed display-owned state between retired frames.
// content: Receives the prepared central content, excluding the independent perimeter.
// Exception: Returns original glyph/geometry errors or rejects unsupported data and phase.
esp_err_t prepare_reset_countdown(const ResetCountdownView &view,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    ResetCountdownPresentation &presentation, SceneContent &content)
{
    if (view.status > ResetCountdownStatus::Elapsed || view.flip_progress > kResetCountdownFlipComplete ||
        view.remaining_seconds > kMaxSeconds || view.previous_seconds > kMaxSeconds ||
        (view.status == ResetCountdownStatus::Active && view.remaining_seconds == 0) ||
        (view.status != ResetCountdownStatus::Active && view.remaining_seconds != 0) ||
        (view.local_time && (view.utc_offset_minutes <= -1440 || view.utc_offset_minutes >= 1440)) ||
        (has_account_data(view.status) && !valid_reset_countdown_fields(view.fields))) {
        return countdown_result("prepare countdown", ESP_ERR_INVALID_ARG);
    }
    if (presentation.valid && same_view(presentation.view, view) &&
        presentation.title_font == &title_font && presentation.body_font == &body_font) {
        attach_content(presentation, content);
        return ESP_OK;
    }
    esp_err_t error = begin_preparation(presentation, title_font, body_font);
    if (error == ESP_OK) { error = add_caption(presentation, title_font, "Codex reset", {73, 83, 320, 48}, kWhite); }
    const bool has_duration = view.status == ResetCountdownStatus::Active || view.status == ResetCountdownStatus::Elapsed;
    if (error == ESP_OK && has_duration) { error = prepare_tiles(presentation, view, title_font, body_font); }
    if (error == ESP_OK) { error = prepare_metadata(presentation, view, body_font); }
    if (error != ESP_OK) { return countdown_result("prepare countdown", error); }
    presentation.view = view;
    ++presentation.revision;
    presentation.valid = true;
    attach_content(presentation, content);
    return ESP_OK;
}

// presentation: Prepared screen, retained until the central and perimeter draws retire.
bool reset_countdown_ring_visible(const ResetCountdownPresentation &presentation)
{
    return presentation.valid && !presentation.view.saved && presentation.view.fields.remaining_basis_points != 0 &&
        (presentation.view.status == ResetCountdownStatus::Active ||
         presentation.view.status == ResetCountdownStatus::TimeUnavailable);
}

// remaining: Canonical remaining allowance in basis points, or zero to omit its arc.
// battery_percent: Canonical charge from zero to 100, or -1 to omit its ring.
// strip: Borrowed RGB565 tile carrying original stride and storage byte order.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original surface or argument error.
esp_err_t draw_device_perimeter_rows(uint16_t remaining, int8_t battery_percent,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (remaining > 10000 || battery_percent < -1 || battery_percent > 100) {
        return countdown_result("draw perimeter values", ESP_ERR_INVALID_ARG);
    }
    const bool show_battery = battery_percent >= 0;
    if (remaining == 0 && !show_battery) { return ESP_OK; }
    const esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) { return countdown_result("draw perimeter tile", error); }
    const int bottom = first_row + strip.height;
    const int right = first_column + strip.width;
    if (bottom > kCanvasSize || right > kCanvasSize) {
        return countdown_result("draw perimeter bounds", ESP_ERR_INVALID_SIZE);
    }
    const float quota_start = kTau * (10000U - remaining) / 10000.f;
    const float battery_start = kTau * (100 - battery_percent) / 100.f;
    const float outer_edge = remaining > 0 ? kOuterEdge : kBatteryOuterEdge;
    const float inner_edge = show_battery ? kBatteryInnerEdge : kInnerEdge;
    for (int y = first_row; y < bottom; ++y) {
        const float dy = y + .5f - kCenter;
        const float outer_square = outer_edge * outer_edge - dy * dy;
        if (outer_square <= 0.f) { continue; }
        const float outer_extent = std::sqrt(outer_square);
        const float inner_square = inner_edge * inner_edge - dy * dy;
        const float inner_extent = inner_square > 0.f ? std::sqrt(inner_square) : 0.f;
        const int row_left = std::max<int>(first_column, static_cast<int>(std::ceil(kCenter - .5f - outer_extent)));
        const int row_right = std::min(right, static_cast<int>(std::floor(kCenter - .5f + outer_extent)) + 1);
        const int skip_left = static_cast<int>(std::floor(kCenter - .5f - inner_extent)) + 1;
        const int skip_right = static_cast<int>(std::ceil(kCenter - .5f + inner_extent));
        for (int x = row_left; x < row_right; ++x) {
            // Skip the entire interior chord and shade only the perimeter bands.
            if (x >= skip_left && x < skip_right) {
                x = skip_right - 1;
                continue;
            }
            const float dx = x + .5f - kCenter;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const bool battery_pixel = show_battery && radius <= kBatteryOuterEdge;
            const float coverage = battery_pixel
                ? std::min(radius - kBatteryInnerEdge, kBatteryOuterEdge - radius)
                : std::min(radius - kInnerEdge, kOuterEdge - radius);
            if (coverage <= 0.f) { continue; }
            float angle = std::atan2(dx, -dy);
            if (angle < 0.f) { angle += kTau; }
            float alpha = std::min(coverage, 1.f);
            uint16_t foreground = kBatteryTrack;
            if (battery_pixel && battery_percent > 0) {
                // Like the quota ring, the empty arc grows clockwise from twelve.
                const float fill = battery_percent == 100 ? 1.f :
                    std::clamp((angle - battery_start) * radius + .5f, 0.f, 1.f);
                if (fill > 0.f) {
                    const float counterclockwise = kTau - angle;
                    const auto gradient = static_cast<uint8_t>(
                        std::min(counterclockwise / (kTau - battery_start), 1.f) * 255.f);
                    const auto color = CODY2D::blend_rgb565(kBatteryYellow, kBatteryGreen, gradient);
                    foreground = CODY2D::blend_rgb565(color, kBatteryTrack, static_cast<uint8_t>(fill * 255.f));
                }
            } else if (!battery_pixel) {
                // The erased arc grows clockwise from twelve as allowance is consumed.
                const float fill = remaining == 10000 ? 1.f : (angle - quota_start) * radius + .5f;
                alpha = std::min(alpha, fill);
                if (alpha <= 0.f) { continue; }
                foreground = perimeter_color(angle);
            }
            auto &pixel = strip.pixels[(y - first_row) * strip.stride + x - first_column];
            const uint16_t background = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            const uint16_t color = CODY2D::blend_rgb565(foreground, background,
                static_cast<uint8_t>(alpha * 255.f + .5f));
            pixel = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
    return ESP_OK;
}

// remaining: Canonical remaining allowance in basis points.
// strip: Borrowed RGB565 tile carrying original stride and storage byte order.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original perimeter drawing error.
esp_err_t draw_reset_countdown_ring_rows(uint16_t remaining,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    return draw_device_perimeter_rows(remaining, -1, strip, first_row, first_column);
}

// presentation: Immutable legacy screen and exact remaining basis points.
// strip: Borrowed RGB565 tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original perimeter drawing error.
esp_err_t draw_reset_countdown_ring_rows(const ResetCountdownPresentation &presentation,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    return reset_countdown_ring_visible(presentation) ?
        draw_reset_countdown_ring_rows(presentation.view.fields.remaining_basis_points, strip, first_row, first_column) :
        ESP_OK;
}

} // namespace CODY
