#include "CODY_audio_view.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_audio_view";
constexpr DamageRect kContentBounds{73, 90, 393, 383};
constexpr CODY2D::Rect kHeading{73, 90, 320, 44};
constexpr CODY2D::Rect kLegend{73, 137, 320, 25};
constexpr CODY2D::Rect kClose{113, 321, 240, 60};
constexpr std::array<CODY2D::Rect, 2> kBars{{{93, 207, 280, 18}, {93, 277, 280, 18}}};
constexpr uint16_t kInnerWidth = 278;
constexpr uint32_t kPcmFullScale = 32768;
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kMint = 0x5FF1;
constexpr uint16_t kAmber = 0xFE27;

// operation: Native presentation operation that may fail.
// error: Original failure preserved for the display owner.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error));
    return error;
}

// font: Immutable native glyphs.
// text: ASCII caption retained through frame retirement.
// bounds: Admitted logical text slot.
// color: Native RGB565 foreground.
// line: Receives the measured centered caption.
// Exception: Preserves glyph failures and rejects text that cannot fit.
esp_err_t prepare_line(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line)
{
    const auto length = std::strlen(text);
    if (length == 0 || length > 28 || font.height > bounds.height) return ESP_ERR_INVALID_SIZE;
    const CODY2D::TextSpan span{text, static_cast<uint16_t>(length)};
    uint32_t width = 0;
    const auto error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) return error;
    if (width > bounds.width) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, span,
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, line);
}

// audio: Canonical observation; no aggregate-to-channel substitution is permitted.
// channel: Physical channel index within the two published channel slots.
// read_error: Original snapshot or microphone-demand error.
// now_us: Current monotonic time after the snapshot read.
AudioView::Level project_level(const LUNASHELL::AudioSnapshot &audio, size_t channel,
    esp_err_t read_error, int64_t now_us)
{
    AudioView::Level level{};
    const auto &sample = audio.microphone;
    if (read_error != ESP_OK) {
        level.state = read_error == ESP_ERR_NOT_FINISHED ? MicrophoneLevelState::Waiting :
            read_error == ESP_ERR_NOT_SUPPORTED ? MicrophoneLevelState::Unavailable : MicrophoneLevelState::Failed;
        level.error = read_error;
    } else if (sample.health == LUNASHELL::DeviceHealth::Missing || sample.last_error == ESP_ERR_NOT_SUPPORTED) {
        level.state = MicrophoneLevelState::Unavailable;
    } else if (sample.data_stale) {
        level.state = MicrophoneLevelState::Stale;
    } else if (sample.last_error != ESP_OK) {
        level.state = sample.last_attempt_at_us == 0 ? MicrophoneLevelState::Waiting : MicrophoneLevelState::Failed;
        level.error = sample.last_error;
    } else if (!sample.data_valid || sample.last_success_at_us == 0) {
        level.state = MicrophoneLevelState::Waiting;
    } else if (sample.last_success_at_us < 0 || sample.last_success_at_us > now_us ||
        audio.channel_count > 2) {
        level.state = MicrophoneLevelState::Failed;
        level.error = ESP_ERR_INVALID_STATE;
    } else if (sample.health != LUNASHELL::DeviceHealth::Ready || channel >= audio.channel_count) {
        level.state = MicrophoneLevelState::Unavailable;
    } else {
        const uint32_t mean = audio.channel_mean_level[channel];
        const uint32_t peak = audio.channel_peak_level[channel];
        if (mean > peak || peak > kPcmFullScale) {
            level.state = MicrophoneLevelState::Failed;
            level.error = ESP_ERR_INVALID_STATE;
        } else {
            level.state = MicrophoneLevelState::Current;
            level.mean_width = static_cast<uint16_t>(mean * kInnerWidth / kPcmFullScale);
            level.peak_offset = static_cast<uint16_t>(peak * (kInnerWidth - 1U) / kPcmFullScale);
            level.mean_percent = static_cast<uint8_t>(mean * 100U / kPcmFullScale);
            level.peak_percent = static_cast<uint8_t>(peak * 100U / kPcmFullScale);
        }
    }
    return level;
}

// first: Current display projection.
// second: Previous fully prepared projection.
bool same_level(const AudioView::Level &first, const AudioView::Level &second)
{
    return first.state == second.state && first.error == second.error &&
        first.mean_width == second.mean_width && first.peak_offset == second.peak_offset &&
        first.mean_percent == second.mean_percent && first.peak_percent == second.peak_percent;
}

// strip: Borrowed RGB565 tile in logical orientation.
// first_row: Tile's logical y origin.
// first_column: Tile's logical x origin.
// bounds: Complete logical rectangle, clipped only to the borrowed tile.
// color: Native RGB565 fill color.
// Exception: Returns the original surface failure.
esp_err_t fill_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
    CODY2D::Rect bounds, uint16_t color)
{
    const int left = std::max<int>(bounds.x, first_column);
    const int top = std::max<int>(bounds.y, first_row);
    const int right = std::min<int>(bounds.x + bounds.width, first_column + strip.width);
    const int bottom = std::min<int>(bounds.y + bounds.height, first_row + strip.height);
    if (left >= right || top >= bottom) return ESP_OK;
    return CODY2D::fill_rect(strip,
        {static_cast<uint16_t>(left - first_column), static_cast<uint16_t>(top - first_row),
         static_cast<uint16_t>(right - left), static_cast<uint16_t>(bottom - top)}, color);
}

// state: Immutable display-owned view borrowed through frame retirement.
// strip: Borrowed native RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Preserves the first raster failure.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto *view = static_cast<const AudioView *>(state);
    if (view == nullptr || !view->valid) return result("draw state", ESP_ERR_INVALID_STATE);
    for (const auto &line : view->captions) {
        const auto error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
        if (error != ESP_OK) return result("draw caption", error);
    }
    for (size_t channel = 0; channel < view->levels.size(); ++channel) {
        const auto &level = view->levels[channel];
        const auto &bar = kBars[channel];
        auto error = fill_rows(strip, first_row, first_column, bar, 0x4A69);
        if (error == ESP_OK) error = fill_rows(strip, first_row, first_column,
            {static_cast<uint16_t>(bar.x + 1U), static_cast<uint16_t>(bar.y + 1U), kInnerWidth, 16}, 0x18C3);
        if (error == ESP_OK && level.state == MicrophoneLevelState::Current) {
            error = fill_rows(strip, first_row, first_column,
                {static_cast<uint16_t>(bar.x + 1U), static_cast<uint16_t>(bar.y + 1U), level.mean_width, 16},
                channel == 0 ? kCyan : kMint);
            if (error == ESP_OK) error = fill_rows(strip, first_row, first_column,
                {static_cast<uint16_t>(bar.x + 1U + level.peak_offset), static_cast<uint16_t>(bar.y + 1U), 1, 16},
                level.peak_percent >= 90 ? kAmber : kWhite);
        }
        if (error != ESP_OK) return result("draw level", error);
    }
    return result("draw Close", CODY2D::draw_capsule_rows(view->close, strip, first_row, first_column));
}

// view: Persistent prepared view with stable caption storage.
// content: Receives the renderer and its complete damage envelope.
void attach_content(const AudioView &view, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.state = &view;
    content.revision = view.revision;
    content.draw_rows = draw_rows;
}

} // namespace

// audio: Canonical audio observation, never a direct hardware read.
// read_error: Original read or microphone-demand admission error.
// now_us: Monotonic timestamp obtained after the canonical read.
// title_font: Immutable native heading glyphs.
// body_font: Immutable native caption glyphs.
// view: Persistent display storage updated after the previous frame retires.
// content: Receives the prepared borrowed renderer.
// Exception: Preserves original layout errors and rejects revision overflow.
esp_err_t prepare_audio_view(const LUNASHELL::AudioSnapshot &audio, esp_err_t read_error,
    int64_t now_us, const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AudioView &view, SceneContent &content)
{
    const std::array<AudioView::Level, 2> levels{
        project_level(audio, 0, read_error, now_us), project_level(audio, 1, read_error, now_us)};
    if (view.valid && same_level(levels[0], view.levels[0]) && same_level(levels[1], view.levels[1]) &&
        view.title_font == &title_font && view.body_font == &body_font) {
        attach_content(view, content);
        return ESP_OK;
    }
    if (view.revision == std::numeric_limits<uint32_t>::max()) return result("view revision", ESP_ERR_INVALID_STATE);
    view.valid = false;
    view.captions = {};
    auto error = prepare_line(title_font, "Audio", kHeading, kCyan, view.captions[0]);
    if (error == ESP_OK) error = prepare_line(body_font, "Mean / peak (PCM)", kLegend, kMuted, view.captions[1]);
    for (size_t channel = 0; error == ESP_OK && channel < levels.size(); ++channel) {
        const auto &level = levels[channel];
        auto &readout = view.readouts[channel];
        int written = 0;
        switch (level.state) {
        case MicrophoneLevelState::Current:
            written = std::snprintf(readout.data(), readout.size(), "%u%% / %u%%",
                static_cast<unsigned>(level.mean_percent), static_cast<unsigned>(level.peak_percent));
            break;
        case MicrophoneLevelState::Failed:
            written = std::snprintf(readout.data(), readout.size(), "Error 0x%lx",
                static_cast<unsigned long>(static_cast<uint32_t>(level.error)));
            break;
        default: {
            const char *status = level.state == MicrophoneLevelState::Waiting ? "Waiting" :
                level.state == MicrophoneLevelState::Stale ? "Stale" : "Unavailable";
            written = std::snprintf(readout.data(), readout.size(), "%s", status);
            break;
        }
        }
        if (written <= 0 || static_cast<size_t>(written) >= readout.size()) return result("format level", ESP_ERR_INVALID_SIZE);
        const uint16_t top = static_cast<uint16_t>(174U + channel * 70U);
        error = prepare_line(body_font, channel == 0 ? "Mic 1" : "Mic 2", {93, top, 100, 25},
            channel == 0 ? kCyan : kMint, view.captions[2U + channel * 2U]);
        if (error == ESP_OK) error = prepare_line(body_font, readout.data(), {193, top, 180, 25},
            level.state == MicrophoneLevelState::Current ? kWhite : kMuted, view.captions[3U + channel * 2U]);
    }
    view.close = {kClose, {}, 0x2947, 0x10A4, true};
    view.close.corner_radius = 28;
    view.close.border_color = 0x73AF;
    if (error == ESP_OK) error = prepare_line(body_font, "Close", kClose, kWhite, view.close.caption);
    if (error != ESP_OK) return result("prepare Audio", error);
    view.levels = levels;
    view.title_font = &title_font;
    view.body_font = &body_font;
    ++view.revision;
    view.valid = true;
    attach_content(view, content);
    return ESP_OK;
}

// view: Last fully presented Audio page.
// point: Logical touch position matching the visible Close control.
bool audio_close_at(const AudioView &view, CODY2D::Point point)
{
    return view.valid && CODY2D::capsule_contains(view.close, point);
}

} // namespace CODY
