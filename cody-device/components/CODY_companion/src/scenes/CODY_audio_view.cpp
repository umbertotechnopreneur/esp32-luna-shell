#include "CODY_audio_view.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_title_style.hpp"
#include "CODY_typography.hpp"
#include "CODY_ui_components.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_audio_view";
constexpr DamageRect kContentBounds{73, 90, 393, 383};
constexpr CODY2D::Rect kHeading{73, 90, 320, 38};
constexpr CODY2D::Rect kLegend{73, 130, 320, 22};
constexpr CODY2D::Rect kClose{113, 321, 240, 60};
constexpr std::array<CODY2D::Rect, 3> kBars{{{143, 183, 180, 14}, {143, 233, 180, 14}, {143, 283, 180, 14}}};
constexpr std::array<CODY2D::Rect, 4> kAdjustments{{{93, 174, 40, 40}, {333, 174, 40, 40},
    {93, 224, 40, 40}, {333, 224, 40, 40}}};
constexpr uint16_t kInnerWidth = 178;
constexpr uint32_t kPcmFullScale = 32768;
constexpr uint16_t kWhite = UI::Palette::text;
constexpr uint16_t kMuted = UI::Palette::muted;
constexpr uint16_t kCyan = UI::Palette::cyan;
constexpr uint16_t kMint = UI::Palette::positive;
constexpr uint16_t kAmber = UI::Palette::warning;

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
    return UI::prepare_text(font, text, bounds, color, line, 28);
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

// audio: Canonical playback snapshot with level supplied by the successful write owner.
// read_error: Original enclosing snapshot read result.
// now_us: Monotonic time after the snapshot read.
AudioView::Level project_playback(const LUNASHELL::AudioSnapshot &audio, esp_err_t read_error, int64_t now_us)
{
    AudioView::Level level{};
    const auto &playback = audio.playback;
    if (read_error != ESP_OK) {
        level.state = MicrophoneLevelState::Failed;
        level.error = read_error;
    } else if (playback.data_stale) {
        level.state = MicrophoneLevelState::Stale;
    } else if (playback.last_error != ESP_OK) {
        level.state = MicrophoneLevelState::Failed;
        level.error = playback.last_error;
    } else if (!playback.data_valid || playback.last_success_at_us <= 0) {
        level.state = MicrophoneLevelState::Unavailable;
    } else if (playback.last_success_at_us > now_us || playback.mean_level > kPcmFullScale) {
        level.state = MicrophoneLevelState::Failed;
        level.error = ESP_ERR_INVALID_STATE;
    } else if (playback.state != LUNASHELL::PlaybackState::Playing || !playback.level_valid) {
        level.state = MicrophoneLevelState::Waiting;
    } else {
        level.state = MicrophoneLevelState::Current;
        level.mean_width = static_cast<uint16_t>(playback.mean_level * kInnerWidth / kPcmFullScale);
        level.mean_percent = static_cast<uint8_t>(playback.mean_level * 100U / kPcmFullScale);
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
    for (size_t index = 0; index < view->captions.size(); ++index) {
        const auto &line = view->captions[index];
        const auto error = index == 0 ? ESP_OK :
            CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
        if (error != ESP_OK) return result("draw caption", error);
    }
    for (size_t channel = 0; channel < view->levels.size(); ++channel) {
        const auto &level = view->levels[channel];
        const auto &bar = kBars[channel];
        auto error = fill_rows(strip, first_row, first_column, bar, Theme::audio_meter_border);
        if (error == ESP_OK) error = fill_rows(strip, first_row, first_column,
            {static_cast<uint16_t>(bar.x + 1U), static_cast<uint16_t>(bar.y + 1U), kInnerWidth, 12}, Theme::audio_meter_track);
        if (error == ESP_OK && level.state == MicrophoneLevelState::Current) {
            error = fill_rows(strip, first_row, first_column,
                {static_cast<uint16_t>(bar.x + 1U), static_cast<uint16_t>(bar.y + 1U), level.mean_width, 12},
                channel == 0 ? kCyan : kMint);
            if (error == ESP_OK && channel != 0) error = fill_rows(strip, first_row, first_column,
                {static_cast<uint16_t>(bar.x + 1U + level.peak_offset), static_cast<uint16_t>(bar.y + 1U), 1, 12},
                level.peak_percent >= 90 ? kAmber : kWhite);
        }
        if (error != ESP_OK) return result("draw level", error);
    }
    for (size_t index = 0; index < kAdjustments.size(); ++index) {
        const auto &bounds = kAdjustments[index];
        const bool enabled = (view->enabled_controls & (1U << index)) != 0;
        const uint16_t color = enabled ? (index < 2 ? kCyan : kMint) : kMuted;
        auto error = fill_rows(strip, first_row, first_column,
            {static_cast<uint16_t>(bounds.x + 12), static_cast<uint16_t>(bounds.y + 18), 16, 3}, color);
        if (error == ESP_OK && (index & 1U) != 0) error = fill_rows(strip, first_row, first_column,
            {static_cast<uint16_t>(bounds.x + 18), static_cast<uint16_t>(bounds.y + 12), 3, 16}, color);
        if (error != ESP_OK) return result("draw audio adjustment", error);
    }
    return result("draw Close", UI::draw_text_action(view->close, strip, first_row, first_column));
}

// view: Persistent prepared view with stable caption storage.
// content: Receives the renderer and its complete damage envelope.
esp_err_t attach_content(const AudioView &view, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.state = &view;
    content.revision = view.revision;
    content.draw_rows = draw_rows;
    return prepare_styled_title(view.captions[0].text, kHeading, content.heading, 3, true);
}

} // namespace

// audio: Canonical audio observation, never a direct hardware read.
// read_error: Original read or microphone-demand admission error.
// playback_error: Original enclosing snapshot read result, independently of microphone demand.
// preferences: Canonical audio control settings.
// preferences_error: Original settings projection error.
// settings_pending: One settings operation is still being applied.
// settings_error: Original result of the last operation.
// now_us: Monotonic timestamp obtained after the canonical read.
// title_font: Immutable native heading glyphs.
// body_font: Immutable native caption glyphs.
// view: Persistent display storage updated after the previous frame retires.
// content: Receives the prepared borrowed renderer.
// Exception: Preserves original layout errors and rejects revision overflow.
esp_err_t prepare_audio_view(const LUNASHELL::AudioSnapshot &audio, esp_err_t read_error, esp_err_t playback_error,
    const LUNASHELL::CodyPreferencesSnapshot &preferences, esp_err_t preferences_error,
    bool settings_pending, esp_err_t settings_error,
    int64_t now_us, const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AudioView &view, SceneContent &content)
{
    const std::array<AudioView::Level, 3> levels{
        project_playback(audio, playback_error, now_us), project_level(audio, 0, read_error, now_us),
        project_level(audio, 1, read_error, now_us)};
    const bool volume_valid = preferences_error == ESP_OK && preferences.playback_volume_valid &&
        preferences.playback_volume_error == ESP_OK && preferences.playback_volume_percent <= 100;
    const bool gain_valid = preferences_error == ESP_OK && preferences.microphone_gain_valid &&
        preferences.microphone_gain_error == ESP_OK && preferences.microphone_gain_db <= 30 &&
        preferences.microphone_gain_db % 3 == 0;
    uint8_t controls = 0;
    if (!settings_pending && volume_valid) {
        if (preferences.playback_volume_percent > 0) controls |= 1U;
        if (preferences.playback_volume_percent < 100) controls |= 2U;
    }
    if (!settings_pending && gain_valid) {
        if (preferences.microphone_gain_db > 0) controls |= 4U;
        if (preferences.microphone_gain_db < 30) controls |= 8U;
    }
    esp_err_t shown_error = settings_error != ESP_OK ? settings_error : preferences_error;
    if (shown_error == ESP_OK && !volume_valid && preferences.playback_volume_error != ESP_ERR_NOT_FINISHED)
        shown_error = preferences.playback_volume_error != ESP_OK ? preferences.playback_volume_error : ESP_ERR_INVALID_RESPONSE;
    if (shown_error == ESP_OK && !gain_valid && preferences.microphone_gain_error != ESP_ERR_NOT_FINISHED)
        shown_error = preferences.microphone_gain_error != ESP_OK ? preferences.microphone_gain_error : ESP_ERR_INVALID_RESPONSE;
    const uint8_t volume = volume_valid ? preferences.playback_volume_percent : 0;
    const uint8_t gain = gain_valid ? preferences.microphone_gain_db : 0;
    if (view.valid && same_level(levels[0], view.levels[0]) && same_level(levels[1], view.levels[1]) &&
        same_level(levels[2], view.levels[2]) &&
        view.playback_volume == volume && view.microphone_gain == gain && view.enabled_controls == controls &&
        view.playback_volume_valid == volume_valid && view.microphone_gain_valid == gain_valid &&
        view.settings_pending == settings_pending && view.settings_error == shown_error &&
        view.title_font == &title_font && view.body_font == &body_font) {
        return attach_content(view, content);
    }
    if (view.revision == std::numeric_limits<uint32_t>::max()) return result("view revision", ESP_ERR_INVALID_STATE);
    view.valid = false;
    view.captions = {};
    view.captions[0].text = {"Audio", 5};
    auto error = ESP_OK;
    if (shown_error != ESP_OK) {
        std::snprintf(view.legend.data(), view.legend.size(), "Error 0x%lx",
            static_cast<unsigned long>(static_cast<uint32_t>(shown_error)));
    } else {
        const char *legend = settings_pending ? "Applying..." :
            (!volume_valid || !gain_valid) ? "Settings unavailable" : "Out% In dB | VU";
        std::snprintf(view.legend.data(), view.legend.size(), "%s", legend);
    }
    const CODY2D::MonoFont *legend_face = nullptr;
    if (error == ESP_OK) error = Typography::label_font(view.legend.data(), kLegend, body_font, legend_face);
    if (error == ESP_OK) error = prepare_line(*legend_face, view.legend.data(), kLegend,
        shown_error == ESP_OK ? kMuted : kAmber, view.captions[1]);
    for (size_t channel = 0; error == ESP_OK && channel < levels.size(); ++channel) {
        const auto &level = levels[channel];
        auto &readout = view.readouts[channel];
        int written = 0;
        switch (level.state) {
        case MicrophoneLevelState::Current:
            written = channel == 0 ? std::snprintf(readout.data(), readout.size(), "%u",
                static_cast<unsigned>(level.mean_percent)) :
                std::snprintf(readout.data(), readout.size(), "%u/%u",
                    static_cast<unsigned>(level.mean_percent), static_cast<unsigned>(level.peak_percent));
            break;
        case MicrophoneLevelState::Failed:
            written = std::snprintf(readout.data(), readout.size(), "Error");
            break;
        default: {
            const char *status = level.state == MicrophoneLevelState::Waiting ? "Wait" :
                level.state == MicrophoneLevelState::Stale ? "Stale" : "N/A";
            written = std::snprintf(readout.data(), readout.size(), "%s", status);
            break;
        }
        }
        if (written <= 0 || static_cast<size_t>(written) >= readout.size()) return result("format level", ESP_ERR_INVALID_SIZE);
        const uint16_t top = static_cast<uint16_t>(156U + channel * 50U);
        auto &label_storage = view.labels[channel];
        if (channel == 0 && volume_valid) std::snprintf(label_storage.data(), label_storage.size(), "O %u%%",
            static_cast<unsigned>(volume));
        else if (channel == 0) std::snprintf(label_storage.data(), label_storage.size(), "O --");
        else if (channel == 1 && gain_valid) std::snprintf(label_storage.data(), label_storage.size(), "I %udB",
            static_cast<unsigned>(gain));
        else if (channel == 1) std::snprintf(label_storage.data(), label_storage.size(), "I --");
        else std::snprintf(label_storage.data(), label_storage.size(), "Mic2");
        const char *label = label_storage.data();
        const CODY2D::Rect label_bounds{143, top, 85, 25};
        const CODY2D::MonoFont *label_face = nullptr;
        error = Typography::label_font(label, label_bounds, body_font, label_face);
        if (error == ESP_OK) error = prepare_line(*label_face, label, label_bounds,
            channel == 0 ? kCyan : kMint, view.captions[2U + channel * 2U]);
        if (error == ESP_OK) error = prepare_line(Typography::monospace(), readout.data(), {228, top, 95, 25},
            level.state == MicrophoneLevelState::Current ? kWhite : kMuted, view.captions[3U + channel * 2U]);
    }
    view.close = {kClose, {}, 0, 0, true};
    view.close.icon = CODY2D::ActionIcon::Close;
    view.close.icon_color = kCyan;
    // Center the icon and caption as one group while keeping the original touch rectangle.
    const CODY2D::Rect close_text{static_cast<uint16_t>(kClose.x + 28U), kClose.y,
        static_cast<uint16_t>(kClose.width - 28U), kClose.height};
    if (error == ESP_OK) error = UI::prepare_text(body_font, "Close", close_text, kWhite, view.close.caption, 28);
    if (error == ESP_OK) view.close.icon_center = {
        static_cast<int16_t>(view.close.caption.origin.x - 16),
        static_cast<int16_t>(kClose.y + kClose.height / 2U)};
    if (error != ESP_OK) return result("prepare Audio", error);
    view.levels = levels;
    view.playback_volume = volume;
    view.microphone_gain = gain;
    view.playback_volume_valid = volume_valid;
    view.microphone_gain_valid = gain_valid;
    view.enabled_controls = controls;
    view.settings_pending = settings_pending;
    view.settings_error = shown_error;
    view.title_font = &title_font;
    view.body_font = &body_font;
    ++view.revision;
    view.valid = true;
    return attach_content(view, content);
}

// view: Last fully presented Audio page.
// point: Logical touch position matching the visible Close control.
bool audio_close_at(const AudioView &view, CODY2D::Point point)
{
    return view.valid && CODY2D::capsule_contains(view.close, point);
}

// view: Last retired Audio presentation and its admitted adjustment controls.
// point: Logical touch position in the same rotation.
AudioAdjustment audio_adjustment_at(const AudioView &view, CODY2D::Point point)
{
    if (!view.valid) return AudioAdjustment::None;
    for (size_t index = 0; index < kAdjustments.size(); ++index) {
        const auto &bounds = kAdjustments[index];
        if ((view.enabled_controls & (1U << index)) != 0 && point.x >= bounds.x && point.y >= bounds.y &&
            point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height)
            return static_cast<AudioAdjustment>(index + 1U);
    }
    return AudioAdjustment::None;
}

} // namespace CODY
