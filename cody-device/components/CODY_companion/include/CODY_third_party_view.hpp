#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "CODY_screen_content.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

// This is a verified public document; its full notices remain authoritative.
inline constexpr char kThirdPartyNoticesUrl[] =
    "https://github.com/umbertotechnopreneur/esp32-luna-shell/blob/main/THIRD_PARTY_NOTICES.md";

// Display owner retains this state through the entire strip transaction.
struct ThirdPartyView {
    std::array<CODY2D::TextLine, 6> lines{};
    uint8_t count = 0;
    bool prepared = false;
};

// state: Immutable view retained until the frame retires.
// strip: Borrowed RGB565 destination tile.
// first_row: Logical top row of the tile.
// first_column: Logical left column of the tile.
// Exception: Returns the first native surface or text raster error.
inline esp_err_t draw_third_party_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) return ESP_ERR_INVALID_ARG;
    const auto &view = *static_cast<const ThirdPartyView *>(state);
    auto error = CODY2D::validate_surface(strip);
    for (uint8_t i = 0; error == ESP_OK && i < view.count; ++i) {
        const auto &line = view.lines[i];
        error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    return error;
}

// view: Owner-allocated presentation whose buffers survive frame retirement.
// font: Immutable body font already owned by the renderer.
// content: Receives the bounded callback and shared styled heading.
// Exception: Returns the first text measurement, layout or title error.
inline esp_err_t prepare_third_party_view(ThirdPartyView &view, const CODY2D::MonoFont &font,
    SceneContent &content)
{
    if (!view.prepared) {
        static constexpr const char *text[] = {
            "ESP-IDF", "Waveshare BSP", "Muse Gadget SDK",
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
            "eSpeak NG (optional)",
#endif
            "See project repository", "THIRD_PARTY_NOTICES.md",
        };
        constexpr auto text_count = sizeof(text) / sizeof(text[0]);
        static_assert(text_count <= 6, "Keep the third-party view bounded");
        view.count = 0;
        for (size_t i = 0; i < text_count; ++i) {
            const auto length = static_cast<uint16_t>(std::strlen(text[i]));
            uint32_t width = 0;
            auto error = CODY2D::measure_text_width(font, {text[i], length}, width);
            if (error != ESP_OK) return error;
            if (width > 320U || font.height > 28U) return ESP_ERR_INVALID_SIZE;
            const auto y = static_cast<int16_t>(i < text_count - 2 ? 150U + i * 34U :
                300U + (i - (text_count - 2U)) * 30U);
            error = CODY2D::prepare_text_line(font, {text[i], length},
                {static_cast<int16_t>((466U - width) / 2U), y},
                i < text_count - 2 ? Theme::text_primary : Theme::text_muted, view.lines[i]);
            if (error != ESP_OK) return error;
            ++view.count;
        }
        view.prepared = true;
    }
    ScreenHeader header{};
    header.title = {"Third party", 11};
    header.title_bounds = {73, 83, 320, 44};
    return ScreenContent::attach(content, {73, 80, 393, 365}, &view, 1,
        draw_third_party_rows, header);
}

} // namespace CODY
