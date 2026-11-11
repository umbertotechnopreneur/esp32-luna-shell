#pragma once

#include "CODY2D_text.hpp"
#include "CODY_title_style.hpp"
#include <cstring>
#include <initializer_list>

// These immutable bundled faces are already linked by Cody's native renderer.
namespace CODY::HomeFonts {
extern const CODY2D::MonoFont heading_font;
extern const CODY2D::MonoFont small_font;
}
namespace CODY::SceneData {
extern const CODY2D::MonoFont body_font;
extern const CODY2D::MonoFont title_font;
}

namespace CODY::Typography {

// No parameters: application system face for general labels and prose.
inline const CODY2D::MonoFont &system()
{
    return HomeFonts::heading_font;
}

// No parameters: fixed-advance face for aligned numbers, codes and measurement data.
inline const CODY2D::MonoFont &monospace()
{
    return SceneData::body_font;
}

// No parameters: larger fixed-advance face for aligned readouts.
inline const CODY2D::MonoFont &monospace_large()
{
    return SceneData::title_font;
}

// text: Nonempty printable caption whose lifetime is retained by its caller.
// bounds: Existing complete layout slot; this helper never expands it.
// fallback: Explicit previous face when the system face cannot fit the slot.
// selected: Receives the system or fallback face only after successful measurement.
// Exception: Returns the original glyph measurement error, or invalid argument for missing text.
inline esp_err_t label_font(const char *text, CODY2D::Rect bounds, const CODY2D::MonoFont &fallback,
    const CODY2D::MonoFont *&selected)
{
    if (text == nullptr || text[0] == '\0') return ESP_ERR_INVALID_ARG;
    const size_t length = std::strlen(text);
    if (length > UINT16_MAX) return ESP_ERR_INVALID_SIZE;
    for (const auto *candidate : {&HomeFonts::heading_font, &HomeFonts::small_font}) {
        if (candidate->height > bounds.height || candidate->width > bounds.width || candidate->advance == 0) continue;
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(*candidate, {text, static_cast<uint16_t>(length)}, width);
        if (error != ESP_OK) return error;
        // Some existing presenters still bound labels by fixed advance. Preserve
        // their complete text rather than introducing extra ellipses on migration.
        const size_t conservative = (bounds.width - candidate->width) / candidate->advance + 1U;
        if (width <= bounds.width && length <= conservative) {
            selected = candidate;
            return ESP_OK;
        }
    }
    selected = &fallback;
    return ESP_OK;
}

} // namespace CODY::Typography
