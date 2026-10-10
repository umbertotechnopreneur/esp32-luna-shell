/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "CODY_debug_views.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "CODY2D_controls.hpp"
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_debug_views";
constexpr uint16_t kTextColor = 0xF7BE;

// text: Null-terminated bounded diagnostic ASCII line.
// top: Logical first row, inside the same circular chord as all controls.
// font: Shared immutable ASCII glyph coverage.
// line: Receives prepared text only after circular bounds pass.
// Exception: Returns native font/layout errors or INVALID_SIZE for clipped/overwide text.
esp_err_t prepare_line(const char *text, uint16_t top, const CODY2D::MonoFont &font,
                       CODY2D::TextLine &line)
{
    const size_t length = std::strlen(text);
    const unsigned width = length == 0 ? 0 : (length - 1) * font.advance + font.width;
    if (length == 0 || width > 320 || top + font.height > 420) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int left = (466 - width) / 2;
    const int half_width = 233 - left;
    const int upper = static_cast<int>(top) - 233;
    const int lower = static_cast<int>(top + font.height - 1) - 233;
    if (half_width * half_width + std::max(upper * upper, lower * lower) >= 233 * 233) {
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, kTextColor, line);
}
}  // namespace

// face: Current worker-owned composed channel values, never an independent sensor sample.
// first_channel: Absolute storage traversal position; catalog names remain the domain identity.
// font: Same native font on firmware and PC.
// list: Stable text/layout destination, invalidated until all rows succeed.
// Exception: Logs and returns actual finite-value, range and text preparation failures.
esp_err_t prepare_debug_channel_list(const Channels &face, uint8_t first_channel,
                                    const CODY2D::MonoFont &font, DebugChannelList &list)
{
    list = {};
    if (first_channel >= kFaceChannelCount) {
        ESP_LOGE(kTag, "Channel list rejected first=%u count=%u", first_channel, kFaceChannelCount);
        return ESP_ERR_INVALID_ARG;
    }
    static_assert(std::size(Data::channel_names) == kFaceChannelCount);
    const unsigned count = std::min<unsigned>(kDebugChannelVisibleRows, kFaceChannelCount - first_channel);
    std::snprintf(list.text[0], sizeof(list.text[0]), "FACE CHANNELS %u", kFaceChannelCount);
    esp_err_t error = prepare_line(list.text[0], 62, font, list.lines[0]);
    for (unsigned index = 0; error == ESP_OK && index < count; ++index) {
        const unsigned channel_index = first_channel + index;
        const float value = face.values[channel_index];
        if (!std::isfinite(value)) {
            ESP_LOGE(kTag, "Channel value rejected channel=%s nonfinite", Data::channel_names[channel_index]);
            return ESP_ERR_INVALID_ARG;
        }
        const int length = std::snprintf(list.text[index + 1], sizeof(list.text[index + 1]),
            "%s %.2f", Data::channel_names[channel_index], static_cast<double>(value));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(list.text[index + 1])) {
            error = ESP_ERR_INVALID_SIZE;
        } else {
            error = prepare_line(list.text[index + 1], static_cast<uint16_t>(100 + index * 34),
                                 font, list.lines[index + 1]);
        }
    }
    if (error == ESP_OK) {
        std::snprintf(list.text[count + 1], sizeof(list.text[count + 1]), "%u-%u / %u",
            first_channel + 1, first_channel + count, kFaceChannelCount);
        error = prepare_line(list.text[count + 1], 385, font, list.lines[count + 1]);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Channel list text preparation failed (%s)", esp_err_to_name(error));
        return error;
    }
    list.line_count = static_cast<uint8_t>(count + 2);
    list.valid = true;
    return ESP_OK;
}

// list: Successfully prepared current text, stable across tile composition.
// strip: Shared native RGB565 tile with explicit byte order.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns original native raster errors or INVALID_STATE for unprepared content.
esp_err_t draw_debug_channel_list_rows(const DebugChannelList &list, CODY2D::Surface strip,
                                      uint16_t first_row, uint16_t first_column)
{
    if (!list.valid) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t index = 0; index < list.line_count; ++index) {
        const auto &line = list.lines[index];
        const esp_err_t error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

// strip: Borrowed native tile; all geometry uses the same logical circle as touch.
// first_row: Logical y origin.
// first_column: Logical x origin.
// Exception: Returns original capsule or round-cap stroke errors.
esp_err_t draw_debug_view_back_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    CODY2D::CapsuleControl back;
    back.bounds = {10, 203, 60, 60};
    back.top_color = 0x2104;
    back.bottom_color = 0x1082;
    back.corner_radius = 30;
    esp_err_t error = CODY2D::draw_capsule_rows(back, strip, first_row, first_column);
    CODY2D::PreparedStroke strokes[2];
    if (error == ESP_OK) {
        error = CODY2D::prepare_stroke({45, 223}, {35, 233}, 3, kTextColor, strokes[0]);
    }
    if (error == ESP_OK) {
        error = CODY2D::prepare_stroke({35, 233}, {45, 243}, 3, kTextColor, strokes[1]);
    }
    if (error == ESP_OK) {
        error = CODY2D::stroke_prepared_batch_rows(strip, first_row, strokes, 2, first_column);
    }
    return error;
}

// point: Same logical contact coordinate used by native rendering after rotation mapping.
bool debug_view_back_contains(CODY2D::Point point)
{
    const int x = point.x - 40;
    const int y = point.y - 233;
    return x * x + y * y <= 30 * 30;
}
}  // namespace CODY
